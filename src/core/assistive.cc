// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/assistive.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/utf8.h"

namespace cros_ime {
namespace {

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

std::string EscapeLine(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (char raw : text) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          static const char kHex[] = "0123456789ABCDEF";
          out += "\\u00";
          out.push_back(kHex[(ch >> 4) & 0x0F]);
          out.push_back(kHex[ch & 0x0F]);
        } else {
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

bool UnescapeLine(const std::string& line, std::string* out) {
  if (line.size() < 2 || line.front() != '"' || line.back() != '"') {
    return false;
  }
  out->clear();
  size_t i = 1;
  const size_t end = line.size() - 1;
  while (i < end) {
    const char ch = line[i];
    if (ch != '\\') {
      out->push_back(ch);
      ++i;
      continue;
    }
    ++i;
    if (i >= end) {
      return false;
    }
    const char escaped = line[i];
    switch (escaped) {
      case '\\':
        out->push_back('\\');
        break;
      case '"':
        out->push_back('"');
        break;
      case 'n':
        out->push_back('\n');
        break;
      case 'r':
        out->push_back('\r');
        break;
      case 't':
        out->push_back('\t');
        break;
      case 'u': {
        if (i + 4 >= end) {
          return false;
        }
        uint32_t value = 0;
        for (size_t k = 1; k <= 4; ++k) {
          if (!IsHexDigit(line[i + k])) {
            return false;
          }
          value = value * 16 + HexValue(line[i + k]);
        }
        i += 4;
        *out += EncodeUtf8(value);
        break;
      }
      default:
        out->push_back(escaped);
        break;
    }
    ++i;
  }
  return true;
}

}  // namespace

AssistiveTables::AssistiveTables() {
  // Canonical ChromeOS Vietnamese long-press order.  The first entry is the
  // most common variant, not the base letter itself.
  longpress_ = {
      {0x0061u,
       {"à", "á", "ả", "ã", "ạ", "ă", "ằ", "ắ", "ẳ", "ẵ", "ặ", "â",
        "ầ", "ấ", "ẩ", "ẫ", "ậ"}},
      {0x0103u, {"ằ", "ắ", "ẳ", "ẵ", "ặ"}},
      {0x00E2u, {"ầ", "ấ", "ẩ", "ẫ", "ậ"}},
      {0x0065u, {"è", "é", "ẻ", "ẽ", "ẹ", "ê", "ề", "ế", "ể", "ễ", "ệ"}},
      {0x00EAu, {"ề", "ế", "ể", "ễ", "ệ"}},
      {0x0069u, {"ì", "í", "ỉ", "ĩ", "ị"}},
      {0x006Fu,
       {"ò", "ó", "ỏ", "õ", "ọ", "ô", "ồ", "ố", "ổ", "ỗ", "ộ", "ơ",
        "ờ", "ớ", "ở", "ỡ", "ợ"}},
      {0x00F4u, {"ồ", "ố", "ổ", "ỗ", "ộ"}},
      {0x01A1u, {"ờ", "ớ", "ở", "ỡ", "ợ"}},
      {0x0075u,
       {"ù", "ú", "ủ", "ũ", "ụ", "ư", "ừ", "ứ", "ử", "ữ", "ự"}},
      {0x01B0u, {"ừ", "ứ", "ử", "ữ", "ự"}},
      {0x0079u, {"ỳ", "ý", "ỷ", "ỹ", "ỵ"}},
      {0x0064u, {"đ"}},
      {0x0041u,
       {"À", "Á", "Ả", "Ã", "Ạ", "Ă", "Ằ", "Ắ", "Ẳ", "Ẵ", "Ặ", "Â",
        "Ầ", "Ấ", "Ẩ", "Ẫ", "Ậ"}},
      {0x0102u, {"Ằ", "Ắ", "Ẳ", "Ẵ", "Ặ"}},
      {0x00C2u, {"Ầ", "Ấ", "Ẩ", "Ẫ", "Ậ"}},
      {0x0045u, {"È", "É", "Ẻ", "Ẽ", "Ẹ", "Ê", "Ề", "Ế", "Ể", "Ễ", "Ệ"}},
      {0x00CAu, {"Ề", "Ế", "Ể", "Ễ", "Ệ"}},
      {0x0049u, {"Ì", "Í", "Ỉ", "Ĩ", "Ị"}},
      {0x004Fu,
       {"Ò", "Ó", "Ỏ", "Õ", "Ọ", "Ô", "Ồ", "Ố", "Ổ", "Ỗ", "Ộ", "Ơ",
        "Ờ", "Ớ", "Ở", "Ỡ", "Ợ"}},
      {0x00D4u, {"Ồ", "Ố", "Ổ", "Ỗ", "Ộ"}},
      {0x01A0u, {"Ờ", "Ớ", "Ở", "Ỡ", "Ợ"}},
      {0x0055u,
       {"Ù", "Ú", "Ủ", "Ũ", "Ụ", "Ư", "Ừ", "Ứ", "Ử", "Ữ", "Ự"}},
      {0x01AFu, {"Ừ", "Ứ", "Ử", "Ữ", "Ự"}},
      {0x0059u, {"Ỳ", "Ý", "Ỷ", "Ỹ", "Ỵ"}},
      {0x0044u, {"Đ"}},
  };
  for (const auto& entry : longpress_) {
    longpress_bases_.push_back(entry.first);
  }

  categories_ = {
      {"Smileys",
       {"😀", "😂", "🥰", "😍", "😊", "😎", "🤔", "😭", "😅", "🥳"}},
      {"People",
       {"👍", "👎", "👏", "🙏", "💪", "🙌", "🤝", "👋", "🧑", "💃"}},
      {"Nature",
       {"🌸", "🌹", "🌻", "🌈", "🌙", "⭐", "🔥", "❄️", "🌊", "🍀"}},
      {"Food",
       {"🍎", "🍕", "🍔", "🍣", "🍜", "🍰", "☕", "🍺", "🍩", "🍓"}},
      {"Activity",
       {"🎉", "🎊", "🎈", "🎮", "⚽", "🏀", "🎸", "🎨", "🎯", "🏆"}},
      {"Travel",
       {"🚀", "✈️", "🚗", "🚲", "🚂", "🗺️", "🏖️", "🏔️", "🗼", "🚁"}},
      {"Objects",
       {"💡", "📱", "💻", "⌚", "📷", "🔑", "💰", "📚", "✏️", "🎁"}},
      {"Symbols",
       {"✅", "❌", "⚠️", "❤️", "💔", "✨", "❓", "❗", "➕", "➖"}},
      {"Flags",
       {"🇻🇳", "🇺🇸", "🇯🇵", "🇰🇷", "🇨🇳", "🇫🇷", "🇩🇪", "🇬🇧", "🇨🇦",
        "🇦🇺"}},
  };
  for (const auto& category : categories_) {
    emoji_.insert(emoji_.end(), category.second.begin(), category.second.end());
  }

  kaomoji_ = {
      R"KO((＾▽＾))KO",
      R"KO((￣▽￣))KO",
      R"KO((´･ω･`))KO",
      R"KO(¯\_(ツ)_/¯)KO",
      R"KO((╯°□°）╯︵ ┻━┻)KO",
      R"KO((・∀・))KO",
      R"KO((๑•̀ㅂ•́)و✧)KO",
      R"KO((｡♥‿♥｡))KO",
      R"KO((T_T))KO",
      R"KO((>_<))KO",
      R"KO((¬‿¬))KO",
      R"KO((╥﹏╥))KO",
      R"KO((＾ω＾))KO",
      R"KO((ʘ‿ʘ))KO",
      R"KO(ಠ_ಠ)KO",
      R"KO((≧▽≦))KO",
      R"KO((๑>ᴗ<๑))KO",
      R"KO((´；ω；`))KO",
      R"KO((ﾉ◕ヮ◕)ﾉ*:･ﾟ✧)KO",
      R"KO((ง'̀-'́)ง)KO",
      R"KO(ヽ(´ー｀)ノ)KO",
      R"KO((°ロ°))KO",
      R"KO((☞ﾟヮﾟ)☞)KO",
      R"KO(☜(ﾟヮﾟ☜))KO",
      R"KO(( ͡° ͜ʖ ͡°))KO",
      R"KO(¯\_(⊙_ʖ⊙)_/¯)KO",
  };

  // Fail fast if an edit accidentally empties a curated table.
  assert(!longpress_.empty());
  assert(!longpress_bases_.empty());
  assert(!emoji_.empty());
  assert(!kaomoji_.empty());
  assert(!categories_.empty());
  assert(longpress_.size() == longpress_bases_.size());
  for (const auto& entry : longpress_) {
    assert(!entry.second.empty());
  }
  for (const auto& category : categories_) {
    assert(!category.first.empty());
    assert(!category.second.empty());
  }
}

const AssistiveTables& AssistiveTables::Instance() {
  static const AssistiveTables* instance = new AssistiveTables();
  return *instance;
}

const std::vector<std::string>* AssistiveTables::LongpressVariants(
    uint32_t base_codepoint) const {
  const auto it = longpress_.find(base_codepoint);
  if (it == longpress_.end()) {
    return nullptr;
  }
  return &it->second;
}

const std::vector<uint32_t>& AssistiveTables::LongpressBases() const {
  return longpress_bases_;
}

const std::vector<std::string>& AssistiveTables::Emoji() const {
  return emoji_;
}

const std::vector<std::string>& AssistiveTables::Kaomoji() const {
  return kaomoji_;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>&
AssistiveTables::EmojiCategories() const {
  return categories_;
}

ClipboardRing::ClipboardRing(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

void ClipboardRing::Push(std::string item) {
  if (item.empty()) {
    return;
  }
  auto& items = items_;
  items.erase(std::remove(items.begin(), items.end(), item), items.end());
  items.insert(items.begin(), std::move(item));
  if (items.size() > capacity_) {
    items.resize(capacity_);
  }
}

const std::vector<std::string>& ClipboardRing::Items() const {
  return items_;
}

bool ClipboardRing::Get(size_t index, std::string* out) const {
  if (out == nullptr || index >= items_.size()) {
    return false;
  }
  *out = items_[index];
  return true;
}

void ClipboardRing::Clear() {
  items_.clear();
}

bool ClipboardRing::SaveToFile(const std::string& path) const {
  std::ofstream stream(path, std::ios::out | std::ios::trunc);
  if (!stream) {
    return false;
  }
  for (const std::string& item : items_) {
    stream << EscapeLine(item) << '\n';
  }
  stream.flush();
  return static_cast<bool>(stream);
}

bool ClipboardRing::LoadFromFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    std::string decoded;
    if (!UnescapeLine(line, &decoded)) {
      continue;
    }
    lines.push_back(std::move(decoded));
  }

  items_.clear();
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->empty()) {
      continue;
    }
    items_.erase(std::remove(items_.begin(), items_.end(), *it),
                 items_.end());
    items_.insert(items_.begin(), *it);
    if (items_.size() > capacity_) {
      items_.resize(capacity_);
    }
  }
  return true;
}

size_t ClipboardRing::capacity() const {
  return capacity_;
}

}  // namespace cros_ime
