// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tiny sample plugin used by the plugin-registry self-tests.  It implements the
// stable C ABI from include/cros_ime/engine_plugin.h and echoes typed text into
// a preedit, committing it on Enter or Space.

#include "cros_ime/engine_plugin.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

struct CrosImeEngine {
  std::string preedit;
  std::string last_commit;
  std::string last_option;
  std::map<std::string, std::string> options;
};

namespace {

void AppendUtf8(std::string* out, uint32_t codepoint) {
  if (codepoint < 0x80u) {
    out->push_back(static_cast<char>(codepoint));
    return;
  }
  if (codepoint < 0x800u) {
    out->push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
    out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return;
  }
  if (codepoint < 0x10000u) {
    out->push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
    out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    return;
  }
  out->push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
  out->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
  out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
  out->push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
}

size_t CountCodepoints(const std::string& text) {
  size_t count = 0;
  for (char raw : text) {
    if ((static_cast<unsigned char>(raw) & 0xC0u) != 0x80u) {
      ++count;
    }
  }
  return count;
}

void RemoveLastCodepoint(std::string* text) {
  if (text->empty()) {
    return;
  }
  size_t index = text->size() - 1;
  while (index > 0 &&
         (static_cast<unsigned char>((*text)[index]) & 0xC0u) == 0x80u) {
    --index;
  }
  text->erase(index);
}

void ClearOutput(CrosImeOutput* output) {
  output->commit_utf8 = nullptr;
  output->delete_before_cursor = 0;
  output->delete_after_cursor = 0;
  output->preedit_utf8 = nullptr;
  output->preedit_cursor = 0;
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
  output->auxiliary_utf8 = nullptr;
  output->candidate_count = 0;
  output->candidate_page = 0;
  output->candidate_pages = 0;
  output->candidate_page_size = 0;
  output->candidate_cursor = 0;
  output->candidates = nullptr;
  output->mode_flags = 0;
}

void PublishPreedit(CrosImeEngine* engine, CrosImeOutput* output) {
  output->preedit_utf8 = engine->preedit.c_str();
  output->preedit_cursor = static_cast<uint32_t>(CountCodepoints(
      engine->preedit));
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
}

const CrosImeEngineInfo kInfo = {
    sizeof(CrosImeEngineInfo),
    CROS_IME_PLUGIN_ABI_VERSION,
    "fake",
    "Fake Echo Engine",
    "en-US,vi-VN",
    "us",
    "",
    "Sample plugin used by cros-imed self-tests",
};

const CrosImeEngineInfo* FakeGetInfo(void) {
  return &kInfo;
}

CrosImeEngine* FakeCreate(const char* config) {
  auto* engine = new CrosImeEngine();
  if (config != nullptr) {
    const std::string text(config);
    size_t start = 0;
    while (start < text.size()) {
      const size_t end = text.find(',', start);
      const std::string pair =
          text.substr(start, end == std::string::npos ? std::string::npos
                                                      : end - start);
      const size_t equals = pair.find('=');
      if (equals != std::string::npos) {
        engine->options[pair.substr(0, equals)] = pair.substr(equals + 1);
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }
  return engine;
}

void FakeDestroy(CrosImeEngine* engine) {
  delete engine;
}

bool FakeSetOption(CrosImeEngine* engine,
                   const char* key,
                   const char* value) {
  if (engine == nullptr || key == nullptr || value == nullptr) {
    return false;
  }
  engine->options[key] = value;
  return true;
}

const char* FakeGetOption(CrosImeEngine* engine, const char* key) {
  if (engine == nullptr || key == nullptr) {
    return nullptr;
  }
  const auto it = engine->options.find(key);
  if (it == engine->options.end()) {
    return nullptr;
  }
  engine->last_option = it->second;
  return engine->last_option.c_str();
}

void FakeReset(CrosImeEngine* engine) {
  if (engine != nullptr) {
    engine->preedit.clear();
    engine->last_commit.clear();
  }
}

void FakeFocusIn(CrosImeEngine* engine, const char* context_id) {
  (void)engine;
  (void)context_id;
}

void FakeFocusOut(CrosImeEngine* engine) {
  (void)engine;
}

void FakeSetSurroundingText(CrosImeEngine* engine,
                            const char* before_cursor,
                            const char* selected) {
  (void)engine;
  (void)before_cursor;
  (void)selected;
}

uint32_t FakeProcessKey(CrosImeEngine* engine,
                        const CrosImeKeyEvent* event,
                        CrosImeOutput* output) {
  if (engine == nullptr || event == nullptr || output == nullptr) {
    return 0;
  }
  ClearOutput(output);

  uint32_t flags = CROS_IME_RESULT_CONSUMED;
  engine->last_commit.clear();

  switch (event->type) {
    case CROS_IME_KEY_CHAR:
      AppendUtf8(&engine->preedit, event->codepoint);
      flags |= CROS_IME_RESULT_PREEDIT_CHANGED;
      break;
    case CROS_IME_KEY_BACKSPACE:
      RemoveLastCodepoint(&engine->preedit);
      flags |= CROS_IME_RESULT_PREEDIT_CHANGED;
      break;
    case CROS_IME_KEY_SPACE:
    case CROS_IME_KEY_ENTER:
      if (!engine->preedit.empty()) {
        engine->last_commit = engine->preedit;
        engine->preedit.clear();
        output->commit_utf8 = engine->last_commit.c_str();
        flags |= CROS_IME_RESULT_HAS_COMMIT | CROS_IME_RESULT_PREEDIT_CHANGED;
      }
      break;
    case CROS_IME_KEY_ESCAPE:
      engine->preedit.clear();
      flags |= CROS_IME_RESULT_PREEDIT_CHANGED;
      break;
    default:
      break;
  }

  PublishPreedit(engine, output);
  output->flags = flags;
  return flags;
}

uint32_t FakeSelectCandidate(CrosImeEngine* engine,
                             uint32_t page,
                             uint32_t index,
                             CrosImeOutput* output) {
  (void)page;
  (void)index;
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  ClearOutput(output);
  PublishPreedit(engine, output);
  output->flags = CROS_IME_RESULT_CONSUMED;
  return CROS_IME_RESULT_CONSUMED;
}

uint32_t FakeCandidateAction(CrosImeEngine* engine,
                             uint32_t action,
                             uint32_t arg,
                             CrosImeOutput* output) {
  (void)action;
  (void)arg;
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  ClearOutput(output);
  PublishPreedit(engine, output);
  output->flags = CROS_IME_RESULT_CONSUMED;
  return CROS_IME_RESULT_CONSUMED;
}

uint32_t FakeLongpressQuery(CrosImeEngine* engine,
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

const char* FakeLayoutJson(CrosImeEngine* engine) {
  (void)engine;
  return "{\"layout\":\"us\",\"engine\":\"fake\"}";
}

const CrosImeEnginePluginV1 kPlugin = {
    sizeof(CrosImeEnginePluginV1),
    CROS_IME_PLUGIN_ABI_VERSION,
    FakeGetInfo,
    FakeCreate,
    FakeDestroy,
    FakeSetOption,
    FakeGetOption,
    FakeReset,
    FakeFocusIn,
    FakeFocusOut,
    FakeSetSurroundingText,
    FakeProcessKey,
    FakeSelectCandidate,
    FakeCandidateAction,
    FakeLongpressQuery,
    FakeLayoutJson,
};

}  // namespace

extern "C" CROS_IME_EXPORT const CrosImeEnginePluginV1* cros_ime_plugin_v1(
    void) {
  return &kPlugin;
}

extern "C" CROS_IME_EXPORT uint32_t cros_ime_plugin_abi_version(void) {
  return CROS_IME_PLUGIN_ABI_VERSION;
}
