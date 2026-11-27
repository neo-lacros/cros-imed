// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_ENGINE_HOST_H_
#define CROS_IME_CORE_ENGINE_HOST_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/plugin_registry.h"
#include "cros_ime/engine_plugin.h"

namespace cros_ime {

struct CandidateView {
  std::string text;
  std::string label;
  std::string annotation;
};

struct HostState {
  std::string preedit;
  uint32_t preedit_cursor = 0;
  uint32_t sel_start = 0;
  uint32_t sel_end = 0;
  bool has_selection = false;

  std::string auxiliary;
  std::vector<CandidateView> candidates;
  uint32_t page = 0;
  uint32_t pages = 0;
  uint32_t page_size = 0;
  uint32_t cursor = 0;
  uint32_t mode_flags = 0;

  bool preedit_visible = false;
  bool candidates_visible = false;

  std::vector<std::string> longpress;
};

struct HostResult {
  bool consumed = false;
  std::string commit;
  uint32_t delete_before = 0;
  uint32_t delete_after = 0;

  bool preedit_changed = false;
  bool candidates_changed = false;
  bool auxiliary_changed = false;

  bool show_longpress = false;
  bool hide_longpress = false;
  bool beep = false;
  bool reset = false;

  HostState state;
};

// Composition orchestrator shared by all frontends.
//
// Thread-safety: EngineHost is single-threaded.  All calls must happen on the
// thread that created the host; frontends serialize their events into it.
class EngineHost {
 public:
  explicit EngineHost(std::string plugin_dir, std::string config = {});
  ~EngineHost();

  EngineHost(const EngineHost&) = delete;
  EngineHost& operator=(const EngineHost&) = delete;

  bool SetEngine(const std::string& id, std::string* error);
  bool has_engine() const;
  const CrosImeEngineInfo* info() const;
  const std::string& engine_id() const;
  const HostState& state() const;

  std::vector<EngineDescriptor> ListAvailable();
  std::vector<std::string> diagnostics() const;

  HostResult Key(const CrosImeKeyEvent& event);
  HostResult SelectCandidate(uint32_t page, uint32_t index);
  HostResult CandidateAction(uint32_t action, uint32_t arg);
  HostResult SurroundingTextChanged(const std::string& before_cursor,
                                    const std::string& selected);
  HostResult Reset();
  HostResult FocusIn(const std::string& context_id);
  HostResult FocusOut();

  bool SetOption(const std::string& key, const std::string& value);
  std::string GetOption(const std::string& key) const;

  HostResult Longpress(uint32_t base_codepoint);
  std::string LayoutJson() const;

  // Feeds one CHAR key per codepoint.  Returns every per-codepoint result.
  std::vector<HostResult> TypeUtf8(const std::string& utf8);

  static CrosImeKeyEvent MakeCharKey(uint32_t codepoint);
  static CrosImeKeyEvent MakeKey(uint32_t type);

 private:
  HostResult ConsumeOutput(uint32_t flags, const CrosImeOutput& output);
  HostResult Snapshot() const;
  static std::string CopyString(const char* text);

  PluginRegistry registry_;
  LoadedPlugin plugin_;
  CrosImeEngine* engine_ = nullptr;
  const CrosImeEngineInfo* info_ = nullptr;
  std::string engine_id_;
  std::string config_;
  HostState state_;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_ENGINE_HOST_H_
