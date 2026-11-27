// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/compose.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/utf8.h"

namespace cros_ime {
namespace {

constexpr size_t kMaxSequenceKeys = 8;
constexpr size_t kMaxTrieNodes = 200000;

std::string Trim(std::string_view text) {
  size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

bool IsHexDigit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  return ch - 'A' + 10;
}

uint32_t KeysymNameToCodepoint(const std::string& name) {
  if (name.empty()) {
    return 0;
  }
  if (name.size() == 1) {
    return static_cast<uint32_t>(static_cast<unsigned char>(name[0]));
  }
  if (name[0] == 'U' && name.size() >= 2) {
    int value = 0;
    bool all_hex = true;
    for (size_t i = 1; i < name.size(); ++i) {
      if (!IsHexDigit(name[i])) {
        all_hex = false;
        break;
      }
      value = value * 16 + HexValue(name[i]);
    }
    if (all_hex && value > 0 && value <= 0x10FFFF) {
      return static_cast<uint32_t>(value);
    }
  }

  static const std::map<std::string, uint32_t> kNames = {
      {"space", 0x20u},        {"exclam", 0x21u},
      {"quotedbl", 0x22u},     {"numbersign", 0x23u},
      {"dollar", 0x24u},       {"percent", 0x25u},
      {"ampersand", 0x26u},    {"apostrophe", 0x27u},
      {"parenleft", 0x28u},    {"parenright", 0x29u},
      {"asterisk", 0x2Au},     {"plus", 0x2Bu},
      {"comma", 0x2Cu},        {"minus", 0x2Du},
      {"period", 0x2Eu},       {"slash", 0x2Fu},
      {"colon", 0x3Au},        {"semicolon", 0x3Bu},
      {"less", 0x3Cu},         {"equal", 0x3Du},
      {"greater", 0x3Eu},      {"question", 0x3Fu},
      {"at", 0x40u},           {"bracketleft", 0x5Bu},
      {"backslash", 0x5Cu},    {"bracketright", 0x5Du},
      {"asciicircum", 0x5Eu},  {"underscore", 0x5Fu},
      {"grave", 0x60u},        {"braceleft", 0x7Bu},
      {"bar", 0x7Cu},          {"braceright", 0x7Du},
      {"asciitilde", 0x7Eu},   {"nobreakspace", 0xA0u},
      {"multiply", 0xD7u},     {"division", 0xF7u},
  };
  const auto it = kNames.find(name);
  if (it != kNames.end()) {
    return it->second;
  }
  return 0;
}

std::string CodepointKeysymName(uint32_t codepoint) {
  if (codepoint < 0x80u && codepoint != 0x20u) {
    return std::string(1, static_cast<char>(codepoint));
  }
  static const std::map<uint32_t, std::string> kNames = {
      {0x20u, "space"},        {0x27u, "apostrophe"},
      {0x22u, "quotedbl"},     {0x60u, "grave"},
      {0x5Cu, "backslash"},    {0x5Eu, "asciicircum"},
      {0x5Fu, "underscore"},   {0x7Cu, "bar"},
      {0x7Eu, "asciitilde"},   {0x3Cu, "less"},
      {0x3Eu, "greater"},      {0x3Au, "colon"},
      {0x3Bu, "semicolon"},    {0x2Du, "minus"},
      {0x2Eu, "period"},       {0x2Cu, "comma"},
      {0x2Fu, "slash"},        {0x3Du, "equal"},
      {0x2Bu, "plus"},         {0x28u, "parenleft"},
      {0x29u, "parenright"},   {0x5Bu, "bracketleft"},
      {0x5Du, "bracketright"},
  };
  const auto it = kNames.find(codepoint);
  if (it != kNames.end()) {
    return it->second;
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "U%04X",
                static_cast<unsigned int>(codepoint));
  return std::string(buffer);
}

bool ParseQuotedText(const std::string& input, std::string* out) {
  if (input.empty() || input[0] != '"') {
    return false;
  }
  out->clear();
  size_t i = 1;
  while (i < input.size()) {
    const char ch = input[i];
    if (ch == '"') {
      return true;
    }
    if (ch != '\\') {
      out->push_back(ch);
      ++i;
      continue;
    }
    ++i;
    if (i >= input.size()) {
      return false;
    }
    const char escaped = input[i];
    switch (escaped) {
      case 'n':
        out->push_back('\n');
        break;
      case 'r':
        out->push_back('\r');
        break;
      case 't':
        out->push_back('\t');
        break;
      case 'b':
        out->push_back('\b');
        break;
      case 'f':
        out->push_back('\f');
        break;
      case 'v':
        out->push_back('\v');
        break;
      case '\\':
        out->push_back('\\');
        break;
      case '"':
        out->push_back('"');
        break;
      case 'x': {
        if (i + 2 >= input.size() || !IsHexDigit(input[i + 1]) ||
            !IsHexDigit(input[i + 2])) {
          return false;
        }
        const int value =
            HexValue(input[i + 1]) * 16 + HexValue(input[i + 2]);
        out->push_back(static_cast<char>(value));
        i += 2;
        break;
      }
      default:
        out->push_back(escaped);
        break;
    }
    ++i;
  }
  return false;
}

}  // namespace

XkbComposeEngine::XkbComposeEngine() = default;

XkbComposeEngine::~XkbComposeEngine() = default;

XkbComposeEngine::XkbComposeEngine(XkbComposeEngine&&) noexcept = default;

XkbComposeEngine& XkbComposeEngine::operator=(XkbComposeEngine&&) noexcept =
    default;

XkbComposeEngine XkbComposeEngine::LoadDefault() {
  const char* override_path = std::getenv("CROS_IME_COMPOSE_FILE");
  if (override_path != nullptr && override_path[0] != '\0') {
    return LoadFromFile(override_path);
  }
  return LoadFromFile("/usr/share/X11/locale/en_US.UTF-8/Compose");
}

XkbComposeEngine XkbComposeEngine::LoadFromFile(const std::string& path) {
  XkbComposeEngine engine;
  engine.root_ = std::make_unique<Node>();
  engine.node_count_ = 1;

  if (engine.ParseFile(path)) {
    engine.loaded_from_file_ = true;
    engine.source_path_ = path;
  }

  engine.MergeBuiltinFallback();
  engine.Reset();
  return engine;
}

void XkbComposeEngine::Reset() {
  current_ = nullptr;
  current_keys_.clear();
}

ComposeResult XkbComposeEngine::Feed(const std::string& keysym_name) {
  ComposeResult result;
  if (keysym_name.empty() || root_ == nullptr) {
    Reset();
    return result;
  }
  if (current_ == nullptr) {
    current_ = root_.get();
  }

  const auto it = current_->children.find(keysym_name);
  if (it == current_->children.end()) {
    Reset();
    return result;
  }

  current_keys_.push_back(keysym_name);
  current_ = it->second.get();

  for (size_t i = 0; i < current_keys_.size(); ++i) {
    if (i != 0) {
      result.sequence.push_back(' ');
    }
    result.sequence += current_keys_[i];
  }

  if (current_->has_text) {
    result.matched = true;
    result.text = current_->text;
    Reset();
    return result;
  }

  if (!current_->children.empty()) {
    result.pending = true;
    return result;
  }

  Reset();
  return result;
}

ComposeResult XkbComposeEngine::FeedCodepoint(uint32_t codepoint) {
  return Feed(KeysymNameForCodepoint(codepoint));
}

size_t XkbComposeEngine::TableSize() const {
  return terminal_count_;
}

bool XkbComposeEngine::loaded_from_file() const {
  return loaded_from_file_;
}

const std::string& XkbComposeEngine::source_path() const {
  return source_path_;
}

bool XkbComposeEngine::ParseFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    return false;
  }

  bool inserted_any = false;
  std::string line;
  while (std::getline(stream, line)) {
    if (ParseLine(line)) {
      inserted_any = true;
    }
  }
  return inserted_any;
}

bool XkbComposeEngine::ParseLine(const std::string& line) {
  if (line.empty()) {
    return false;
  }

  const size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return false;
  }

  const std::string sequence_part = line.substr(0, colon);
  const std::string first = Trim(sequence_part);
  if (first.empty() || first[0] == '#') {
    return false;
  }
  if (first.rfind("include", 0) == 0) {
    return false;  // Include directives are intentionally ignored.
  }

  std::vector<std::string> keys;
  size_t cursor = 0;
  while (cursor < sequence_part.size()) {
    while (cursor < sequence_part.size() &&
           std::isspace(
               static_cast<unsigned char>(sequence_part[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= sequence_part.size()) {
      break;
    }
    if (sequence_part[cursor] != '<') {
      return false;
    }
    const size_t close = sequence_part.find('>', cursor + 1);
    if (close == std::string::npos) {
      return false;
    }
    keys.push_back(sequence_part.substr(cursor + 1, close - cursor - 1));
    cursor = close + 1;
  }

  if (keys.empty() || keys.size() > kMaxSequenceKeys) {
    return false;
  }

  const std::string result_part = Trim(line.substr(colon + 1));
  if (result_part.empty()) {
    return false;
  }

  std::string text;
  if (result_part[0] == '"') {
    if (!ParseQuotedText(result_part, &text)) {
      return false;
    }
  } else {
    std::string name;
    if (result_part[0] == '<') {
      const size_t close = result_part.find('>');
      if (close == std::string::npos) {
        return false;
      }
      name = result_part.substr(1, close - 1);
    } else {
      const size_t end = result_part.find_first_of(" \t");
      name = result_part.substr(0, end);
    }
    const uint32_t codepoint = KeysymNameToCodepoint(name);
    if (codepoint == 0) {
      return false;
    }
    text = EncodeUtf8(codepoint);
    if (codepoint_to_name_.find(codepoint) == codepoint_to_name_.end()) {
      codepoint_to_name_[codepoint] = name;
    }
  }

  for (const std::string& key : keys) {
    const uint32_t codepoint = KeysymNameToCodepoint(key);
    if (codepoint != 0 &&
        codepoint_to_name_.find(codepoint) == codepoint_to_name_.end()) {
      codepoint_to_name_[codepoint] = key;
    }
  }

  InsertSequence(keys, text, /*only_if_absent=*/false);
  return true;
}

void XkbComposeEngine::InsertSequence(const std::vector<std::string>& keys,
                                      const std::string& text,
                                      bool only_if_absent) {
  if (root_ == nullptr) {
    root_ = std::make_unique<Node>();
    node_count_ = 1;
  }

  Node* node = root_.get();
  for (const std::string& key : keys) {
    auto it = node->children.find(key);
    if (it == node->children.end()) {
      if (node_count_ >= kMaxTrieNodes) {
        return;
      }
      auto child = std::make_unique<Node>();
      ++node_count_;
      it = node->children.emplace(key, std::move(child)).first;
    }
    node = it->second.get();
  }

  if (only_if_absent && node->has_text) {
    return;
  }
  if (!node->has_text) {
    node->has_text = true;
    ++terminal_count_;
  }
  node->text = text;
}

void XkbComposeEngine::MergeBuiltinFallback() {
  const auto add = [this](std::vector<std::string> keys,
                          const std::string& text) {
    InsertSequence(keys, text, /*only_if_absent=*/true);
  };

  struct BasicEntry {
    const char* dead;
    const char* base;
    const char* composed;
  };
  static const BasicEntry kBasic[] = {
      {"dead_grave", "a", "à"},       {"dead_grave", "e", "è"},
      {"dead_grave", "i", "ì"},       {"dead_grave", "o", "ò"},
      {"dead_grave", "u", "ù"},       {"dead_grave", "A", "À"},
      {"dead_grave", "E", "È"},       {"dead_grave", "I", "Ì"},
      {"dead_grave", "O", "Ò"},       {"dead_grave", "U", "Ù"},
      {"dead_acute", "a", "á"},       {"dead_acute", "e", "é"},
      {"dead_acute", "i", "í"},       {"dead_acute", "o", "ó"},
      {"dead_acute", "u", "ú"},       {"dead_acute", "y", "ý"},
      {"dead_acute", "A", "Á"},       {"dead_acute", "E", "É"},
      {"dead_acute", "I", "Í"},       {"dead_acute", "O", "Ó"},
      {"dead_acute", "U", "Ú"},       {"dead_acute", "Y", "Ý"},
      {"dead_circumflex", "a", "â"},  {"dead_circumflex", "e", "ê"},
      {"dead_circumflex", "i", "î"},  {"dead_circumflex", "o", "ô"},
      {"dead_circumflex", "u", "û"},  {"dead_circumflex", "A", "Â"},
      {"dead_circumflex", "E", "Ê"},  {"dead_circumflex", "I", "Î"},
      {"dead_circumflex", "O", "Ô"},  {"dead_circumflex", "U", "Û"},
      {"dead_tilde", "a", "ã"},       {"dead_tilde", "n", "ñ"},
      {"dead_tilde", "o", "õ"},       {"dead_tilde", "A", "Ã"},
      {"dead_tilde", "N", "Ñ"},       {"dead_tilde", "O", "Õ"},
      {"dead_diaeresis", "a", "ä"},   {"dead_diaeresis", "e", "ë"},
      {"dead_diaeresis", "i", "ï"},   {"dead_diaeresis", "o", "ö"},
      {"dead_diaeresis", "u", "ü"},   {"dead_diaeresis", "y", "ÿ"},
      {"dead_diaeresis", "A", "Ä"},   {"dead_diaeresis", "E", "Ë"},
      {"dead_diaeresis", "I", "Ï"},   {"dead_diaeresis", "O", "Ö"},
      {"dead_diaeresis", "U", "Ü"},   {"dead_breve", "a", "ă"},
      {"dead_breve", "A", "Ă"},       {"dead_horn", "o", "ơ"},
      {"dead_horn", "u", "ư"},        {"dead_horn", "O", "Ơ"},
      {"dead_horn", "U", "Ư"},        {"dead_stroke", "d", "đ"},
      {"dead_stroke", "D", "Đ"},
  };
  for (const BasicEntry& entry : kBasic) {
    add({entry.dead, entry.base}, entry.composed);
  }

  // Vietnamese tone + shape sequences for both lower and upper case.
  struct ShapeEntry {
    const char* dead_shape;
    int shape;
  };
  static const ShapeEntry kShapes[] = {
      {"dead_circumflex", 1}, {"dead_breve", 2}, {"dead_horn", 3},
  };
  struct ToneEntry {
    const char* dead_tone;
    int tone;
  };
  static const ToneEntry kTones[] = {
      {"dead_acute", 1}, {"dead_grave", 2}, {"dead_tilde", 4},
  };
  static const char* const kBases[] = {"a", "e", "i", "o", "u", "y"};
  for (const ShapeEntry& shape : kShapes) {
    for (const ToneEntry& tone : kTones) {
      for (const char* base : kBases) {
        const uint32_t codepoint =
            ComposeVietnamese(static_cast<uint32_t>(base[0]), tone.tone,
                              shape.shape);
        if (codepoint == 0) {
          continue;
        }
        add({tone.dead_tone, shape.dead_shape, base}, EncodeUtf8(codepoint));
      }
    }
  }

  // Common multi-key aliases for the Vietnamese stroked d.
  add({"Multi_key", "dead_stroke", "d"}, "đ");
  add({"Multi_key", "dead_stroke", "D"}, "Đ");
  add({"Multi_key", "d"}, "đ");
}

std::string XkbComposeEngine::KeysymNameForCodepoint(
    uint32_t codepoint) const {
  const auto it = codepoint_to_name_.find(codepoint);
  if (it != codepoint_to_name_.end()) {
    return it->second;
  }
  return CodepointKeysymName(codepoint);
}

}  // namespace cros_ime
