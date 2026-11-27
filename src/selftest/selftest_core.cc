// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "selftest/registry.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

#include "core/assistive.h"
#include "core/compose.h"
#include "core/ipc.h"
#include "core/text_buffer.h"
#include "core/utf8.h"

namespace cros_ime {
namespace {

std::string Join(const std::vector<std::string>& items) {
  std::string out;
  for (const std::string& item : items) {
    out += item;
  }
  return out;
}

bool Contains(const std::vector<std::string>& items,
              const std::string& needle) {
  return std::find(items.begin(), items.end(), needle) != items.end();
}

std::string Bytes(std::initializer_list<unsigned char> bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (unsigned char byte : bytes) {
    out.push_back(static_cast<char>(byte));
  }
  return out;
}

}  // namespace

CROS_IME_SELFTEST("core utf8", 100, TestCoreUtf8) {
  const std::string viet = "việt";
  checker.ExpectStr("utf8 round-trip việt",
                    CodepointsToUtf8(Utf8ToCodepoints(viet)), viet);
  checker.ExpectStr("utf8 round-trip 한글",
                    CodepointsToUtf8(Utf8ToCodepoints("한글")), "한글");
  checker.ExpectStr("utf8 round-trip 日本語",
                    CodepointsToUtf8(Utf8ToCodepoints("日本語")), "日本語");
  checker.ExpectInt("Utf8Length(việt)",
                    static_cast<long long>(Utf8Length(viet)), 4);
  checker.ExpectInt("Utf8Length(한글)",
                    static_cast<long long>(Utf8Length("한글")), 2);
  checker.ExpectInt("Utf8Length(日本語)",
                    static_cast<long long>(Utf8Length("日本語")), 3);

  uint32_t codepoint = 0;
  size_t length = 0;
  const bool decoded_emoji = DecodeUtf8("😀", 0, &codepoint, &length);
  checker.Expect("decode 😀", decoded_emoji);
  checker.ExpectInt("😀 codepoint", static_cast<long long>(codepoint),
                    0x1F600);
  checker.ExpectInt("😀 byte length", static_cast<long long>(length), 4);

  const bool overlong = DecodeUtf8(Bytes({0xC0, 0x80}), 0, &codepoint, &length);
  checker.Expect("strict decode rejects overlong C0 80", !overlong);
  const bool surrogate =
      DecodeUtf8(Bytes({0xED, 0xA0, 0x80}), 0, &codepoint, &length);
  checker.Expect("strict decode rejects surrogate ED A0 80", !surrogate);
  const bool truncated = DecodeUtf8(Bytes({0xE2, 0x82}), 0, &codepoint, &length);
  checker.Expect("strict decode rejects truncated E2 82", !truncated);

  checker.ExpectStr("Utf8Substr 日本語 [1,1]", Utf8Substr("日本語", 1, 1), "本");
  checker.ExpectStr("Utf8Substr 日本語 [0,9]", Utf8Substr("日本語", 0, 9),
                    "日本語");
  checker.ExpectStr("Utf8Substr 日本語 [9,1]", Utf8Substr("日本語", 9, 1), "");
  checker.ExpectInt("Utf8ToCodepoints a😀b size",
                    static_cast<long long>(Utf8ToCodepoints("a😀b").size()), 3);

  checker.ExpectStr("AsciiToLower", AsciiToLower("AbC"), "abc");
  checker.Expect("U+0323 is a combining mark", IsCombiningMark(0x0323));
  checker.Expect("ASCII a is not a combining mark", !IsCombiningMark('a'));

  const std::string decomposed =
      std::string("e") + Bytes({0xCC, 0x82}) + Bytes({0xCC, 0xA3});
  checker.ExpectStr("NormalizeVietnamese e+circumflex+dot",
                    NormalizeVietnamese(decomposed), "ệ");
}

CROS_IME_SELFTEST("core text buffer", 110, TestCoreTextBuffer) {
  TextBuffer buffer("việt");
  checker.ExpectInt("buffer length", static_cast<long long>(buffer.Length()), 4);
  checker.ExpectInt("buffer cursor", static_cast<long long>(buffer.Cursor()), 4);
  checker.ExpectInt("buffer anchor", static_cast<long long>(buffer.Anchor()), 4);

  buffer.EraseBefore(1);
  checker.ExpectStr("EraseBefore(1) keeps valid UTF-8",
                    std::string(buffer.View()), "việ");
  checker.ExpectInt("buffer length after erase",
                    static_cast<long long>(buffer.Length()), 3);

  buffer.Insert("😀");
  checker.ExpectStr("Insert emoji after multibyte text",
                    std::string(buffer.View()), "việ😀");
  checker.ExpectInt("buffer length after emoji insert",
                    static_cast<long long>(buffer.Length()), 4);

  buffer.MoveCursor(-1);
  buffer.Insert("X");
  checker.ExpectStr("MoveCursor respects codepoint boundaries",
                    std::string(buffer.View()), "việX😀");

  TextBuffer selected("việt");
  selected.Select(1, 3);
  checker.ExpectStr("SelectedText", selected.SelectedText(), "iệ");

  TextBuffer navigation("việt");
  checker.ExpectStr("BeforeCursor(2)", navigation.BeforeCursor(2), "ệt");
  navigation.Select(0, 0);
  checker.ExpectStr("AfterCursor(2)", navigation.AfterCursor(2), "vi");

  TextBuffer replacement("abc");
  replacement.Select(0, 2);
  replacement.Insert("X");
  checker.ExpectStr("Insert replaces selection", std::string(replacement.View()),
                    "Xc");

  TextBuffer eraser("a😀b");
  eraser.Set("a😀b", 1, 1);
  eraser.EraseAfter(1);
  checker.ExpectStr("EraseAfter removes the whole emoji",
                    std::string(eraser.View()), "ab");

  TextBuffer empty("abc");
  empty.Clear();
  checker.ExpectInt("Clear empties the buffer",
                    static_cast<long long>(empty.Length()), 0);
}

CROS_IME_SELFTEST("core vietnamese", 120, TestCoreVietnamese) {
  checker.ExpectInt("ComposeVietnamese a grave",
                    static_cast<long long>(ComposeVietnamese('a', 2, 0)),
                    0x00E0);
  checker.ExpectInt("ComposeVietnamese e dot circumflex",
                    static_cast<long long>(ComposeVietnamese('e', 5, 1)),
                    0x1EC7);
  checker.ExpectInt("ComposeVietnamese o grave horn",
                    static_cast<long long>(ComposeVietnamese('o', 2, 3)),
                    0x1EDD);
  checker.ExpectInt("ComposeVietnamese u acute horn",
                    static_cast<long long>(ComposeVietnamese('u', 1, 3)),
                    0x1EE9);
  checker.ExpectInt("ComposeVietnamese e acute circumflex",
                    static_cast<long long>(ComposeVietnamese('e', 1, 1)),
                    0x1EBF);
  checker.ExpectInt("ComposeVietnamese y tilde",
                    static_cast<long long>(ComposeVietnamese('y', 4, 0)),
                    0x1EF9);
  checker.ExpectInt("ComposeVietnamese d stroke",
                    static_cast<long long>(ComposeVietnamese('d', 0, 3)),
                    0x0111);

  uint32_t base = 0;
  int tone = -1;
  int shape = -1;
  const bool decomposed =
      DecomposeVietnamese(0x1EC7u, &base, &tone, &shape);
  checker.Expect("DecomposeVietnamese ệ", decomposed);
  checker.ExpectInt("ệ base", static_cast<long long>(base), 'e');
  checker.ExpectInt("ệ tone", tone, 5);
  checker.ExpectInt("ệ shape", shape, 1);

  checker.ExpectStr("NormalizeVietnamese combining to việt",
                    NormalizeVietnamese(std::string("vie") +
                                        Bytes({0xCC, 0x82}) +
                                        Bytes({0xCC, 0xA3}) + "t"),
                    "việt");
  checker.ExpectStr("NormalizeVietnamese preserves precomposed",
                    NormalizeVietnamese("việt"), "việt");

  const char bases[] = {'a', 'e', 'i', 'o', 'u', 'y'};
  const int shapes[] = {0, 1, 2, 3};
  bool round_trips = true;
  int composed_count = 0;
  for (char letter : bases) {
    for (int shape_id : shapes) {
      for (int tone_id = 0; tone_id <= 5; ++tone_id) {
        const uint32_t composed = ComposeVietnamese(
            static_cast<uint32_t>(letter), tone_id, shape_id);
        if (composed == 0) {
          continue;
        }
        ++composed_count;
        uint32_t out_base = 0;
        int out_tone = -1;
        int out_shape = -1;
        if (!DecomposeVietnamese(composed, &out_base, &out_tone, &out_shape) ||
            out_base != static_cast<uint32_t>(letter) ||
            out_tone != tone_id || out_shape != shape_id) {
          round_trips = false;
        }
      }
    }
  }
  checker.Expect("Vietnamese table round-trips", round_trips);
  checker.ExpectGe("Vietnamese table is complete", composed_count, 70);
}

CROS_IME_SELFTEST("core compose", 130, TestCoreCompose) {
  const char* old_environment = std::getenv("CROS_IME_COMPOSE_FILE");
  const bool had_environment = old_environment != nullptr;
  const std::string old_value =
      old_environment != nullptr ? old_environment : "";

  setenv("CROS_IME_COMPOSE_FILE", "/nonexistent", 1);
  XkbComposeEngine fallback = XkbComposeEngine::LoadDefault();
  checker.Expect("fallback does not claim a file was loaded",
                 !fallback.loaded_from_file());
  checker.ExpectGe("fallback table size",
                   static_cast<long long>(fallback.TableSize()), 50);

  const ComposeResult pending = fallback.Feed("dead_grave");
  checker.Expect("first dead key is pending", pending.pending);
  checker.Expect("first dead key is not matched", !pending.matched);

  const ComposeResult grave_a = fallback.Feed("a");
  checker.Expect("fallback dead_grave+a matched", grave_a.matched);
  checker.ExpectStr("fallback dead_grave+a text", grave_a.text, "à");

  const ComposeResult acute_e = fallback.Feed("dead_acute");
  (void)acute_e;
  const ComposeResult acute_e_result = fallback.Feed("e");
  checker.ExpectStr("fallback dead_acute+e text", acute_e_result.text, "é");

  fallback.Feed("dead_circumflex");
  const ComposeResult circumflex_o = fallback.Feed("o");
  checker.ExpectStr("fallback dead_circumflex+o text", circumflex_o.text, "ô");

  fallback.Feed("dead_horn");
  const ComposeResult horn_o = fallback.Feed("o");
  checker.ExpectStr("fallback dead_horn+o text", horn_o.text, "ơ");

  fallback.Feed("dead_stroke");
  const ComposeResult stroke_d = fallback.Feed("d");
  checker.ExpectStr("fallback dead_stroke+d text", stroke_d.text, "đ");

  fallback.Reset();
  fallback.Feed("dead_grave");
  fallback.Reset();
  const ComposeResult after_reset = fallback.Feed("a");
  checker.Expect("Reset clears pending state", !after_reset.matched);

  fallback.Feed("dead_acute");
  const ComposeResult codepoint_e = fallback.FeedCodepoint('e');
  checker.ExpectStr("FeedCodepoint dead_acute+e", codepoint_e.text, "é");

  if (had_environment) {
    setenv("CROS_IME_COMPOSE_FILE", old_value.c_str(), 1);
  } else {
    unsetenv("CROS_IME_COMPOSE_FILE");
  }

  unsetenv("CROS_IME_COMPOSE_FILE");
  XkbComposeEngine system = XkbComposeEngine::LoadDefault();
  checker.Expect("system Compose file loaded", system.loaded_from_file());
  if (system.loaded_from_file()) {
    checker.Ok("compose loaded from " + system.source_path());
    checker.ExpectGe("system compose table size",
                     static_cast<long long>(system.TableSize()), 1000);
    system.Feed("dead_grave");
    const ComposeResult system_grave_a = system.Feed("a");
    checker.Expect("system dead_grave+a matched", system_grave_a.matched);
    checker.ExpectStr("system dead_grave+a text", system_grave_a.text, "à");
  }

  if (had_environment) {
    setenv("CROS_IME_COMPOSE_FILE", old_value.c_str(), 1);
  } else {
    unsetenv("CROS_IME_COMPOSE_FILE");
  }
}

CROS_IME_SELFTEST("core assistive", 140, TestCoreAssistive) {
  const AssistiveTables& tables = AssistiveTables::Instance();

  const std::vector<std::string>* variants = tables.LongpressVariants('a');
  checker.Expect("a longpress variants exist", variants != nullptr);
  if (variants != nullptr) {
    checker.ExpectInt("a longpress variant count",
                      static_cast<long long>(variants->size()), 17);
    const std::vector<std::string> first_five(variants->begin(),
                                              variants->begin() + 5);
    checker.ExpectStr("a longpress canonical order", Join(first_five),
                      "àáảãạ");
  }

  const std::vector<std::string>* d_variants = tables.LongpressVariants('d');
  checker.Expect("d longpress variants exist", d_variants != nullptr);
  if (d_variants != nullptr) {
    checker.ExpectInt("d longpress variant count",
                      static_cast<long long>(d_variants->size()), 1);
    checker.ExpectStr("d longpress is đ", d_variants->front(), "đ");
  }

  const std::vector<std::string>* upper_a = tables.LongpressVariants('A');
  checker.Expect("uppercase A variants exist", upper_a != nullptr);
  if (upper_a != nullptr) {
    checker.ExpectInt("uppercase A variant count",
                      static_cast<long long>(upper_a->size()), 17);
  }

  checker.ExpectGe("emoji count", static_cast<long long>(tables.Emoji().size()),
                   64);
  checker.Expect("emoji contains 😀", Contains(tables.Emoji(), "😀"));
  checker.Expect("emoji contains 🚀", Contains(tables.Emoji(), "🚀"));
  checker.ExpectGe("kaomoji count",
                   static_cast<long long>(tables.Kaomoji().size()), 24);
  checker.Expect("kaomoji contains (＾▽＾)",
                 Contains(tables.Kaomoji(), "(＾▽＾)"));

  const auto& categories = tables.EmojiCategories();
  checker.ExpectInt("emoji category count",
                    static_cast<long long>(categories.size()), 9);
  bool categories_ok = true;
  for (const auto& category : categories) {
    if (category.second.size() < 8) {
      categories_ok = false;
    }
  }
  checker.Expect("every emoji category has >= 8 entries", categories_ok);
  checker.ExpectGe("longpress base count",
                   static_cast<long long>(tables.LongpressBases().size()), 20);

  ClipboardRing ring(2);
  ring.Push("alpha");
  ring.Push("beta");
  ring.Push("gamma");
  checker.ExpectInt("clipboard capacity enforced",
                    static_cast<long long>(ring.Items().size()), 2);
  checker.ExpectStr("clipboard newest first", ring.Items()[0], "gamma");
  checker.ExpectStr("clipboard second item", ring.Items()[1], "beta");

  ring.Push("beta");
  checker.ExpectStr("clipboard dedupes existing", ring.Items()[0], "beta");
  checker.ExpectStr("clipboard keeps next after dedupe", ring.Items()[1],
                    "gamma");

  std::string fetched;
  checker.Expect("clipboard Get(0)", ring.Get(0, &fetched));
  checker.ExpectStr("clipboard Get(0) value", fetched, "beta");
  checker.Expect("clipboard Get out of range", !ring.Get(9, &fetched));

  const std::string clipboard_path =
      "/tmp/cros_ime_clipboard_selftest.txt";
  unlink(clipboard_path.c_str());
  ClipboardRing persistent(8);
  persistent.Push("line1\nline2\t\"q\"\\end");
  persistent.Push("plain");
  checker.Expect("clipboard SaveToFile", persistent.SaveToFile(clipboard_path));

  ClipboardRing restored(8);
  checker.Expect("clipboard LoadFromFile", restored.LoadFromFile(clipboard_path));
  checker.ExpectInt("clipboard round-trip size",
                    static_cast<long long>(restored.Items().size()), 2);
  bool round_trip_equal = restored.Items().size() == persistent.Items().size() &&
                          restored.Items()[0] == persistent.Items()[0] &&
                          restored.Items()[1] == persistent.Items()[1];
  checker.Expect("clipboard round-trip content", round_trip_equal);

  ring.Clear();
  checker.ExpectInt("clipboard Clear",
                    static_cast<long long>(ring.Items().size()), 0);
}

CROS_IME_SELFTEST("core ipc", 150, TestCoreIpc) {
  checker.ExpectStr("JsonEscape quotes/backslashes/newlines",
                    JsonEscape("a\"b\\c\nd\te"),
                    "a\\\"b\\\\c\\nd\\te");
  checker.ExpectStr("JsonEscape control character",
                    JsonEscape(std::string(1, '\x01')), "\\u0001");
  checker.ExpectStr("JsonQuote", JsonQuote("a\nb"), "\"a\\nb\"");

  std::map<std::string, JsonValue> object;
  std::string error;
  checker.Expect("ParseJsonObject sample",
                 ParseJsonObject("{\"op\":\"ping\",\"n\":42,\"b\":true}",
                                 &object, &error));
  checker.ExpectStr("parsed op", object["op"].AsString(), "ping");
  checker.ExpectInt("parsed number", object["n"].AsInt(), 42);
  checker.Expect("parsed bool", object["b"].AsBool());

  checker.Expect("ParseJsonObject rejects trailing garbage",
                 !ParseJsonObject("{\"op\":\"ping\"} x", &object, &error));
  checker.Expect("ParseJsonObject rejects malformed",
                 !ParseJsonObject("{\"op\":}", &object, &error));
  checker.Expect("DefaultIpcSocketPath is non-empty",
                 !DefaultIpcSocketPath().empty());
}

}  // namespace cros_ime
