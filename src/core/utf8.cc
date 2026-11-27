// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/utf8.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cros_ime {
namespace {

constexpr uint32_t kReplacementCharacter = 0xFFFD;

struct VietnameseEntry {
  uint32_t codepoint;
  char base;
  int tone;
  int shape;
};

// Every precomposed lowercase Vietnamese letter:
//   12 base/shape forms x 6 tones, plus đ.
// The table is sorted by codepoint for deterministic scanning.
constexpr std::array<VietnameseEntry, 73> kVietnameseTable = {{
    {0x0061u, 'a', 0, 0},  {0x0065u, 'e', 0, 0},  {0x0069u, 'i', 0, 0},
    {0x006Fu, 'o', 0, 0},  {0x0075u, 'u', 0, 0},  {0x0079u, 'y', 0, 0},
    {0x00E0u, 'a', 2, 0},  {0x00E1u, 'a', 1, 0},  {0x00E2u, 'a', 0, 1},
    {0x00E3u, 'a', 4, 0},  {0x00E8u, 'e', 2, 0},  {0x00E9u, 'e', 1, 0},
    {0x00EAu, 'e', 0, 1},  {0x00ECu, 'i', 2, 0},  {0x00EDu, 'i', 1, 0},
    {0x00F2u, 'o', 2, 0},  {0x00F3u, 'o', 1, 0},  {0x00F4u, 'o', 0, 1},
    {0x00F5u, 'o', 4, 0},  {0x00F9u, 'u', 2, 0},  {0x00FAu, 'u', 1, 0},
    {0x00FDu, 'y', 1, 0},  {0x0103u, 'a', 0, 2},  {0x0111u, 'd', 0, 3},
    {0x0129u, 'i', 4, 0},  {0x0169u, 'u', 4, 0},  {0x01A1u, 'o', 0, 3},
    {0x01B0u, 'u', 0, 3},  {0x1EA1u, 'a', 5, 0},  {0x1EA3u, 'a', 3, 0},
    {0x1EA5u, 'a', 1, 1},  {0x1EA7u, 'a', 2, 1},  {0x1EA9u, 'a', 3, 1},
    {0x1EABu, 'a', 4, 1},  {0x1EADu, 'a', 5, 1},  {0x1EAFu, 'a', 1, 2},
    {0x1EB1u, 'a', 2, 2},  {0x1EB3u, 'a', 3, 2},  {0x1EB5u, 'a', 4, 2},
    {0x1EB7u, 'a', 5, 2},  {0x1EB9u, 'e', 5, 0},  {0x1EBBu, 'e', 3, 0},
    {0x1EBDu, 'e', 4, 0},  {0x1EBFu, 'e', 1, 1},  {0x1EC1u, 'e', 2, 1},
    {0x1EC3u, 'e', 3, 1},  {0x1EC5u, 'e', 4, 1},  {0x1EC7u, 'e', 5, 1},
    {0x1EC9u, 'i', 3, 0},  {0x1ECBu, 'i', 5, 0},  {0x1ECDu, 'o', 5, 0},
    {0x1ECFu, 'o', 3, 0},  {0x1ED1u, 'o', 1, 1},  {0x1ED3u, 'o', 2, 1},
    {0x1ED5u, 'o', 3, 1},  {0x1ED7u, 'o', 4, 1},  {0x1ED9u, 'o', 5, 1},
    {0x1EDBu, 'o', 1, 3},  {0x1EDDu, 'o', 2, 3},  {0x1EDFu, 'o', 3, 3},
    {0x1EE1u, 'o', 4, 3},  {0x1EE3u, 'o', 5, 3},  {0x1EE5u, 'u', 5, 0},
    {0x1EE7u, 'u', 3, 0},  {0x1EE9u, 'u', 1, 3},  {0x1EEBu, 'u', 2, 3},
    {0x1EEDu, 'u', 3, 3},  {0x1EEFu, 'u', 4, 3},  {0x1EF1u, 'u', 5, 3},
    {0x1EF3u, 'y', 2, 0},  {0x1EF5u, 'y', 5, 0},  {0x1EF7u, 'y', 3, 0},
    {0x1EF9u, 'y', 4, 0},
}};

int ToneFromMark(uint32_t codepoint) {
  switch (codepoint) {
    case 0x0301u:
      return 1;  // acute
    case 0x0300u:
      return 2;  // grave
    case 0x0309u:
      return 3;  // hook above
    case 0x0303u:
      return 4;  // tilde
    case 0x0323u:
      return 5;  // dot below
    default:
      return 0;
  }
}

int ShapeFromMark(uint32_t codepoint) {
  switch (codepoint) {
    case 0x0302u:
      return 1;  // circumflex
    case 0x0306u:
      return 2;  // breve
    case 0x031Bu:
      return 3;  // horn
    default:
      return 0;
  }
}

bool IsVietnameseBase(uint32_t codepoint) {
  switch (codepoint) {
    case 'a':
    case 'e':
    case 'i':
    case 'o':
    case 'u':
    case 'y':
    case 'd':
      return true;
    default:
      return false;
  }
}

}  // namespace

bool DecodeUtf8(std::string_view text,
                size_t offset,
                uint32_t* codepoint,
                size_t* len) {
  if (codepoint == nullptr || len == nullptr) {
    return false;
  }
  if (offset >= text.size()) {
    return false;
  }

  const auto byte_at = [&text](size_t index) -> uint32_t {
    return static_cast<uint32_t>(
        static_cast<unsigned char>(text[index]));
  };

  const uint32_t first = byte_at(offset);
  if (first < 0x80u) {
    *codepoint = first;
    *len = 1;
    return true;
  }

  size_t sequence_len = 0;
  uint32_t value = 0;
  uint32_t minimum = 0;
  if ((first & 0xE0u) == 0xC0u) {
    sequence_len = 2;
    value = first & 0x1Fu;
    minimum = 0x80u;
  } else if ((first & 0xF0u) == 0xE0u) {
    sequence_len = 3;
    value = first & 0x0Fu;
    minimum = 0x800u;
  } else if ((first & 0xF8u) == 0xF0u) {
    sequence_len = 4;
    value = first & 0x07u;
    minimum = 0x10000u;
  } else {
    return false;
  }

  if (offset + sequence_len > text.size()) {
    return false;
  }
  for (size_t i = 1; i < sequence_len; ++i) {
    const uint32_t continuation = byte_at(offset + i);
    if ((continuation & 0xC0u) != 0x80u) {
      return false;
    }
    value = (value << 6u) | (continuation & 0x3Fu);
  }

  if (value < minimum) {
    return false;  // Overlong encoding.
  }
  if (value > 0x10FFFFu) {
    return false;  // Beyond Unicode.
  }
  if (value >= 0xD800u && value <= 0xDFFFu) {
    return false;  // Surrogate half.
  }

  *codepoint = value;
  *len = sequence_len;
  return true;
}

std::string EncodeUtf8(uint32_t codepoint) {
  if (codepoint > 0x10FFFFu ||
      (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
    codepoint = kReplacementCharacter;
  }

  std::string out;
  if (codepoint < 0x80u) {
    out.push_back(static_cast<char>(codepoint));
    return out;
  }
  if (codepoint < 0x800u) {
    out.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return out;
  }
  if (codepoint < 0x10000u) {
    out.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return out;
  }
  out.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
  out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
  out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
  out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  return out;
}

size_t Utf8Length(std::string_view text) {
  size_t length = 0;
  size_t offset = 0;
  while (offset < text.size()) {
    uint32_t codepoint = 0;
    size_t len = 0;
    if (DecodeUtf8(text, offset, &codepoint, &len)) {
      offset += len;
    } else {
      offset += 1;
    }
    ++length;
  }
  return length;
}

std::string Utf8Substr(std::string_view text,
                       size_t cp_start,
                       size_t cp_len) {
  if (cp_len == 0) {
    return std::string();
  }

  size_t offset = 0;
  size_t cp_index = 0;
  while (offset < text.size() && cp_index < cp_start) {
    uint32_t codepoint = 0;
    size_t len = 0;
    if (DecodeUtf8(text, offset, &codepoint, &len)) {
      offset += len;
    } else {
      offset += 1;
    }
    ++cp_index;
  }
  if (cp_index < cp_start) {
    return std::string();
  }

  const size_t start_byte = offset;
  size_t taken = 0;
  while (offset < text.size() && taken < cp_len) {
    uint32_t codepoint = 0;
    size_t len = 0;
    if (DecodeUtf8(text, offset, &codepoint, &len)) {
      offset += len;
    } else {
      offset += 1;
    }
    ++taken;
  }
  return std::string(text.substr(start_byte, offset - start_byte));
}

std::vector<uint32_t> Utf8ToCodepoints(std::string_view text) {
  std::vector<uint32_t> codepoints;
  codepoints.reserve(text.size());
  size_t offset = 0;
  while (offset < text.size()) {
    uint32_t codepoint = 0;
    size_t len = 0;
    if (DecodeUtf8(text, offset, &codepoint, &len)) {
      codepoints.push_back(codepoint);
      offset += len;
    } else {
      codepoints.push_back(kReplacementCharacter);
      offset += 1;
    }
  }
  return codepoints;
}

std::string CodepointsToUtf8(const std::vector<uint32_t>& codepoints) {
  std::string out;
  out.reserve(codepoints.size());
  for (uint32_t codepoint : codepoints) {
    out += EncodeUtf8(codepoint);
  }
  return out;
}

std::string AsciiToLower(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return out;
}

std::string AsciiToUpper(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  return out;
}

bool IsCombiningMark(uint32_t codepoint) {
  return (codepoint >= 0x0300u && codepoint <= 0x036Fu) ||
         (codepoint >= 0x1AB0u && codepoint <= 0x1AFFu) ||
         (codepoint >= 0x1DC0u && codepoint <= 0x1DFFu) ||
         (codepoint >= 0x20D0u && codepoint <= 0x20FFu) ||
         (codepoint >= 0xFE20u && codepoint <= 0xFE2Fu);
}

uint32_t ComposeVietnamese(uint32_t base, int tone, int shape) {
  if (tone < 0 || tone > 5 || shape < 0 || shape > 3) {
    return 0;
  }
  if (base == 'd' && tone == 0 && shape == 0) {
    return 'd';
  }
  for (const VietnameseEntry& entry : kVietnameseTable) {
    if (entry.base == static_cast<char>(base) && entry.tone == tone &&
        entry.shape == shape) {
      return entry.codepoint;
    }
  }
  return 0;
}

bool DecomposeVietnamese(uint32_t codepoint, uint32_t* base, int* tone,
                         int* shape) {
  if (base == nullptr || tone == nullptr || shape == nullptr) {
    return false;
  }
  if (codepoint == 'd') {
    *base = 'd';
    *tone = 0;
    *shape = 0;
    return true;
  }
  for (const VietnameseEntry& entry : kVietnameseTable) {
    if (entry.codepoint != codepoint) {
      continue;
    }
    *base = static_cast<uint32_t>(entry.base);
    *tone = entry.tone;
    *shape = entry.shape;
    return true;
  }
  return false;
}

std::string NormalizeVietnamese(std::string_view text) {
  const std::vector<uint32_t> codepoints = Utf8ToCodepoints(text);
  std::string out;
  out.reserve(text.size());

  size_t index = 0;
  while (index < codepoints.size()) {
    const uint32_t current = codepoints[index];
    uint32_t base = 0;
    int tone = 0;
    int shape = 0;
    const bool is_precomposed =
        DecomposeVietnamese(current, &base, &tone, &shape);

    if (!is_precomposed && !IsVietnameseBase(current)) {
      out += EncodeUtf8(current);
      ++index;
      continue;
    }
    if (!is_precomposed) {
      base = current;
    }

    ++index;
    std::vector<uint32_t> marks;
    bool has_unsupported_mark = false;
    while (index < codepoints.size() &&
           IsCombiningMark(codepoints[index])) {
      const uint32_t mark = codepoints[index];
      const int mark_tone = ToneFromMark(mark);
      const int mark_shape = ShapeFromMark(mark);
      if (mark_tone != 0) {
        tone = mark_tone;
      } else if (mark_shape != 0) {
        shape = mark_shape;
      } else {
        has_unsupported_mark = true;
      }
      marks.push_back(mark);
      ++index;
    }

    const uint32_t composed = ComposeVietnamese(base, tone, shape);
    if (!has_unsupported_mark && composed != 0) {
      out += EncodeUtf8(composed);
      continue;
    }

    // Unsupported combination: preserve the original input faithfully.
    out += EncodeUtf8(current);
    for (uint32_t mark : marks) {
      out += EncodeUtf8(mark);
    }
  }

  return out;
}

}  // namespace cros_ime
