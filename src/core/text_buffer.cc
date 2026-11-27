// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/text_buffer.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/utf8.h"

namespace cros_ime {
namespace {

std::string Sanitize(std::string_view text) {
  return CodepointsToUtf8(Utf8ToCodepoints(text));
}

size_t CountCodepoints(std::string_view text) {
  return Utf8Length(text);
}

}  // namespace

TextBuffer::TextBuffer() = default;

TextBuffer::TextBuffer(std::string_view text) {
  Set(text, CountCodepoints(text), CountCodepoints(text));
}

void TextBuffer::Set(std::string_view text, size_t cursor, size_t anchor) {
  text_ = Sanitize(text);
  const size_t length = CountCodepoints(text_);
  cursor_ = std::min(cursor, length);
  anchor_ = std::min(anchor, length);
}

size_t TextBuffer::Length() const {
  return CountCodepoints(text_);
}

size_t TextBuffer::Cursor() const {
  return cursor_;
}

size_t TextBuffer::Anchor() const {
  return anchor_;
}

void TextBuffer::Insert(std::string_view text) {
  const std::string sanitized = Sanitize(text);
  if (sanitized.empty()) {
    return;
  }

  if (cursor_ != anchor_) {
    const size_t start = std::min(cursor_, anchor_);
    const size_t end = std::max(cursor_, anchor_);
    const size_t start_byte = ByteOffset(start);
    const size_t end_byte = ByteOffset(end);
    text_.erase(start_byte, end_byte - start_byte);
    cursor_ = start;
    anchor_ = start;
  }

  const size_t insert_at = ByteOffset(cursor_);
  text_.insert(insert_at, sanitized);
  cursor_ += CountCodepoints(sanitized);
  anchor_ = cursor_;
}

void TextBuffer::EraseBefore(size_t count) {
  if (count == 0 || text_.empty()) {
    return;
  }
  const size_t start = cursor_ > count ? cursor_ - count : 0;
  const size_t start_byte = ByteOffset(start);
  const size_t cursor_byte = ByteOffset(cursor_);
  text_.erase(start_byte, cursor_byte - start_byte);
  cursor_ = start;
  anchor_ = Clamp(anchor_);
}

void TextBuffer::EraseAfter(size_t count) {
  if (count == 0) {
    return;
  }
  const size_t end = std::min(cursor_ + count, Length());
  const size_t cursor_byte = ByteOffset(cursor_);
  const size_t end_byte = ByteOffset(end);
  text_.erase(cursor_byte, end_byte - cursor_byte);
  anchor_ = Clamp(anchor_);
}

void TextBuffer::MoveCursor(long long delta) {
  const long long length = static_cast<long long>(Length());
  long long target = static_cast<long long>(cursor_) + delta;
  target = std::max(0LL, std::min(target, length));
  cursor_ = static_cast<size_t>(target);
  anchor_ = cursor_;
}

void TextBuffer::Select(size_t start, size_t end) {
  const size_t length = Length();
  anchor_ = std::min(start, length);
  cursor_ = std::min(end, length);
}

std::string TextBuffer::SelectedText() const {
  if (cursor_ == anchor_) {
    return std::string();
  }
  const size_t start = std::min(cursor_, anchor_);
  const size_t end = std::max(cursor_, anchor_);
  return Utf8Substr(text_, start, end - start);
}

std::string TextBuffer::BeforeCursor(size_t max_codepoints) const {
  if (max_codepoints == 0) {
    return std::string();
  }
  const size_t start =
      cursor_ > max_codepoints ? cursor_ - max_codepoints : 0;
  return Utf8Substr(text_, start, cursor_ - start);
}

std::string TextBuffer::AfterCursor(size_t max_codepoints) const {
  if (max_codepoints == 0) {
    return std::string();
  }
  return Utf8Substr(text_, cursor_, max_codepoints);
}

void TextBuffer::Clear() {
  text_.clear();
  cursor_ = 0;
  anchor_ = 0;
}

std::string_view TextBuffer::View() const {
  return text_;
}

size_t TextBuffer::Clamp(size_t codepoint_index) const {
  return std::min(codepoint_index, Length());
}

size_t TextBuffer::ByteOffset(size_t codepoint_index) const {
  size_t offset = 0;
  size_t index = 0;
  while (offset < text_.size() && index < codepoint_index) {
    uint32_t codepoint = 0;
    size_t len = 0;
    if (DecodeUtf8(text_, offset, &codepoint, &len)) {
      offset += len;
    } else {
      offset += 1;
    }
    ++index;
  }
  return offset;
}

}  // namespace cros_ime
