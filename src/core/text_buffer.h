// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_TEXT_BUFFER_H_
#define CROS_IME_CORE_TEXT_BUFFER_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace cros_ime {

// UTF-8 aware editable text with a cursor and an anchor.
//
// All positions are Unicode codepoint indices, never byte offsets.  Every
// mutation keeps the buffer valid UTF-8: invalid input bytes are replaced by
// U+FFFD at the boundary.
class TextBuffer {
 public:
  TextBuffer();
  explicit TextBuffer(std::string_view text);

  void Set(std::string_view text, size_t cursor, size_t anchor);

  size_t Length() const;
  size_t Cursor() const;
  size_t Anchor() const;

  void Insert(std::string_view text);
  void EraseBefore(size_t count);
  void EraseAfter(size_t count);
  void MoveCursor(long long delta);
  void Select(size_t start, size_t end);

  std::string SelectedText() const;
  std::string BeforeCursor(size_t max_codepoints) const;
  std::string AfterCursor(size_t max_codepoints) const;

  void Clear();
  std::string_view View() const;

 private:
  size_t ByteOffset(size_t codepoint_index) const;
  size_t Clamp(size_t codepoint_index) const;

  std::string text_;
  size_t cursor_ = 0;
  size_t anchor_ = 0;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_TEXT_BUFFER_H_
