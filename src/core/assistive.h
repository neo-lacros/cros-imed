// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_ASSISTIVE_H_
#define CROS_IME_CORE_ASSISTIVE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cros_ime {

// Curated, compile-time assistive data: long-press diacritics, emoji and
// kaomoji.  Tables are immutable after construction and shared by all callers.
class AssistiveTables {
 public:
  static const AssistiveTables& Instance();

  // Returns nullptr when `base_codepoint` has no variants.
  const std::vector<std::string>* LongpressVariants(
      uint32_t base_codepoint) const;

  const std::vector<uint32_t>& LongpressBases() const;
  const std::vector<std::string>& Emoji() const;
  const std::vector<std::string>& Kaomoji() const;
  const std::vector<std::pair<std::string, std::vector<std::string>>>&
  EmojiCategories() const;

 private:
  AssistiveTables();

  std::map<uint32_t, std::vector<std::string>> longpress_;
  std::vector<uint32_t> longpress_bases_;
  std::vector<std::string> emoji_;
  std::vector<std::string> kaomoji_;
  std::vector<std::pair<std::string, std::vector<std::string>>> categories_;
};

// A small most-recently-used clipboard history.  Newest item is first.
class ClipboardRing {
 public:
  explicit ClipboardRing(size_t capacity = 20);

  void Push(std::string item);
  const std::vector<std::string>& Items() const;
  bool Get(size_t index, std::string* out) const;
  void Clear();

  // Persists one escaped string per line.  Returns false only when the file
  // cannot be written.
  bool SaveToFile(const std::string& path) const;
  bool LoadFromFile(const std::string& path);

  size_t capacity() const;

 private:
  size_t capacity_;
  std::vector<std::string> items_;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_ASSISTIVE_H_
