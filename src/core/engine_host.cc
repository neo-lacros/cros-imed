// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/engine_host.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/assistive.h"
#include "core/utf8.h"

namespace cros_ime {
namespace {

constexpr uint32_t kMaxCandidates = 10000;
constexpr uint32_t kMaxLongpressVariants = 64;

std::vector<std::string> SplitPluginPaths(const std::string& plugin_dir) {
  std::vector<std::string> paths;
  size_t start = 0;
  while (start < plugin_dir.size()) {
    const size_t end = plugin_dir.find(':', start);
    const std::string part =
        plugin_dir.substr(start, end == std::string::npos
                                     ? std::string::npos
                                     : end - start);
    if (!part.empty()) {
      paths.push_back(part);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return paths;
}

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

EngineHost::EngineHost(std::string plugin_dir, std::string config)
    : registry_(SplitPluginPaths(plugin_dir)), config_(std::move(config)) {}

EngineHost::~EngineHost() {
  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->destroy != nullptr) {
    plugin_.plugin->destroy(engine_);
  }
  engine_ = nullptr;
}

bool EngineHost::SetEngine(const std::string& id, std::string* error) {
  registry_.Scan();
  if (registry_.Find(id) == nullptr) {
    SetError(error, "engine '" + id + "' was not found in any plugin directory");
    return false;
  }

  std::string load_error;
  LoadedPlugin loaded = registry_.Load(id, &load_error);
  if (!loaded) {
    SetError(error, load_error);
    return false;
  }
  if (loaded.plugin->create == nullptr || loaded.plugin->destroy == nullptr) {
    SetError(error, "engine '" + id + "' plugin is missing create/destroy");
    return false;
  }

  CrosImeEngine* engine = loaded.plugin->create(config_.c_str());
  if (engine == nullptr) {
    SetError(error, "engine '" + id + "' failed to create an instance");
    return false;
  }

  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->destroy != nullptr) {
    plugin_.plugin->destroy(engine_);
  }
  engine_ = nullptr;

  plugin_ = std::move(loaded);
  engine_ = engine;
  info_ = plugin_.plugin->get_info();
  engine_id_ = info_ != nullptr && info_->engine_id != nullptr
                   ? info_->engine_id
                   : id;
  state_ = HostState{};
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool EngineHost::has_engine() const {
  return engine_ != nullptr;
}

const CrosImeEngineInfo* EngineHost::info() const {
  return info_;
}

const std::string& EngineHost::engine_id() const {
  return engine_id_;
}

const HostState& EngineHost::state() const {
  return state_;
}

std::vector<EngineDescriptor> EngineHost::ListAvailable() {
  return registry_.Scan();
}

std::vector<std::string> EngineHost::diagnostics() const {
  return registry_.diagnostics();
}

HostResult EngineHost::Key(const CrosImeKeyEvent& event) {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->process_key == nullptr) {
    return Snapshot();
  }

  CrosImeOutput output{};
  output.struct_size = sizeof(output);
  output.preedit_sel_start = UINT32_MAX;
  output.preedit_sel_end = UINT32_MAX;
  const uint32_t flags = plugin_.plugin->process_key(engine_, &event, &output);
  return ConsumeOutput(flags, output);
}

HostResult EngineHost::SelectCandidate(uint32_t page, uint32_t index) {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->select_candidate == nullptr) {
    return Snapshot();
  }

  CrosImeOutput output{};
  output.struct_size = sizeof(output);
  output.preedit_sel_start = UINT32_MAX;
  output.preedit_sel_end = UINT32_MAX;
  const uint32_t flags =
      plugin_.plugin->select_candidate(engine_, page, index, &output);
  return ConsumeOutput(flags, output);
}

HostResult EngineHost::CandidateAction(uint32_t action, uint32_t arg) {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->candidate_action == nullptr) {
    return Snapshot();
  }

  CrosImeOutput output{};
  output.struct_size = sizeof(output);
  output.preedit_sel_start = UINT32_MAX;
  output.preedit_sel_end = UINT32_MAX;
  const uint32_t flags =
      plugin_.plugin->candidate_action(engine_, action, arg, &output);
  return ConsumeOutput(flags, output);
}

HostResult EngineHost::SurroundingTextChanged(const std::string& before_cursor,
                                              const std::string& selected) {
  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->set_surrounding_text != nullptr) {
    plugin_.plugin->set_surrounding_text(engine_, before_cursor.c_str(),
                                         selected.c_str());
  }
  return Snapshot();
}

HostResult EngineHost::Reset() {
  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->reset != nullptr) {
    plugin_.plugin->reset(engine_);
  }
  state_ = HostState{};
  HostResult result;
  result.reset = true;
  result.state = state_;
  return result;
}

HostResult EngineHost::FocusIn(const std::string& context_id) {
  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->focus_in != nullptr) {
    plugin_.plugin->focus_in(engine_, context_id.c_str());
  }
  return Snapshot();
}

HostResult EngineHost::FocusOut() {
  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->focus_out != nullptr) {
    plugin_.plugin->focus_out(engine_);
  }
  return Snapshot();
}

bool EngineHost::SetOption(const std::string& key, const std::string& value) {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->set_option == nullptr) {
    return false;
  }
  return plugin_.plugin->set_option(engine_, key.c_str(), value.c_str());
}

std::string EngineHost::GetOption(const std::string& key) const {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->get_option == nullptr) {
    return std::string();
  }
  return CopyString(plugin_.plugin->get_option(engine_, key.c_str()));
}

HostResult EngineHost::Longpress(uint32_t base_codepoint) {
  std::vector<std::string> variants;

  if (engine_ != nullptr && plugin_.plugin != nullptr &&
      plugin_.plugin->longpress_query != nullptr) {
    const char* texts[kMaxLongpressVariants] = {};
    uint32_t count = 0;
    const uint32_t written = plugin_.plugin->longpress_query(
        engine_, base_codepoint, texts, kMaxLongpressVariants, &count);
    if (count == 0) {
      count = written;
    }
    if (count > kMaxLongpressVariants) {
      count = kMaxLongpressVariants;
    }
    for (uint32_t i = 0; i < count; ++i) {
      variants.push_back(CopyString(texts[i]));
    }
  }

  if (variants.empty()) {
    const std::vector<std::string>* fallback =
        AssistiveTables::Instance().LongpressVariants(base_codepoint);
    if (fallback != nullptr) {
      variants = *fallback;
    }
  }

  state_.longpress = variants;
  HostResult result;
  result.show_longpress = !variants.empty();
  result.hide_longpress = variants.empty();
  result.state = state_;
  return result;
}

std::string EngineHost::LayoutJson() const {
  if (engine_ == nullptr || plugin_.plugin == nullptr ||
      plugin_.plugin->layout_json == nullptr) {
    return "{}";
  }
  return CopyString(plugin_.plugin->layout_json(engine_));
}

std::vector<HostResult> EngineHost::TypeUtf8(const std::string& utf8) {
  std::vector<HostResult> results;
  const std::vector<uint32_t> codepoints = Utf8ToCodepoints(utf8);
  results.reserve(codepoints.size());
  for (uint32_t codepoint : codepoints) {
    results.push_back(Key(MakeCharKey(codepoint)));
  }
  return results;
}

CrosImeKeyEvent EngineHost::MakeCharKey(uint32_t codepoint) {
  CrosImeKeyEvent event{};
  event.struct_size = sizeof(event);
  event.type = CROS_IME_KEY_CHAR;
  event.codepoint = codepoint;
  event.keysym = codepoint;
  return event;
}

CrosImeKeyEvent EngineHost::MakeKey(uint32_t type) {
  CrosImeKeyEvent event{};
  event.struct_size = sizeof(event);
  event.type = type;
  return event;
}

HostResult EngineHost::ConsumeOutput(uint32_t flags,
                                     const CrosImeOutput& output) {
  HostResult result;
  result.consumed = (flags & CROS_IME_RESULT_CONSUMED) != 0;
  result.preedit_changed = (flags & CROS_IME_RESULT_PREEDIT_CHANGED) != 0;
  result.candidates_changed =
      (flags & CROS_IME_RESULT_CANDIDATES_CHANGED) != 0;
  result.auxiliary_changed =
      (flags & CROS_IME_RESULT_AUXILIARY_CHANGED) != 0;
  result.beep = (flags & CROS_IME_RESULT_BEEP) != 0;
  result.reset = (flags & CROS_IME_RESULT_RESET) != 0;

  if ((flags & CROS_IME_RESULT_HAS_COMMIT) != 0) {
    result.commit = CopyString(output.commit_utf8);
  }
  if ((flags & CROS_IME_RESULT_DELETE_SURROUNDING) != 0) {
    result.delete_before = output.delete_before_cursor;
    result.delete_after = output.delete_after_cursor;
  }

  state_.preedit = CopyString(output.preedit_utf8);
  state_.preedit_cursor = output.preedit_cursor;
  state_.sel_start = output.preedit_sel_start;
  state_.sel_end = output.preedit_sel_end;
  state_.has_selection = output.preedit_sel_start != UINT32_MAX &&
                         output.preedit_sel_end != UINT32_MAX;

  state_.auxiliary = CopyString(output.auxiliary_utf8);

  state_.candidates.clear();
  uint32_t candidate_count = output.candidate_count;
  if (candidate_count > kMaxCandidates) {
    candidate_count = kMaxCandidates;
  }
  if (output.candidates != nullptr) {
    state_.candidates.reserve(candidate_count);
    for (uint32_t i = 0; i < candidate_count; ++i) {
      const CrosImeCandidate& candidate = output.candidates[i];
      CandidateView view;
      view.text = CopyString(candidate.text_utf8);
      view.label = CopyString(candidate.label_utf8);
      view.annotation = CopyString(candidate.annotation_utf8);
      state_.candidates.push_back(std::move(view));
    }
  }

  state_.page = output.candidate_page;
  state_.pages = output.candidate_pages;
  state_.page_size = output.candidate_page_size;
  state_.cursor = output.candidate_cursor;
  state_.mode_flags = output.mode_flags;

  state_.preedit_visible = !state_.preedit.empty();
  state_.candidates_visible = !state_.candidates.empty();

  result.show_longpress = (flags & CROS_IME_RESULT_SHOW_LONGPRESS) != 0;
  result.hide_longpress = (flags & CROS_IME_RESULT_HIDE_LONGPRESS) != 0;
  if (result.hide_longpress) {
    state_.longpress.clear();
  }

  result.state = state_;
  return result;
}

HostResult EngineHost::Snapshot() const {
  HostResult result;
  result.state = state_;
  return result;
}

std::string EngineHost::CopyString(const char* text) {
  return text != nullptr ? std::string(text) : std::string();
}

}  // namespace cros_ime
