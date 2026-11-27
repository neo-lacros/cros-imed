// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Header-only helpers shared by the open-source engine plugins.  Each plugin is
// compiled as a single translation unit, so everything here is `inline` and
// free of linkable dependencies.

#ifndef CROS_IME_PLUGINS_PLUGIN_COMMON_H_
#define CROS_IME_PLUGINS_PLUGIN_COMMON_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cros_ime/engine_plugin.h"

namespace cros_ime {
namespace plugin {

inline bool IsAsciiUpper(char ch) { return ch >= 'A' && ch <= 'Z'; }
inline bool IsAsciiLower(char ch) { return ch >= 'a' && ch <= 'z'; }
inline bool IsAsciiLetter(char ch) {
  return IsAsciiUpper(ch) || IsAsciiLower(ch);
}
inline char ToAsciiLower(char ch) {
  return IsAsciiUpper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch;
}

inline void AppendUtf8(std::string* out, uint32_t codepoint) {
  if (codepoint < 0x80u) {
    out->push_back(static_cast<char>(codepoint));
    return;
  }
  if (codepoint < 0x800u) {
    out->push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
    out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return;
  }
  if (codepoint < 0x10000u) {
    out->push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
    out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return;
  }
  out->push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
  out->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
  out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
  out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
}

// Strict decoder: returns false for any malformed sequence.
inline bool DecodeUtf8At(std::string_view text,
                         size_t offset,
                         uint32_t* codepoint,
                         size_t* length) {
  if (offset >= text.size()) {
    return false;
  }
  const auto byte_at = [&text](size_t index) {
    return static_cast<uint32_t>(
        static_cast<unsigned char>(text[index]));
  };
  const uint32_t first = byte_at(offset);
  if (first < 0x80u) {
    *codepoint = first;
    *length = 1;
    return true;
  }

  size_t count = 0;
  uint32_t value = 0;
  if ((first & 0xE0u) == 0xC0u) {
    count = 2;
    value = first & 0x1Fu;
  } else if ((first & 0xF0u) == 0xE0u) {
    count = 3;
    value = first & 0x0Fu;
  } else if ((first & 0xF8u) == 0xF0u) {
    count = 4;
    value = first & 0x07u;
  } else {
    return false;
  }
  if (offset + count > text.size()) {
    return false;
  }
  for (size_t i = 1; i < count; ++i) {
    const uint32_t next = byte_at(offset + i);
    if ((next & 0xC0u) != 0x80u) {
      return false;
    }
    value = (value << 6u) | (next & 0x3Fu);
  }
  if ((count == 2 && value < 0x80u) || (count == 3 && value < 0x800u) ||
      (count == 4 && value < 0x10000u) || value > 0x10FFFFu ||
      (value >= 0xD800u && value <= 0xDFFFu)) {
    return false;
  }
  *codepoint = value;
  *length = count;
  return true;
}

inline size_t CodepointCount(std::string_view text) {
  size_t count = 0;
  for (char raw : text) {
    if ((static_cast<unsigned char>(raw) & 0xC0u) != 0x80u) {
      ++count;
    }
  }
  return count;
}

inline void RemoveLastCodepoint(std::string* text) {
  if (text->empty()) {
    return;
  }
  size_t index = text->size() - 1;
  while (index > 0 &&
         (static_cast<unsigned char>((*text)[index]) & 0xC0u) == 0x80u) {
    --index;
  }
  text->erase(index);
}

inline bool LastCodepoint(std::string_view text, uint32_t* codepoint) {
  if (text.empty()) {
    return false;
  }
  size_t start = text.size() - 1;
  while (start > 0 &&
         (static_cast<unsigned char>(text[start]) & 0xC0u) == 0x80u) {
    --start;
  }
  size_t length = 0;
  return DecodeUtf8At(text, start, codepoint, &length);
}

// Splits "a=b,c=d;e=f" pairs.  Malformed pieces are ignored.
inline std::vector<std::pair<std::string, std::string>> ParseConfig(
    const char* config) {
  std::vector<std::pair<std::string, std::string>> pairs;
  if (config == nullptr) {
    return pairs;
  }
  const std::string text(config);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find_first_of(",;", start);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string piece = text.substr(start, end - start);
    const size_t equals = piece.find('=');
    if (equals != std::string::npos && equals > 0) {
      pairs.emplace_back(piece.substr(0, equals),
                         piece.substr(equals + 1));
    }
    start = end + 1;
  }
  return pairs;
}

// Prepares a caller-provided output for a fresh answer.
inline void InitOutput(CrosImeOutput* output) {
  output->struct_size = sizeof(CrosImeOutput);
  output->flags = 0;
  output->commit_utf8 = nullptr;
  output->delete_before_cursor = 0;
  output->delete_after_cursor = 0;
  output->preedit_utf8 = nullptr;
  output->preedit_cursor = 0;
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
  output->auxiliary_utf8 = nullptr;
  output->candidate_count = 0;
  output->candidate_page = 0;
  output->candidate_pages = 0;
  output->candidate_page_size = 0;
  output->candidate_cursor = 0;
  output->candidates = nullptr;
  output->mode_flags = 0;
}

inline bool IsVietnameseVowel(char ch) {
  const char lower = ToAsciiLower(ch);
  return lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' ||
         lower == 'u' || lower == 'y';
}

// Composes one ASCII base letter with a Vietnamese tone/shape.
//   tone: 0 none, 1 acute, 2 grave, 3 hook, 4 tilde, 5 dot
//   shape: 0 none, 1 circumflex, 2 breve, 3 horn (on 'd' means đ)
// Returns 0 when the combination does not exist.
inline uint32_t ComposeVietnamese(char base, int tone, int shape) {
  const bool upper = IsAsciiUpper(base);
  const char lower = ToAsciiLower(base);

  struct Entry {
    char base;
    int tone;
    int shape;
    uint32_t lower_cp;
    uint32_t upper_cp;
  };
  static constexpr Entry kTable[] = {
      {'a', 0, 0, 0x0061u, 0x0041u}, {'a', 2, 0, 0x00E0u, 0x00C0u},
      {'a', 1, 0, 0x00E1u, 0x00C1u}, {'a', 0, 1, 0x00E2u, 0x00C2u},
      {'a', 4, 0, 0x00E3u, 0x00C3u}, {'a', 0, 2, 0x0103u, 0x0102u},
      {'a', 5, 0, 0x1EA1u, 0x1EA0u}, {'a', 3, 0, 0x1EA3u, 0x1EA2u},
      {'a', 1, 1, 0x1EA5u, 0x1EA4u}, {'a', 2, 1, 0x1EA7u, 0x1EA6u},
      {'a', 3, 1, 0x1EA9u, 0x1EA8u}, {'a', 4, 1, 0x1EABu, 0x1EAAu},
      {'a', 5, 1, 0x1EADu, 0x1EACu}, {'a', 1, 2, 0x1EAFu, 0x1EAEu},
      {'a', 2, 2, 0x1EB1u, 0x1EB0u}, {'a', 3, 2, 0x1EB3u, 0x1EB2u},
      {'a', 4, 2, 0x1EB5u, 0x1EB4u}, {'a', 5, 2, 0x1EB7u, 0x1EB6u},
      {'e', 0, 0, 0x0065u, 0x0045u}, {'e', 2, 0, 0x00E8u, 0x00C8u},
      {'e', 1, 0, 0x00E9u, 0x00C9u}, {'e', 0, 1, 0x00EAu, 0x00CAu},
      {'e', 5, 0, 0x1EB9u, 0x1EB8u}, {'e', 3, 0, 0x1EBBu, 0x1EBAu},
      {'e', 4, 0, 0x1EBDu, 0x1EBCu}, {'e', 1, 1, 0x1EBFu, 0x1EBEu},
      {'e', 2, 1, 0x1EC1u, 0x1EC0u}, {'e', 3, 1, 0x1EC3u, 0x1EC2u},
      {'e', 4, 1, 0x1EC5u, 0x1EC4u}, {'e', 5, 1, 0x1EC7u, 0x1EC6u},
      {'i', 0, 0, 0x0069u, 0x0049u}, {'i', 2, 0, 0x00ECu, 0x00CCu},
      {'i', 1, 0, 0x00EDu, 0x00CDu}, {'i', 4, 0, 0x0129u, 0x0128u},
      {'i', 3, 0, 0x1EC9u, 0x1EC8u}, {'i', 5, 0, 0x1ECBu, 0x1ECAu},
      {'o', 0, 0, 0x006Fu, 0x004Fu}, {'o', 2, 0, 0x00F2u, 0x00D2u},
      {'o', 1, 0, 0x00F3u, 0x00D3u}, {'o', 0, 1, 0x00F4u, 0x00D4u},
      {'o', 4, 0, 0x00F5u, 0x00D5u}, {'o', 0, 3, 0x01A1u, 0x01A0u},
      {'o', 5, 0, 0x1ECDu, 0x1ECCu}, {'o', 3, 0, 0x1ECFu, 0x1ECEu},
      {'o', 1, 1, 0x1ED1u, 0x1ED0u}, {'o', 2, 1, 0x1ED3u, 0x1ED2u},
      {'o', 3, 1, 0x1ED5u, 0x1ED4u}, {'o', 4, 1, 0x1ED7u, 0x1ED6u},
      {'o', 5, 1, 0x1ED9u, 0x1ED8u}, {'o', 1, 3, 0x1EDBu, 0x1EDAu},
      {'o', 2, 3, 0x1EDDu, 0x1EDCu}, {'o', 3, 3, 0x1EDFu, 0x1EDEu},
      {'o', 4, 3, 0x1EE1u, 0x1EE0u}, {'o', 5, 3, 0x1EE3u, 0x1EE2u},
      {'u', 0, 0, 0x0075u, 0x0055u}, {'u', 2, 0, 0x00F9u, 0x00D9u},
      {'u', 1, 0, 0x00FAu, 0x00DAu}, {'u', 4, 0, 0x0169u, 0x0168u},
      {'u', 0, 3, 0x01B0u, 0x01AFu}, {'u', 5, 0, 0x1EE5u, 0x1EE4u},
      {'u', 3, 0, 0x1EE7u, 0x1EE6u}, {'u', 1, 3, 0x1EE9u, 0x1EE8u},
      {'u', 2, 3, 0x1EEBu, 0x1EEAu}, {'u', 3, 3, 0x1EEDu, 0x1EECu},
      {'u', 4, 3, 0x1EEFu, 0x1EEEu}, {'u', 5, 3, 0x1EF1u, 0x1EF0u},
      {'y', 0, 0, 0x0079u, 0x0059u}, {'y', 1, 0, 0x00FDu, 0x00DDu},
      {'y', 2, 0, 0x1EF3u, 0x1EF2u}, {'y', 5, 0, 0x1EF5u, 0x1EF4u},
      {'y', 3, 0, 0x1EF7u, 0x1EF6u}, {'y', 4, 0, 0x1EF9u, 0x1EF8u},
      {'d', 0, 3, 0x0111u, 0x0110u},
  };
  for (const Entry& entry : kTable) {
    if (entry.base == lower && entry.tone == tone && entry.shape == shape) {
      return upper ? entry.upper_cp : entry.lower_cp;
    }
  }
  return 0;
}

// True for the ASCII punctuation that commits the current word.
inline bool IsCommitPunctuation(uint32_t codepoint) {
  switch (codepoint) {
    case ' ':
    case ',':
    case '.':
    case '!':
    case '?':
    case ';':
    case ':':
    case '\'':
    case '"':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '-':
    case '_':
    case '/':
    case '\\':
    case '@':
    case '#':
    case '$':
    case '%':
    case '&':
    case '*':
    case '+':
    case '=':
    case '~':
    case '^':
    case '|':
    case '<':
    case '>':
      return true;
    default:
      return false;
  }
}

}  // namespace plugin
}  // namespace cros_ime

#endif  // CROS_IME_PLUGINS_PLUGIN_COMMON_H_
