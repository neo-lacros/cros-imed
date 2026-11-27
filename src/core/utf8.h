// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_UTF8_H_
#define CROS_IME_CORE_UTF8_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cros_ime {

// Decodes one codepoint starting at `offset`.  Returns false for any invalid
// sequence: overlong encodings, surrogates, truncated input and values above
// U+10FFFF.  On success `*len` is 1..4 and `offset + *len` is the next
// codepoint boundary.
bool DecodeUtf8(std::string_view text,
                size_t offset,
                uint32_t* codepoint,
                size_t* len);

// Encodes a codepoint.  Invalid codepoints become U+FFFD.
std::string EncodeUtf8(uint32_t codepoint);

// Number of codepoints in `text`.  Every invalid byte counts as one
// replacement codepoint so that lengths never break apart a valid sequence.
size_t Utf8Length(std::string_view text);

// Codepoint-oriented substring.  Out-of-range requests are clamped rather than
// producing invalid UTF-8.
std::string Utf8Substr(std::string_view text,
                       size_t cp_start,
                       size_t cp_len);

// Strict-ish conversion helpers.  Invalid bytes become U+FFFD in the output.
std::vector<uint32_t> Utf8ToCodepoints(std::string_view text);
std::string CodepointsToUtf8(const std::vector<uint32_t>& codepoints);

std::string AsciiToLower(std::string_view text);
std::string AsciiToUpper(std::string_view text);

// True for Unicode combining marks that cros-imed composes (Latin combining
// diacritics, including the Vietnamese tone/shape marks).
bool IsCombiningMark(uint32_t codepoint);

// Vietnamese composition primitives.
//
//   tone:  0 = none, 1 = acute, 2 = grave, 3 = hook, 4 = tilde, 5 = dot
//   shape: 0 = none, 1 = circumflex, 2 = breve, 3 = horn
//          (shape 3 on base 'd' means the stroked letter đ)
//
// Returns 0 when the combination does not exist.
uint32_t ComposeVietnamese(uint32_t base, int tone, int shape);

// Decomposes a precomposed Vietnamese letter into its base letter, tone and
// shape.  Returns false when `codepoint` is not a Vietnamese letter.
bool DecomposeVietnamese(uint32_t codepoint, uint32_t* base, int* tone,
                         int* shape);

// NFC-like composition for lowercase Vietnamese text: A base letter followed
// by combining marks is folded into a single precomposed codepoint.  Text that
// is already precomposed is preserved.
std::string NormalizeVietnamese(std::string_view text);

}  // namespace cros_ime

#endif  // CROS_IME_CORE_UTF8_H_
