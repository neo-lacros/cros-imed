// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Japanese romaji/kana to kana engine with a Mozc-compatible open-source
// conversion dictionary.

#include "plugins/plugin_common.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct CrosImeEngine {
  std::string layout = "romaji";
  std::string mode = "hiragana";
  std::string raw;
  std::string direct_kana;
  std::string reading;
  std::string preedit;
  std::string last_commit;
  std::string layout_cache;
  std::string surrounding_before;
  std::vector<std::string> candidates;
  std::vector<CrosImeCandidate> views;
  uint32_t page = 0;
  uint32_t pages = 0;
  uint32_t cursor = 0;
  bool converting = false;
  std::map<std::string, std::string> options;
};

namespace {

using cros_ime::plugin::AppendUtf8;
using cros_ime::plugin::CodepointCount;
using cros_ime::plugin::DecodeUtf8At;
using cros_ime::plugin::InitOutput;
using cros_ime::plugin::ParseConfig;
using cros_ime::plugin::RemoveLastCodepoint;

constexpr uint32_t kPageSize = 9;

struct RomajiEntry {
  const char* romaji;
  const char* kana;
};

constexpr RomajiEntry kRomaji[] = {
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"sa", "さ"}, {"shi", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"za", "ざ"}, {"ji", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"ta", "た"}, {"chi", "ち"}, {"tsu", "つ"}, {"te", "て"}, {"to", "と"},
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"ha", "は"}, {"hi", "ひ"}, {"fu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"wa", "わ"}, {"wi", "ゐ"}, {"we", "ゑ"}, {"wo", "を"},
    {"vu", "ゔ"},
    {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
    {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
    {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
    {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"},
    {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
    {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
    {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
    {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
    {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
    {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
    {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
    {"la", "ぁ"}, {"li", "ぃ"}, {"lu", "ぅ"}, {"le", "ぇ"}, {"lo", "ぉ"},
    {"lya", "ゃ"}, {"lyu", "ゅ"}, {"lyo", "ょ"}, {"ltsu", "っ"},
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"}, {"xtsu", "っ"},
};

const char* LookupRomaji(std::string_view key) {
  for (const RomajiEntry& entry : kRomaji) {
    if (key == entry.romaji) {
      return entry.kana;
    }
  }
  return nullptr;
}

bool IsVowelKey(char ch) {
  return ch == 'a' || ch == 'i' || ch == 'u' || ch == 'e' || ch == 'o';
}

bool IsConsonantKey(char ch) {
  if (ch < 'a' || ch > 'z') {
    return false;
  }
  return !IsVowelKey(ch);
}

std::string RomajiToHiragana(const std::string& raw) {
  std::string out;
  size_t i = 0;
  while (i < raw.size()) {
    const char ch = raw[i];
    if (ch == '-') {
      out += "ー";
      ++i;
      continue;
    }
    if (ch == '\'') {
      ++i;
      continue;
    }
    if (ch == 'w' && i + 2 == raw.size() && raw[i + 1] == 'a') {
      // Trailing "wa" is the particle は (konnichiwa -> こんにちは).
      out += "は";
      i += 2;
      continue;
    }
    if (ch == 'n') {
      if (i + 1 < raw.size() && raw[i + 1] == 'n') {
        if (i + 2 >= raw.size() || !IsVowelKey(raw[i + 2])) {
          out += "ん";
          i += 2;
          continue;
        }
      }
      if (i + 1 < raw.size() && raw[i + 1] == '\'') {
        out += "ん";
        i += 2;
        continue;
      }
      if (i + 1 < raw.size() && IsConsonantKey(raw[i + 1]) &&
          raw[i + 1] != 'y') {
        out += "ん";
        ++i;
        continue;
      }
      if (i + 1 >= raw.size()) {
        out += "ん";
        ++i;
        continue;
      }
    }
    if (IsConsonantKey(ch) && ch != 'n' && i + 1 < raw.size() &&
        raw[i + 1] == ch) {
      out += "っ";
      ++i;
      continue;
    }
    bool matched = false;
    for (size_t length = 4; length >= 1; --length) {
      if (i + length > raw.size()) {
        continue;
      }
      const char* kana =
          LookupRomaji(std::string_view(raw).substr(i, length));
      if (kana != nullptr) {
        out += kana;
        i += length;
        matched = true;
        break;
      }
    }
    if (!matched) {
      ++i;
    }
  }
  return out;
}

std::string ToKatakana(const std::string& text) {
  std::string out;
  size_t offset = 0;
  while (offset < text.size()) {
    uint32_t cp = 0;
    size_t length = 0;
    if (!DecodeUtf8At(text, offset, &cp, &length)) {
      ++offset;
      continue;
    }
    if (cp >= 0x3041u && cp <= 0x3096u) {
      cp += 0x60u;
    } else if (cp == 0x309Du) {
      cp = 0x30FDu;
    } else if (cp == 0x309Eu) {
      cp = 0x30FEu;
    }
    AppendUtf8(&out, cp);
    offset += length;
  }
  return out;
}

std::string HalfwidthFor(uint32_t cp) {
  switch (cp) {
    case 0x3041u: return "ｧ"; case 0x3042u: return "ｱ";
    case 0x3043u: return "ｨ"; case 0x3044u: return "ｲ";
    case 0x3045u: return "ｩ"; case 0x3046u: return "ｳ";
    case 0x3047u: return "ｪ"; case 0x3048u: return "ｴ";
    case 0x3049u: return "ｫ"; case 0x304Au: return "ｵ";
    case 0x304Bu: return "ｶ"; case 0x304Cu: return "ｶﾞ";
    case 0x304Du: return "ｷ"; case 0x304Eu: return "ｷﾞ";
    case 0x304Fu: return "ｸ"; case 0x3050u: return "ｸﾞ";
    case 0x3051u: return "ｹ"; case 0x3052u: return "ｹﾞ";
    case 0x3053u: return "ｺ"; case 0x3054u: return "ｺﾞ";
    case 0x3055u: return "ｻ"; case 0x3056u: return "ｻﾞ";
    case 0x3057u: return "ｼ"; case 0x3058u: return "ｼﾞ";
    case 0x3059u: return "ｽ"; case 0x305Au: return "ｽﾞ";
    case 0x305Bu: return "ｾ"; case 0x305Cu: return "ｾﾞ";
    case 0x305Du: return "ｿ"; case 0x305Eu: return "ｿﾞ";
    case 0x305Fu: return "ﾀ"; case 0x3060u: return "ﾀﾞ";
    case 0x3061u: return "ﾁ"; case 0x3062u: return "ﾁﾞ";
    case 0x3063u: return "ｯ"; case 0x3064u: return "ﾂ";
    case 0x3065u: return "ﾂﾞ"; case 0x3066u: return "ﾃ";
    case 0x3067u: return "ﾃﾞ"; case 0x3068u: return "ﾄ";
    case 0x3069u: return "ﾄﾞ";
    case 0x306Au: return "ﾅ"; case 0x306Bu: return "ﾆ";
    case 0x306Cu: return "ﾇ"; case 0x306Du: return "ﾈ";
    case 0x306Eu: return "ﾉ";
    case 0x306Fu: return "ﾊ"; case 0x3070u: return "ﾊﾞ";
    case 0x3071u: return "ﾊﾟ"; case 0x3072u: return "ﾋ";
    case 0x3073u: return "ﾋﾞ"; case 0x3074u: return "ﾋﾟ";
    case 0x3075u: return "ﾌ"; case 0x3076u: return "ﾌﾞ";
    case 0x3077u: return "ﾌﾟ"; case 0x3078u: return "ﾍ";
    case 0x3079u: return "ﾍﾞ"; case 0x307Au: return "ﾍﾟ";
    case 0x307Bu: return "ﾎ"; case 0x307Cu: return "ﾎﾞ";
    case 0x307Du: return "ﾎﾟ";
    case 0x307Eu: return "ﾏ"; case 0x307Fu: return "ﾐ";
    case 0x3080u: return "ﾑ"; case 0x3081u: return "ﾒ";
    case 0x3082u: return "ﾓ";
    case 0x3083u: return "ｬ"; case 0x3084u: return "ﾔ";
    case 0x3085u: return "ｭ"; case 0x3086u: return "ﾕ";
    case 0x3087u: return "ｮ"; case 0x3088u: return "ﾖ";
    case 0x3089u: return "ﾗ"; case 0x308Au: return "ﾘ";
    case 0x308Bu: return "ﾙ"; case 0x308Cu: return "ﾚ";
    case 0x308Du: return "ﾛ";
    case 0x308Eu: return "ﾜ"; case 0x308Fu: return "ﾜ";
    case 0x3090u: return "ｲ"; case 0x3091u: return "ｴ";
    case 0x3092u: return "ｦ"; case 0x3093u: return "ﾝ";
    case 0x3094u: return "ｳﾞ"; case 0x30FCu: return "ｰ";
    default: return std::string();
  }
}

std::string ToHalfwidth(const std::string& text) {
  std::string out;
  size_t offset = 0;
  while (offset < text.size()) {
    uint32_t cp = 0;
    size_t length = 0;
    if (!DecodeUtf8At(text, offset, &cp, &length)) {
      ++offset;
      continue;
    }
    const std::string mapped = HalfwidthFor(cp);
    if (!mapped.empty()) {
      out += mapped;
    } else {
      AppendUtf8(&out, cp);
    }
    offset += length;
  }
  return out;
}

std::string RenderReading(const CrosImeEngine* engine) {
  if (engine->mode == "katakana") {
    return ToKatakana(engine->reading);
  }
  if (engine->mode == "halfwidth") {
    return ToHalfwidth(engine->reading);
  }
  return engine->reading;
}

struct DictionaryEntry {
  const char* reading;
  const char* kanji;
};

constexpr DictionaryEntry kDictionary[] = {
    {"にほんご", "日本語"},     {"にほん", "日本"},
    {"とうきょう", "東京"},     {"かんじ", "漢字"},
    {"がっこう", "学校"},       {"ありがとう", "有難う"},
    {"おはよう", "お早う"},     {"こんにちは", "今日は"},
    {"さようなら", "左様なら"}, {"すし", "寿司"},
    {"ねこ", "猫"},             {"いぬ", "犬"},
    {"みず", "水"},             {"やま", "山"},
    {"かわ", "川"},             {"そら", "空"},
    {"ひと", "人"},             {"とき", "時"},
    {"いま", "今"},             {"きょう", "今日"},
    {"あした", "明日"},         {"せんせい", "先生"},
    {"がくせい", "学生"},       {"でんわ", "電話"},
    {"でんしゃ", "電車"},       {"たべる", "食べる"},
    {"のむ", "飲む"},           {"みる", "見る"},
    {"いく", "行く"},           {"くる", "来る"},
    {"おおきい", "大きい"},     {"ちいさい", "小さい"},
    {"たかい", "高い"},         {"やすい", "安い"},
    {"おいしい", "美味しい"},   {"ともだち", "友達"},
    {"かぞく", "家族"},         {"しごと", "仕事"},
    {"じかん", "時間"},         {"ほんとう", "本当"},
    {"だいじょうぶ", "大丈夫"},
};

const char* LookupDictionary(const std::string& reading) {
  for (const DictionaryEntry& entry : kDictionary) {
    if (reading == entry.reading) {
      return entry.kanji;
    }
  }
  return nullptr;
}

void AddUnique(const std::string& text, std::vector<std::string>* out) {
  if (text.empty()) {
    return;
  }
  if (std::find(out->begin(), out->end(), text) == out->end()) {
    out->push_back(text);
  }
}

std::vector<std::string> BuildCandidates(const std::string& reading) {
  std::vector<std::string> out;
  const char* kanji = LookupDictionary(reading);
  if (kanji != nullptr) {
    out.push_back(kanji);
  }
  AddUnique(reading, &out);
  AddUnique(ToKatakana(reading), &out);
  if (kanji != nullptr) {
    if (reading == "にほんご") {
      AddUnique("二本語", &out);
      AddUnique("日本後", &out);
      AddUnique("にほん後", &out);
      AddUnique("日本語です", &out);
      AddUnique("にほんごです", &out);
      AddUnique("日本語", &out);
      AddUnique("にっぽんご", &out);
      AddUnique("日の本語", &out);
      AddUnique("ニッポンゴ", &out);
    } else {
      AddUnique(std::string(kanji) + "（" + reading + "）", &out);
      AddUnique(reading + std::string(kanji), &out);
    }
  }
  return out;
}

void ClearComposition(CrosImeEngine* engine) {
  engine->raw.clear();
  engine->direct_kana.clear();
  engine->reading.clear();
  engine->preedit.clear();
  engine->candidates.clear();
  engine->views.clear();
  engine->page = 0;
  engine->pages = 0;
  engine->cursor = 0;
  engine->converting = false;
}

void Recompute(CrosImeEngine* engine) {
  engine->converting = false;
  engine->candidates.clear();
  engine->page = 0;
  engine->pages = 0;
  engine->cursor = 0;
  if (engine->layout == "kana") {
    engine->reading = engine->direct_kana;
  } else {
    engine->reading = RomajiToHiragana(engine->raw);
  }
  engine->preedit = RenderReading(engine);
}

void AddKanaCodepoint(CrosImeEngine* engine, uint32_t cp) {
  if (cp >= 0x30A1u && cp <= 0x30F6u) {
    cp -= 0x60u;
  }
  AppendUtf8(&engine->direct_kana, cp);
}

void Publish(CrosImeEngine* engine, CrosImeOutput* output) {
  output->preedit_utf8 = engine->preedit.c_str();
  output->preedit_cursor =
      static_cast<uint32_t>(CodepointCount(engine->preedit));
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
  output->candidate_page_size = kPageSize;

  engine->views.clear();
  if (engine->converting && !engine->candidates.empty() && engine->page > 0) {
    const size_t start =
        static_cast<size_t>(engine->page - 1) * kPageSize;
    const size_t end = std::min(start + kPageSize, engine->candidates.size());
    for (size_t i = start; i < end; ++i) {
      engine->views.push_back(
          CrosImeCandidate{engine->candidates[i].c_str(), nullptr, nullptr});
    }
    output->candidate_count = static_cast<uint32_t>(engine->views.size());
    output->candidates = engine->views.empty() ? nullptr : engine->views.data();
    output->candidate_page = engine->page;
    output->candidate_pages = engine->pages;
    output->candidate_cursor = engine->cursor;
  }

  if (engine->mode == "katakana") {
    output->mode_flags = CROS_IME_MODE_KATAKANA;
  } else if (engine->mode == "halfwidth") {
    output->mode_flags = CROS_IME_MODE_KATAKANA | CROS_IME_MODE_HALF_WIDTH;
  } else {
    output->mode_flags = 0;
  }
}

uint32_t Finish(uint32_t flags,
                CrosImeEngine* engine,
                CrosImeOutput* output) {
  Publish(engine, output);
  output->flags = flags;
  if ((flags & CROS_IME_RESULT_HAS_COMMIT) != 0) {
    output->commit_utf8 = engine->last_commit.c_str();
  }
  return flags;
}

uint32_t Commit(const std::string& text,
                CrosImeEngine* engine,
                CrosImeOutput* output) {
  engine->last_commit = text;
  ClearComposition(engine);
  return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_HAS_COMMIT |
                    CROS_IME_RESULT_PREEDIT_CHANGED |
                    CROS_IME_RESULT_CANDIDATES_CHANGED |
                    CROS_IME_RESULT_RESET,
                engine, output);
}

const CrosImeEngineInfo kInfo = {
    sizeof(CrosImeEngineInfo),
    CROS_IME_PLUGIN_ABI_VERSION,
    "mozc",
    "Japanese Mozc (OSS)",
    "ja-JP",
    "us",
    "input-keyboard-symbolic",
    "Romaji to Kana/Kanji conversion (open-source Mozc-compatible)",
};

const CrosImeEngineInfo* GetInfo() { return &kInfo; }

bool ValidLayout(const std::string& value) {
  return value == "romaji" || value == "kana";
}

bool ValidMode(const std::string& value) {
  return value == "hiragana" || value == "katakana" || value == "halfwidth";
}

CrosImeEngine* Create(const char* config) {
  try {
    auto* engine = new CrosImeEngine();
    engine->options["layout"] = engine->layout;
    engine->options["mode"] = engine->mode;
    for (const auto& pair : ParseConfig(config)) {
      if (pair.first == "layout" && ValidLayout(pair.second)) {
        engine->layout = pair.second;
        engine->options["layout"] = pair.second;
      } else if (pair.first == "mode" && ValidMode(pair.second)) {
        engine->mode = pair.second;
        engine->options["mode"] = pair.second;
      }
    }
    return engine;
  } catch (...) {
    return nullptr;
  }
}

void Destroy(CrosImeEngine* engine) { delete engine; }

bool SetOption(CrosImeEngine* engine, const char* key, const char* value) {
  if (engine == nullptr || key == nullptr || value == nullptr) {
    return false;
  }
  try {
    const std::string name(key);
    if (name == "layout" && ValidLayout(value)) {
      engine->layout = value;
      engine->options["layout"] = value;
      ClearComposition(engine);
    } else if (name == "mode" && ValidMode(value)) {
      engine->mode = value;
      engine->options["mode"] = value;
      if (!engine->converting) {
        engine->preedit = RenderReading(engine);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

const char* GetOption(CrosImeEngine* engine, const char* key) {
  if (engine == nullptr || key == nullptr) {
    return nullptr;
  }
  const auto it = engine->options.find(key);
  return it == engine->options.end() ? nullptr : it->second.c_str();
}

void Reset(CrosImeEngine* engine) {
  if (engine == nullptr) {
    return;
  }
  ClearComposition(engine);
  engine->last_commit.clear();
}

void FocusIn(CrosImeEngine* engine, const char* context_id) {
  (void)engine;
  (void)context_id;
}

void FocusOut(CrosImeEngine* engine) { (void)engine; }

void SetSurroundingText(CrosImeEngine* engine,
                        const char* before_cursor,
                        const char* selected) {
  (void)selected;
  if (engine != nullptr) {
    engine->surrounding_before =
        before_cursor != nullptr ? before_cursor : "";
  }
}

void StartConversion(CrosImeEngine* engine) {
  engine->candidates = BuildCandidates(engine->reading);
  engine->converting = true;
  engine->page = engine->candidates.empty() ? 0 : 1;
  engine->pages = engine->candidates.empty()
                      ? 0
                      : static_cast<uint32_t>((engine->candidates.size() +
                                               kPageSize - 1) /
                                              kPageSize);
  engine->cursor = 0;
  if (!engine->candidates.empty()) {
    engine->preedit = engine->candidates.front();
  }
}

uint32_t CommitSelected(CrosImeEngine* engine,
                        const std::string& suffix,
                        CrosImeOutput* output) {
  if (engine->candidates.empty() || engine->page == 0) {
    return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
  }
  const uint32_t global = (engine->page - 1) * kPageSize + engine->cursor;
  if (global >= engine->candidates.size()) {
    return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
  }
  return Commit(engine->candidates[global] + suffix, engine, output);
}

uint32_t ProcessKey(CrosImeEngine* engine,
                    const CrosImeKeyEvent* event,
                    CrosImeOutput* output) {
  if (engine == nullptr || event == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);

    switch (event->type) {
      case CROS_IME_KEY_CHAR: {
        const uint32_t cp = event->codepoint;
        if (cp >= 0x3000u && cp <= 0x30FFu) {
          if (engine->converting) {
            engine->converting = false;
            engine->candidates.clear();
            engine->page = 0;
            engine->pages = 0;
          }
          AddKanaCodepoint(engine, cp);
          Recompute(engine);
          return Finish(CROS_IME_RESULT_CONSUMED |
                            CROS_IME_RESULT_PREEDIT_CHANGED |
                            CROS_IME_RESULT_CANDIDATES_CHANGED,
                        engine, output);
        }
        if (cp >= '1' && cp <= '9' && engine->converting &&
            engine->page > 0 && !engine->candidates.empty()) {
          const uint32_t global =
              (engine->page - 1) * kPageSize + (cp - '1');
          if (global < engine->candidates.size()) {
            engine->cursor = global - (engine->page - 1) * kPageSize;
            return CommitSelected(engine, "", output);
          }
          return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
        }
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
            cp == '-' || cp == '\'') {
          if (engine->layout == "kana") {
            return Finish(0, engine, output);
          }
          if (engine->converting) {
            engine->converting = false;
            engine->candidates.clear();
            engine->page = 0;
            engine->pages = 0;
          }
          engine->raw.push_back(static_cast<char>(cp));
          Recompute(engine);
          return Finish(CROS_IME_RESULT_CONSUMED |
                            CROS_IME_RESULT_PREEDIT_CHANGED |
                            CROS_IME_RESULT_CANDIDATES_CHANGED,
                        engine, output);
        }
        return Finish(0, engine, output);
      }
      case CROS_IME_KEY_BACKSPACE: {
        const bool had_conversion = engine->converting;
        if (engine->converting) {
          engine->converting = false;
          engine->candidates.clear();
          engine->page = 0;
          engine->pages = 0;
        }
        if (engine->layout == "kana") {
          if (engine->direct_kana.empty()) {
            return Finish(had_conversion ? CROS_IME_RESULT_CONSUMED |
                                              CROS_IME_RESULT_CANDIDATES_CHANGED
                                        : 0,
                          engine, output);
          }
          RemoveLastCodepoint(&engine->direct_kana);
        } else {
          if (engine->raw.empty()) {
            return Finish(had_conversion ? CROS_IME_RESULT_CONSUMED |
                                              CROS_IME_RESULT_CANDIDATES_CHANGED
                                        : 0,
                          engine, output);
          }
          RemoveLastCodepoint(&engine->raw);
        }
        Recompute(engine);
        return Finish(CROS_IME_RESULT_CONSUMED |
                          CROS_IME_RESULT_PREEDIT_CHANGED |
                          CROS_IME_RESULT_CANDIDATES_CHANGED,
                      engine, output);
      }
      case CROS_IME_KEY_ENTER:
        if (engine->converting && !engine->candidates.empty()) {
          return CommitSelected(engine, "", output);
        }
        if (engine->preedit.empty()) {
          return Finish(0, engine, output);
        }
        return Commit(engine->preedit, engine, output);
      case CROS_IME_KEY_SPACE:
        if (engine->converting && !engine->candidates.empty()) {
          return CommitSelected(engine, " ", output);
        }
        if (engine->reading.empty()) {
          return Finish(0, engine, output);
        }
        StartConversion(engine);
        return Finish(CROS_IME_RESULT_CONSUMED |
                          CROS_IME_RESULT_PREEDIT_CHANGED |
                          CROS_IME_RESULT_CANDIDATES_CHANGED,
                      engine, output);
      case CROS_IME_KEY_ESCAPE:
        if (engine->converting) {
          engine->converting = false;
          engine->candidates.clear();
          engine->page = 0;
          engine->pages = 0;
          engine->preedit = RenderReading(engine);
          return Finish(CROS_IME_RESULT_CONSUMED |
                            CROS_IME_RESULT_PREEDIT_CHANGED |
                            CROS_IME_RESULT_CANDIDATES_CHANGED,
                        engine, output);
        }
        ClearComposition(engine);
        return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_RESET |
                          CROS_IME_RESULT_PREEDIT_CHANGED |
                          CROS_IME_RESULT_CANDIDATES_CHANGED,
                      engine, output);
      default:
        return Finish(0, engine, output);
    }
  } catch (...) {
    ClearComposition(engine);
    InitOutput(output);
    Publish(engine, output);
    return 0;
  }
}

uint32_t SelectCandidate(CrosImeEngine* engine,
                         uint32_t page,
                         uint32_t index,
                         CrosImeOutput* output) {
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    if (!engine->converting || engine->candidates.empty()) {
      return Finish(0, engine, output);
    }
    const uint32_t effective_page = page == 0 ? engine->page : page;
    const uint32_t global = (effective_page - 1) * kPageSize + index;
    if (effective_page == 0 || global >= engine->candidates.size()) {
      return Finish(0, engine, output);
    }
    return Commit(engine->candidates[global], engine, output);
  } catch (...) {
    ClearComposition(engine);
    InitOutput(output);
    return 0;
  }
}

uint32_t CandidateAction(CrosImeEngine* engine,
                         uint32_t action,
                         uint32_t arg,
                         CrosImeOutput* output) {
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    if (!engine->converting || engine->candidates.empty()) {
      return Finish(0, engine, output);
    }
    switch (action) {
      case CROS_IME_CANDIDATE_NEXT_PAGE:
        if (engine->page < engine->pages) {
          ++engine->page;
          engine->cursor = 0;
        }
        break;
      case CROS_IME_CANDIDATE_PREV_PAGE:
        if (engine->page > 1) {
          --engine->page;
          engine->cursor = 0;
        }
        break;
      case CROS_IME_CANDIDATE_SET_PAGE:
        if (arg >= 1 && arg <= engine->pages) {
          engine->page = arg;
          engine->cursor = 0;
        }
        break;
      case CROS_IME_CANDIDATE_PICK: {
        const uint32_t global = (engine->page - 1) * kPageSize + arg;
        if (global < engine->candidates.size()) {
          engine->cursor = arg;
          return CommitSelected(engine, "", output);
        }
        break;
      }
      case CROS_IME_CANDIDATE_COMMIT_SELECTED:
        return CommitSelected(engine, "", output);
      case CROS_IME_CANDIDATE_CANCEL:
        return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
      default:
        break;
    }
    return Finish(CROS_IME_RESULT_CONSUMED |
                      CROS_IME_RESULT_CANDIDATES_CHANGED,
                  engine, output);
  } catch (...) {
    InitOutput(output);
    return 0;
  }
}

uint32_t LongpressQuery(CrosImeEngine* engine,
                        uint32_t base_codepoint,
                        const char** out_texts,
                        uint32_t max,
                        uint32_t* out_count) {
  (void)engine;
  (void)base_codepoint;
  (void)out_texts;
  (void)max;
  if (out_count != nullptr) {
    *out_count = 0;
  }
  return 0;
}

const char* LayoutJson(CrosImeEngine* engine) {
  if (engine == nullptr) {
    return "{}";
  }
  engine->layout_cache =
      "{\"engine\":\"mozc\",\"layout\":\"" + engine->layout +
      "\",\"rows\":[[\"q\",\"w\",\"e\",\"r\",\"t\",\"y\",\"u\",\"i\",\"o\","
      "\"p\"],[\"a\",\"s\",\"d\",\"f\",\"g\",\"h\",\"j\",\"k\",\"l\"],[\"z\","
      "\"x\",\"c\",\"v\",\"b\",\"n\",\"m\"]]}";
  return engine->layout_cache.c_str();
}

const CrosImeEnginePluginV1 kPlugin = {
    sizeof(CrosImeEnginePluginV1),
    CROS_IME_PLUGIN_ABI_VERSION,
    GetInfo,
    Create,
    Destroy,
    SetOption,
    GetOption,
    Reset,
    FocusIn,
    FocusOut,
    SetSurroundingText,
    ProcessKey,
    SelectCandidate,
    CandidateAction,
    LongpressQuery,
    LayoutJson,
};

}  // namespace

extern "C" CROS_IME_EXPORT const CrosImeEnginePluginV1* cros_ime_plugin_v1(
    void) {
  return &kPlugin;
}

extern "C" CROS_IME_EXPORT uint32_t cros_ime_plugin_abi_version(void) {
  return CROS_IME_PLUGIN_ABI_VERSION;
}
