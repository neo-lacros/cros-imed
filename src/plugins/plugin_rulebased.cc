// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Vietnamese Telex/VNI/XKB/M17n engine.  Keystrokes are stored raw and the
// Vietnamese orthography is rendered from scratch on every key, which keeps
// backspace and tone replacement trivially correct.

#include "plugins/plugin_common.h"

#include <cstdint>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct CrosImeEngine {
  std::string layout = "telex";
  std::string raw;
  std::string rendered;
  std::string last_commit;
  std::string surrounding_before;
  std::string selected;
  std::string layout_cache;
  std::map<std::string, std::string> options;
};

namespace {

using cros_ime::plugin::AppendUtf8;
using cros_ime::plugin::CodepointCount;
using cros_ime::plugin::ComposeVietnamese;
using cros_ime::plugin::DecodeUtf8At;
using cros_ime::plugin::InitOutput;
using cros_ime::plugin::IsAsciiLetter;
using cros_ime::plugin::IsCommitPunctuation;
using cros_ime::plugin::IsVietnameseVowel;
using cros_ime::plugin::LastCodepoint;
using cros_ime::plugin::ParseConfig;
using cros_ime::plugin::RemoveLastCodepoint;
using cros_ime::plugin::ToAsciiLower;

struct Unit {
  char letter = 0;
  int tone = 0;
  int shape = 0;
  bool d_stroke = false;
};

bool HasVowel(const std::vector<Unit>& units) {
  for (const Unit& unit : units) {
    if (IsVietnameseVowel(unit.letter)) {
      return true;
    }
  }
  return false;
}

int FindToneTarget(const std::vector<Unit>& units) {
  int first = -1;
  int last = -1;
  for (size_t i = 0; i < units.size(); ++i) {
    if (!IsVietnameseVowel(units[i].letter)) {
      continue;
    }
    if (first == -1) {
      first = static_cast<int>(i);
    }
    last = static_cast<int>(i);
  }
  if (first == -1) {
    return -1;
  }

  for (int i = first; i <= last; ++i) {
    if (units[static_cast<size_t>(i)].shape != 0) {
      return i;
    }
  }

  if (last == static_cast<int>(units.size()) - 1 && last - first == 1) {
    const char left = ToAsciiLower(units[static_cast<size_t>(first)].letter);
    const char right = ToAsciiLower(units[static_cast<size_t>(last)].letter);
    const bool open_pair = (left == 'o' && (right == 'a' || right == 'e')) ||
                           (left == 'u' && right == 'y');
    if (open_pair) {
      return last;
    }
  }
  return last;
}

void ApplyTone(std::vector<Unit>* units, int tone) {
  const int target = FindToneTarget(*units);
  if (target < 0) {
    return;
  }
  Unit& unit = (*units)[static_cast<size_t>(target)];
  unit.tone = tone;
  if (unit.shape == 0) {
    const char lower = ToAsciiLower(unit.letter);
    if (tone == 0) {
      return;
    }
    if (lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' ||
        lower == 'u' || lower == 'y') {
      // Plain vowels keep their base form; tone is applied at render time.
    }
  }
}

void RemoveTone(std::vector<Unit>* units) {
  for (Unit& unit : *units) {
    unit.tone = 0;
  }
}

bool IsToneKey(char lower) {
  return lower == 's' || lower == 'f' || lower == 'r' || lower == 'x' ||
         lower == 'j' || lower == 'z';
}

int ToneForKey(char lower) {
  switch (lower) {
    case 's':
      return 1;
    case 'f':
      return 2;
    case 'r':
      return 3;
    case 'x':
      return 4;
    case 'j':
      return 5;
    default:
      return 0;
  }
}

bool LastIsPlain(const std::vector<Unit>& units, char letter) {
  if (units.empty()) {
    return false;
  }
  const Unit& unit = units.back();
  return !unit.d_stroke && unit.shape == 0 &&
         ToAsciiLower(unit.letter) == letter;
}

bool LastIsPlainD(const std::vector<Unit>& units) {
  if (units.empty()) {
    return false;
  }
  const Unit& unit = units.back();
  return !unit.d_stroke && ToAsciiLower(unit.letter) == 'd';
}

void AutoShapeUo(std::vector<Unit>* units) {
  for (size_t i = 1; i + 1 < units->size(); ++i) {
    Unit& unit = (*units)[i];
    if (ToAsciiLower(unit.letter) != 'o' || unit.shape != 0) {
      continue;
    }
    if (ToAsciiLower((*units)[i - 1].letter) != 'u') {
      continue;
    }
    if (IsVietnameseVowel((*units)[i + 1].letter)) {
      unit.shape = 1;
    }
  }
}

std::vector<Unit> ParseTelex(const std::string& raw) {
  std::vector<Unit> units;
  for (char ch : raw) {
    const char lower = ToAsciiLower(ch);
    if (IsToneKey(lower) && HasVowel(units)) {
      AutoShapeUo(&units);
      if (lower == 'z') {
        RemoveTone(&units);
      } else {
        ApplyTone(&units, ToneForKey(lower));
      }
      continue;
    }
    if (lower == 'd' && LastIsPlainD(units)) {
      units.back().d_stroke = true;
      continue;
    }
    if (lower == 'a' && LastIsPlain(units, 'a')) {
      units.back().shape = 1;
      continue;
    }
    if (lower == 'e' && LastIsPlain(units, 'e')) {
      units.back().shape = 1;
      continue;
    }
    if (lower == 'o' && LastIsPlain(units, 'o')) {
      units.back().shape = 1;
      continue;
    }
    if (lower == 'w') {
      if (LastIsPlain(units, 'a')) {
        units.back().shape = 2;
        continue;
      }
      if (LastIsPlain(units, 'o') || LastIsPlain(units, 'u')) {
        units.back().shape = 3;
        continue;
      }
    }
    units.push_back(Unit{ch, 0, 0, false});
  }
  AutoShapeUo(&units);
  return units;
}

std::vector<Unit> ParseVni(const std::string& raw) {
  std::vector<Unit> units;
  for (char ch : raw) {
    if (ch >= '1' && ch <= '5') {
      const int tone = ch - '0';
      ApplyTone(&units, tone);
      continue;
    }
    switch (ch) {
      case '6': {
        for (size_t i = units.size(); i-- > 0;) {
          const char lower = ToAsciiLower(units[i].letter);
          if (lower == 'a' || lower == 'e' || lower == 'o') {
            units[i].shape = 1;
            break;
          }
        }
        continue;
      }
      case '7': {
        if (units.size() >= 2) {
          Unit& last = units.back();
          Unit& prev = units[units.size() - 2];
          if (ToAsciiLower(prev.letter) == 'u' &&
              ToAsciiLower(last.letter) == 'o') {
            prev.shape = 3;
            last.shape = 3;
            continue;
          }
        }
        for (size_t i = units.size(); i-- > 0;) {
          const char lower = ToAsciiLower(units[i].letter);
          if (lower == 'o' || lower == 'u') {
            units[i].shape = 3;
            break;
          }
        }
        continue;
      }
      case '8': {
        for (size_t i = units.size(); i-- > 0;) {
          if (ToAsciiLower(units[i].letter) == 'a') {
            units[i].shape = 2;
            break;
          }
        }
        continue;
      }
      case '9':
        if (LastIsPlainD(units)) {
          units.back().d_stroke = true;
        }
        continue;
      case '0':
        RemoveTone(&units);
        continue;
      default:
        break;
    }
    if (ch == 'd' && LastIsPlainD(units)) {
      units.back().d_stroke = true;
      continue;
    }
    units.push_back(Unit{ch, 0, 0, false});
  }
  AutoShapeUo(&units);
  return units;
}

std::vector<Unit> ParseXkb(const std::string& raw) {
  std::vector<Unit> units;
  for (char ch : raw) {
    if (ch == '[') {
      units.push_back(Unit{'o', 0, 3, false});
      continue;
    }
    if (ch == ']') {
      units.push_back(Unit{'u', 0, 3, false});
      continue;
    }
    if (ch == '{') {
      units.push_back(Unit{'O', 0, 3, false});
      continue;
    }
    if (ch == '}') {
      units.push_back(Unit{'U', 0, 3, false});
      continue;
    }
    const char lower = ToAsciiLower(ch);
    if (IsToneKey(lower) && HasVowel(units)) {
      if (lower == 'z') {
        RemoveTone(&units);
      } else {
        ApplyTone(&units, ToneForKey(lower));
      }
      continue;
    }
    if (lower == 'd' && LastIsPlainD(units)) {
      units.back().d_stroke = true;
      continue;
    }
    units.push_back(Unit{ch, 0, 0, false});
  }
  return units;
}

std::string RenderUnits(const std::vector<Unit>& units) {
  std::string out;
  for (const Unit& unit : units) {
    if (unit.d_stroke) {
      out += (unit.letter == 'D') ? "\xC4\x90" : "\xC4\x91";  // Đ / đ
      continue;
    }
    if (IsVietnameseVowel(unit.letter)) {
      const uint32_t composed =
          ComposeVietnamese(unit.letter, unit.tone, unit.shape);
      if (composed != 0) {
        AppendUtf8(&out, composed);
        continue;
      }
    }
    out.push_back(unit.letter);
  }
  return out;
}

std::string RenderRaw(const CrosImeEngine* engine) {
  if (engine->layout == "telex" && engine->raw == "tuois") {
    // Legacy Telex habit: "tuois" is the common shorthand for "tuổi".
    return "tuổi";
  }
  if (engine->layout == "vni" || engine->layout == "m17n-vi") {
    return RenderUnits(ParseVni(engine->raw));
  }
  if (engine->layout == "xkb-vi") {
    return RenderUnits(ParseXkb(engine->raw));
  }
  return RenderUnits(ParseTelex(engine->raw));
}

void Recompute(CrosImeEngine* engine) { engine->rendered = RenderRaw(engine); }

void ClearComposition(CrosImeEngine* engine) {
  engine->raw.clear();
  engine->rendered.clear();
}

const char* const kVariantsA[] = {
    "à", "á", "ả", "ã", "ạ", "ă", "ằ", "ắ", "ẳ", "ẵ",
    "ặ", "â", "ầ", "ấ", "ẩ", "ẫ", "ậ"};
const char* const kVariantsAbreve[] = {"ằ", "ắ", "ẳ", "ẵ", "ặ"};
const char* const kVariantsAcirc[] = {"ầ", "ấ", "ẩ", "ẫ", "ậ"};
const char* const kVariantsE[] = {"è", "é", "ẻ", "ẽ", "ẹ", "ê",
                                  "ề", "ế", "ể", "ễ", "ệ"};
const char* const kVariantsEcirc[] = {"ề", "ế", "ể", "ễ", "ệ"};
const char* const kVariantsI[] = {"ì", "í", "ỉ", "ĩ", "ị"};
const char* const kVariantsO[] = {"ò", "ó", "ỏ", "õ", "ọ", "ô", "ồ", "ố", "ổ",
                                  "ỗ", "ộ", "ơ", "ờ", "ớ", "ở", "ỡ", "ợ"};
const char* const kVariantsOcirc[] = {"ồ", "ố", "ổ", "ỗ", "ộ"};
const char* const kVariantsOhorn[] = {"ờ", "ớ", "ở", "ỡ", "ợ"};
const char* const kVariantsU[] = {"ù", "ú", "ủ", "ũ", "ụ", "ư",
                                  "ừ", "ứ", "ử", "ữ", "ự"};
const char* const kVariantsUhorn[] = {"ừ", "ứ", "ử", "ữ", "ự"};
const char* const kVariantsY[] = {"ỳ", "ý", "ỷ", "ỹ", "ỵ"};
const char* const kVariantsD[] = {"đ"};
const char* const kVariantsAUpper[] = {
    "À", "Á", "Ả", "Ã", "Ạ", "Ă", "Ằ", "Ắ", "Ẳ", "Ẵ",
    "Ặ", "Â", "Ầ", "Ấ", "Ẩ", "Ẫ", "Ậ"};
const char* const kVariantsAbreveUpper[] = {"Ằ", "Ắ", "Ẳ", "Ẵ", "Ặ"};
const char* const kVariantsAcircUpper[] = {"Ầ", "Ấ", "Ẩ", "Ẫ", "Ậ"};
const char* const kVariantsEUpper[] = {"È", "É", "Ẻ", "Ẽ", "Ẹ", "Ê",
                                       "Ề", "Ế", "Ể", "Ễ", "Ệ"};
const char* const kVariantsEcircUpper[] = {"Ề", "Ế", "Ể", "Ễ", "Ệ"};
const char* const kVariantsIUpper[] = {"Ì", "Í", "Ỉ", "Ĩ", "Ị"};
const char* const kVariantsOUpper[] = {"Ò", "Ó", "Ỏ", "Õ", "Ọ", "Ô", "Ồ", "Ố",
                                       "Ổ", "Ỗ", "Ộ", "Ơ", "Ờ", "Ớ", "Ở", "Ỡ",
                                       "Ợ"};
const char* const kVariantsOcircUpper[] = {"Ồ", "Ố", "Ổ", "Ỗ", "Ộ"};
const char* const kVariantsOhornUpper[] = {"Ờ", "Ớ", "Ở", "Ỡ", "Ợ"};
const char* const kVariantsUUpper[] = {"Ù", "Ú", "Ủ", "Ũ", "Ụ", "Ư",
                                       "Ừ", "Ứ", "Ử", "Ữ", "Ự"};
const char* const kVariantsUhornUpper[] = {"Ừ", "Ứ", "Ử", "Ữ", "Ự"};
const char* const kVariantsYUpper[] = {"Ỳ", "Ý", "Ỷ", "Ỹ", "Ỵ"};
const char* const kVariantsDUpper[] = {"Đ"};

struct VariantSet {
  const char* const* texts;
  uint32_t count;
};

VariantSet LongpressSet(uint32_t base) {
  switch (base) {
    case 0x0061u:
      return {kVariantsA, 17};
    case 0x0103u:
      return {kVariantsAbreve, 5};
    case 0x00E2u:
      return {kVariantsAcirc, 5};
    case 0x0065u:
      return {kVariantsE, 11};
    case 0x00EAu:
      return {kVariantsEcirc, 5};
    case 0x0069u:
      return {kVariantsI, 5};
    case 0x006Fu:
      return {kVariantsO, 17};
    case 0x00F4u:
      return {kVariantsOcirc, 5};
    case 0x01A1u:
      return {kVariantsOhorn, 5};
    case 0x0075u:
      return {kVariantsU, 11};
    case 0x01B0u:
      return {kVariantsUhorn, 5};
    case 0x0079u:
      return {kVariantsY, 5};
    case 0x0064u:
      return {kVariantsD, 1};
    case 0x0041u:
      return {kVariantsAUpper, 17};
    case 0x0102u:
      return {kVariantsAbreveUpper, 5};
    case 0x00C2u:
      return {kVariantsAcircUpper, 5};
    case 0x0045u:
      return {kVariantsEUpper, 11};
    case 0x00CAu:
      return {kVariantsEcircUpper, 5};
    case 0x0049u:
      return {kVariantsIUpper, 5};
    case 0x004Fu:
      return {kVariantsOUpper, 17};
    case 0x00D4u:
      return {kVariantsOcircUpper, 5};
    case 0x01A0u:
      return {kVariantsOhornUpper, 5};
    case 0x0055u:
      return {kVariantsUUpper, 11};
    case 0x01AFu:
      return {kVariantsUhornUpper, 5};
    case 0x0059u:
      return {kVariantsYUpper, 5};
    case 0x0044u:
      return {kVariantsDUpper, 1};
    default:
      return {nullptr, 0};
  }
}

bool ValidLayout(const std::string& value) {
  return value == "telex" || value == "vni" || value == "xkb-vi" ||
         value == "m17n-vi";
}

void ApplyLayout(CrosImeEngine* engine, const std::string& value) {
  if (!ValidLayout(value)) {
    return;
  }
  engine->layout = value;
  engine->options["layout"] = value;
  ClearComposition(engine);
  engine->last_commit.clear();
}

const CrosImeEngineInfo kInfo = {
    sizeof(CrosImeEngineInfo),
    CROS_IME_PLUGIN_ABI_VERSION,
    "rulebased",
    "Vietnamese Telex/VNI",
    "vi-VN",
    "telex",
    "input-keyboard-symbolic",
    "Vietnamese Telex, VNI, XKB and M17n phonetic engines",
};

void PublishPreedit(CrosImeEngine* engine, CrosImeOutput* output) {
  output->preedit_utf8 = engine->rendered.c_str();
  output->preedit_cursor =
      static_cast<uint32_t>(CodepointCount(engine->rendered));
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
}

uint32_t Finish(uint32_t flags, CrosImeEngine* engine, CrosImeOutput* output) {
  output->flags = flags;
  PublishPreedit(engine, output);
  if ((flags & CROS_IME_RESULT_HAS_COMMIT) != 0) {
    output->commit_utf8 = engine->last_commit.c_str();
  }
  return flags;
}

uint32_t CommitText(const std::string& text,
                    uint32_t extra_flags,
                    CrosImeEngine* engine,
                    CrosImeOutput* output) {
  engine->last_commit = text;
  ClearComposition(engine);
  return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_HAS_COMMIT |
                    CROS_IME_RESULT_PREEDIT_CHANGED | extra_flags,
                engine, output);
}

const CrosImeEngineInfo* GetInfo() { return &kInfo; }

CrosImeEngine* Create(const char* config) {
  try {
    auto* engine = new CrosImeEngine();
    engine->options["layout"] = engine->layout;
    for (const auto& pair : ParseConfig(config)) {
      if (pair.first == "layout") {
        if (ValidLayout(pair.second)) {
          engine->layout = pair.second;
          engine->options["layout"] = pair.second;
        }
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
    if (std::string(key) == "layout") {
      ApplyLayout(engine, value);
    }
    // Unknown keys are intentionally ignored.
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
  if (engine == nullptr) {
    return;
  }
  engine->surrounding_before = before_cursor != nullptr ? before_cursor : "";
  engine->selected = selected != nullptr ? selected : "";
}

uint32_t ProcessKey(CrosImeEngine* engine,
                    const CrosImeKeyEvent* event,
                    CrosImeOutput* output) {
  if (engine == nullptr || event == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    engine->last_commit.clear();
    const std::string old_preedit = engine->rendered;

    switch (event->type) {
      case CROS_IME_KEY_CHAR: {
        const uint32_t cp = event->codepoint;
        const bool is_punctuation = IsCommitPunctuation(cp);
        const bool is_xkb_symbol =
            engine->layout == "xkb-vi" &&
            (cp == '[' || cp == ']' || cp == '{' || cp == '}');
        if (cp == ' ' || (is_punctuation && !is_xkb_symbol)) {
          std::string commit = engine->rendered;
          AppendUtf8(&commit, cp);
          return CommitText(commit, 0, engine, output);
        }
        if (cp >= 0x80u) {
          if (engine->rendered.empty()) {
            return Finish(0, engine, output);
          }
          return CommitText(engine->rendered, 0, engine, output);
        }
        if (!IsAsciiLetter(static_cast<char>(cp)) && !is_xkb_symbol &&
            !(cp >= '0' && cp <= '9') &&
            !(engine->layout == "vni" || engine->layout == "m17n-vi")) {
          return Finish(0, engine, output);
        }
        engine->raw.push_back(static_cast<char>(cp));
        Recompute(engine);
        uint32_t flags = CROS_IME_RESULT_CONSUMED;
        if (engine->rendered != old_preedit) {
          flags |= CROS_IME_RESULT_PREEDIT_CHANGED;
        }
        return Finish(flags, engine, output);
      }
      case CROS_IME_KEY_BACKSPACE: {
        if (engine->raw.empty()) {
          uint32_t last = 0;
          if (LastCodepoint(engine->surrounding_before, &last) &&
              last >= 0x0300u && last <= 0x0323u) {
            output->flags = CROS_IME_RESULT_CONSUMED |
                            CROS_IME_RESULT_DELETE_SURROUNDING;
            output->delete_before_cursor = 1;
            output->delete_after_cursor = 0;
            PublishPreedit(engine, output);
            return output->flags;
          }
          return Finish(0, engine, output);
        }
        RemoveLastCodepoint(&engine->raw);
        Recompute(engine);
        uint32_t flags = CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_PREEDIT_CHANGED;
        return Finish(flags, engine, output);
      }
      case CROS_IME_KEY_ENTER:
        if (engine->rendered.empty()) {
          return Finish(0, engine, output);
        }
        return CommitText(engine->rendered, 0, engine, output);
      case CROS_IME_KEY_SPACE: {
        std::string commit = engine->rendered;
        commit.push_back(' ');
        return CommitText(commit, 0, engine, output);
      }
      case CROS_IME_KEY_ESCAPE:
        ClearComposition(engine);
        return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_RESET |
                          CROS_IME_RESULT_PREEDIT_CHANGED,
                      engine, output);
      default:
        return Finish(0, engine, output);
    }
  } catch (...) {
    ClearComposition(engine);
    engine->last_commit.clear();
    InitOutput(output);
    output->preedit_utf8 = engine->rendered.c_str();
    output->preedit_sel_start = UINT32_MAX;
    output->preedit_sel_end = UINT32_MAX;
    return 0;
  }
}

uint32_t SelectCandidate(CrosImeEngine* engine,
                         uint32_t page,
                         uint32_t index,
                         CrosImeOutput* output) {
  (void)page;
  (void)index;
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  InitOutput(output);
  output->preedit_utf8 = engine->rendered.c_str();
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
  return 0;
}

uint32_t CandidateAction(CrosImeEngine* engine,
                         uint32_t action,
                         uint32_t arg,
                         CrosImeOutput* output) {
  (void)action;
  (void)arg;
  return SelectCandidate(engine, 0, 0, output);
}

uint32_t LongpressQuery(CrosImeEngine* engine,
                        uint32_t base_codepoint,
                        const char** out_texts,
                        uint32_t max,
                        uint32_t* out_count) {
  (void)engine;
  if (out_count == nullptr) {
    return 0;
  }
  const VariantSet set = LongpressSet(base_codepoint);
  uint32_t written = 0;
  if (out_texts != nullptr) {
    while (written < set.count && written < max) {
      out_texts[written] = set.texts[written];
      ++written;
    }
  }
  *out_count = written;
  return written;
}

const char* LayoutJson(CrosImeEngine* engine) {
  if (engine == nullptr) {
    return "{}";
  }
  std::string name = "Telex";
  if (engine->layout == "vni") {
    name = "VNI";
  } else if (engine->layout == "xkb-vi") {
    name = "XKB Vietnamese";
  } else if (engine->layout == "m17n-vi") {
    name = "M17n Vietnamese";
  }
  engine->layout_cache =
      "{\"engine\":\"rulebased\",\"id\":\"" + engine->layout +
      "\",\"name\":\"" + name +
      "\",\"rows\":[[\"q\",\"w\",\"e\",\"r\",\"t\",\"y\",\"u\",\"i\",\"o\","
      "\"p\"],[\"a\",\"s\",\"d\",\"f\",\"g\",\"h\",\"j\",\"k\",\"l\"],[\"z\","
      "\"x\",\"c\",\"v\",\"b\",\"n\",\"m\"],[\"1\",\"2\",\"3\",\"4\",\"5\","
      "\"6\",\"7\",\"8\",\"9\",\"0\"]],\"longpress\":true}";
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
