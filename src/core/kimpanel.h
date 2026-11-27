// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_KIMPANEL_H_
#define CROS_IME_CORE_KIMPANEL_H_

#include <dbus/dbus.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/engine_host.h"

namespace cros_ime {

class KimpanelClient {
 public:
  enum class Mode { kAuto, kOn, kOff };

  KimpanelClient();
  ~KimpanelClient();

  KimpanelClient(const KimpanelClient&) = delete;
  KimpanelClient& operator=(const KimpanelClient&) = delete;

  static bool ParseMode(const std::string& value, Mode* mode);

  bool Init(Mode mode, EngineHost* host, std::string* error);
  void Shutdown();
  void Tick();
  void SyncState(const HostState& state, const std::string& engine_id);

  bool enabled() const;
  bool attached() const;
  Mode mode() const;

  void SetSpotRect(int x, int y, int w, int h);

  // Testable helpers. These do not require a live panel connection.
  static std::string FormatProperty(const std::string& key,
                                    const std::string& label,
                                    const std::string& icon,
                                    const std::string& tip,
                                    const std::string& hint);
  static std::string EngineProperty(const std::string& engine_id);
  static std::vector<std::string> BuildLookupLabels(
      const std::vector<CandidateView>& candidates);
  static std::vector<std::string> BuildLookupTexts(
      const std::vector<CandidateView>& candidates);
  static std::vector<std::string> BuildLookupAttrs(
      const std::vector<CandidateView>& candidates);
  static bool HasPrevPage(const HostState& state);
  static bool HasNextPage(const HostState& state);
  static void AppendStringArray(DBusMessageIter* iter,
                                const std::vector<std::string>& values);

  // Processes a Kimpanel host signal. Returns true when the signal was
  // recognized and handled. Exposed for in-process self-tests.
  bool HandleSignal(DBusMessage* message);

  // Test hook: simulate panel attachment without a real impanel service.
  void SetAttachedForTest(bool attached);

 private:
  bool ConnectBus(std::string* error);
  void AddMatches();
  void RemoveMatches();
  void RefreshOwnership();
  bool NameHasOwner(const char* name) const;
  void HandleNameOwnerChanged(DBusMessage* message);
  void OnPanelCreated();
  void RegisterProperties();
  void SendCurrentState(bool force);

  void SendSignal(const char* name);
  void SendSignalBool(const char* name, bool value);
  void SendSignalString(const char* name, const std::string& value);
  void SendSignalString2(const char* name,
                         const std::string& first,
                         const std::string& second);
  void SendSignalInt2(const char* name, int32_t first, int32_t second);
  void SendSignalInt4(const char* name,
                      int32_t a,
                      int32_t b,
                      int32_t c,
                      int32_t d);
  void SendLookupTableV1(const HostState& state);
  void CallImpanel2SetLookupTable(const HostState& state);
  void CallImpanel2SetSpotRect();

  void UpdateAttached(bool attached);

  Mode mode_ = Mode::kAuto;
  EngineHost* host_ = nullptr;
  DBusConnection* conn_ = nullptr;
  bool enabled_ = false;
  bool attached_ = false;
  bool impanel_owned_ = false;
  bool impanel2_owned_ = false;

  bool has_last_state_ = false;
  HostState last_state_;
  std::string last_engine_id_;
  bool last_preedit_visible_ = false;
  bool last_aux_visible_ = false;
  bool last_lookup_visible_ = false;

  int spot_x_ = 0;
  int spot_y_ = 0;
  int spot_w_ = 0;
  int spot_h_ = 0;
  bool spot_dirty_ = false;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_KIMPANEL_H_
