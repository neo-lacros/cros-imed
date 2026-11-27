// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/kimpanel.h"

#include <dbus/dbus.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cros_ime {
namespace {

constexpr char kImpanelService[] = "org.kde.impanel";
constexpr char kImpanel2Service[] = "org.kde.impanel2";
constexpr char kImpanelPath[] = "/org/freedesktop/impanel";
constexpr char kImpanelInterface[] = "org.kde.impanel";
constexpr char kImpanel2Interface[] = "org.kde.impanel2";
constexpr char kInputMethodInterface[] = "org.kde.kimpanel.inputmethod";
constexpr char kDbusInterface[] = "org.freedesktop.DBus";
constexpr char kDbusPath[] = "/org/freedesktop/DBus";
constexpr char kDbusService[] = "org.freedesktop.DBus";
constexpr char kEnginePropertyKey[] = "/CrosImed/Engine";

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool IsOwnedName(const char* owner) {
  return owner != nullptr && owner[0] != '\0';
}

std::string ReadStringArg(DBusMessage* message, int index) {
  if (message == nullptr) {
    return std::string();
  }
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return std::string();
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return std::string();
    }
  }
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
    return std::string();
  }
  const char* value = nullptr;
  dbus_message_iter_get_basic(&iter, &value);
  return value != nullptr ? std::string(value) : std::string();
}

int32_t ReadInt32Arg(DBusMessage* message, int index, int32_t fallback) {
  if (message == nullptr) {
    return fallback;
  }
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return fallback;
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return fallback;
    }
  }
  const int type = dbus_message_iter_get_arg_type(&iter);
  if (type == DBUS_TYPE_INT32) {
    dbus_int32_t value = fallback;
    dbus_message_iter_get_basic(&iter, &value);
    return static_cast<int32_t>(value);
  }
  if (type == DBUS_TYPE_UINT32) {
    dbus_uint32_t value = static_cast<dbus_uint32_t>(fallback);
    dbus_message_iter_get_basic(&iter, &value);
    return static_cast<int32_t>(value);
  }
  return fallback;
}

bool StateEquivalentForKimpanel(const HostState& a, const HostState& b) {
  if (a.preedit != b.preedit || a.auxiliary != b.auxiliary ||
      a.page != b.page || a.pages != b.pages ||
      a.page_size != b.page_size || a.cursor != b.cursor ||
      a.preedit_visible != b.preedit_visible ||
      a.candidates_visible != b.candidates_visible ||
      a.candidates.size() != b.candidates.size()) {
    return false;
  }
  for (size_t i = 0; i < a.candidates.size(); ++i) {
    if (a.candidates[i].text != b.candidates[i].text ||
        a.candidates[i].label != b.candidates[i].label ||
        a.candidates[i].annotation != b.candidates[i].annotation) {
      return false;
    }
  }
  return true;
}

}  // namespace

KimpanelClient::KimpanelClient() = default;

KimpanelClient::~KimpanelClient() {
  Shutdown();
}

bool KimpanelClient::ParseMode(const std::string& value, Mode* mode) {
  if (mode == nullptr) {
    return false;
  }
  if (value == "auto") {
    *mode = Mode::kAuto;
    return true;
  }
  if (value == "on") {
    *mode = Mode::kOn;
    return true;
  }
  if (value == "off") {
    *mode = Mode::kOff;
    return true;
  }
  return false;
}

bool KimpanelClient::Init(Mode mode, EngineHost* host, std::string* error) {
  Shutdown();
  mode_ = mode;
  host_ = host;
  enabled_ = mode != Mode::kOff;
  if (!enabled_) {
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }

  std::string connect_error;
  if (!ConnectBus(&connect_error)) {
    enabled_ = mode_ != Mode::kAuto;
    attached_ = false;
    if (mode_ == Mode::kOn) {
      SetError(error, connect_error);
      return false;
    }
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }

  AddMatches();
  RefreshOwnership();
  if (attached_) {
    RegisterProperties();
    if (host_ != nullptr) {
      SendCurrentState(true);
    }
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void KimpanelClient::Shutdown() {
  if (conn_ != nullptr) {
    RemoveMatches();
    dbus_connection_unref(conn_);
    conn_ = nullptr;
  }
  enabled_ = false;
  attached_ = false;
  impanel_owned_ = false;
  impanel2_owned_ = false;
  has_last_state_ = false;
  last_state_ = HostState{};
  last_engine_id_.clear();
  last_preedit_visible_ = false;
  last_aux_visible_ = false;
  last_lookup_visible_ = false;
  spot_dirty_ = false;
  host_ = nullptr;
  mode_ = Mode::kOff;
}

void KimpanelClient::Tick() {
  if (conn_ == nullptr) {
    return;
  }
  dbus_connection_read_write(conn_, 0);
  DBusMessage* message = dbus_connection_pop_message(conn_);
  while (message != nullptr) {
    HandleSignal(message);
    dbus_message_unref(message);
    message = dbus_connection_pop_message(conn_);
  }
}

void KimpanelClient::SyncState(const HostState& state,
                               const std::string& engine_id) {
  if (!enabled_ || host_ == nullptr) {
    return;
  }
  const bool state_changed =
      !has_last_state_ || !StateEquivalentForKimpanel(last_state_, state) ||
      last_engine_id_ != engine_id;
  if (!attached_ || conn_ == nullptr) {
    last_state_ = state;
    last_engine_id_ = engine_id;
    has_last_state_ = true;
    return;
  }
  if (!state_changed) {
    return;
  }
  SendCurrentState(false);
}

bool KimpanelClient::enabled() const {
  return enabled_;
}

bool KimpanelClient::attached() const {
  return attached_;
}

KimpanelClient::Mode KimpanelClient::mode() const {
  return mode_;
}

void KimpanelClient::SetSpotRect(int x, int y, int w, int h) {
  if (spot_x_ == x && spot_y_ == y && spot_w_ == w && spot_h_ == h &&
      !spot_dirty_) {
    return;
  }
  spot_x_ = x;
  spot_y_ = y;
  spot_w_ = w;
  spot_h_ = h;
  spot_dirty_ = true;
  if (attached_ && conn_ != nullptr) {
    SendSignalInt2("UpdateSpotLocation", spot_x_, spot_y_);
    SendSignalInt4("UpdateSpotLocation2", spot_x_, spot_y_, spot_w_, spot_h_);
    CallImpanel2SetSpotRect();
    spot_dirty_ = false;
  }
}

std::string KimpanelClient::FormatProperty(const std::string& key,
                                           const std::string& label,
                                           const std::string& icon,
                                           const std::string& tip,
                                           const std::string& hint) {
  return key + ":" + label + ":" + icon + ":" + tip + ":" + hint;
}

std::string KimpanelClient::EngineProperty(const std::string& engine_id) {
  std::string label = engine_id.empty() ? "EN" : engine_id;
  if (label.size() > 8) {
    label = label.substr(0, 8);
  }
  return FormatProperty(kEnginePropertyKey, label, "input-keyboard",
                        "ChromiumOS IME", "label=" + label);
}

std::vector<std::string> KimpanelClient::BuildLookupLabels(
    const std::vector<CandidateView>& candidates) {
  std::vector<std::string> labels;
  labels.reserve(candidates.size());
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (!candidates[i].label.empty()) {
      labels.push_back(candidates[i].label);
    } else {
      labels.push_back(std::to_string(i + 1));
    }
  }
  return labels;
}

std::vector<std::string> KimpanelClient::BuildLookupTexts(
    const std::vector<CandidateView>& candidates) {
  std::vector<std::string> texts;
  texts.reserve(candidates.size());
  for (const CandidateView& candidate : candidates) {
    texts.push_back(candidate.text);
  }
  return texts;
}

std::vector<std::string> KimpanelClient::BuildLookupAttrs(
    const std::vector<CandidateView>& candidates) {
  return std::vector<std::string>(candidates.size(), std::string());
}

bool KimpanelClient::HasPrevPage(const HostState& state) {
  return state.pages > 0 && state.page > 0;
}

bool KimpanelClient::HasNextPage(const HostState& state) {
  return state.pages > 0 && state.page + 1 < state.pages;
}

bool KimpanelClient::HandleSignal(DBusMessage* message) {
  if (message == nullptr) {
    return false;
  }

  if (dbus_message_is_signal(message, kDbusInterface, "NameOwnerChanged")) {
    HandleNameOwnerChanged(message);
    return true;
  }

  if (!dbus_message_has_interface(message, kImpanelInterface) &&
      !dbus_message_is_signal(message, kImpanelInterface, "PanelCreated") &&
      !dbus_message_is_signal(message, kImpanelInterface, "PanelCreated2") &&
      !dbus_message_is_signal(message, kImpanelInterface, "SelectCandidate") &&
      !dbus_message_is_signal(message, kImpanelInterface,
                              "LookupTablePageUp") &&
      !dbus_message_is_signal(message, kImpanelInterface,
                              "LookupTablePageDown") &&
      !dbus_message_is_signal(message, kImpanelInterface, "TriggerProperty") &&
      !dbus_message_is_signal(message, kImpanelInterface, "MovePreeditCaret") &&
      !dbus_message_is_signal(message, kImpanelInterface, "ReloadConfig") &&
      !dbus_message_is_signal(message, kImpanelInterface, "Configure") &&
      !dbus_message_is_signal(message, kImpanelInterface, "Exit")) {
    return false;
  }

  if (dbus_message_is_signal(message, kImpanelInterface, "PanelCreated") ||
      dbus_message_is_signal(message, kImpanelInterface, "PanelCreated2")) {
    OnPanelCreated();
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface, "SelectCandidate")) {
    if (host_ == nullptr) {
      return true;
    }
    const int32_t index = ReadInt32Arg(message, 0, -1);
    if (index >= 0) {
      const HostState& state = host_->state();
      host_->SelectCandidate(state.page, static_cast<uint32_t>(index));
    }
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface,
                             "LookupTablePageUp")) {
    if (host_ == nullptr) {
      return true;
    }
    const HostState& state = host_->state();
    if (HasPrevPage(state)) {
      host_->SelectCandidate(state.page - 1, 0);
    }
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface,
                             "LookupTablePageDown")) {
    if (host_ == nullptr) {
      return true;
    }
    const HostState& state = host_->state();
    if (HasNextPage(state)) {
      host_->SelectCandidate(state.page + 1, 0);
    }
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface, "TriggerProperty")) {
    if (host_ == nullptr) {
      return true;
    }
    const std::string key = ReadStringArg(message, 0);
    if (key == kEnginePropertyKey) {
      const std::vector<EngineDescriptor> engines = host_->ListAvailable();
      if (!engines.empty()) {
        const std::string current = host_->engine_id();
        size_t index = 0;
        for (size_t i = 0; i < engines.size(); ++i) {
          if (engines[i].id == current) {
            index = (i + 1) % engines.size();
            break;
          }
        }
        if (current.empty()) {
          index = 0;
        }
        std::string ignored_error;
        host_->SetEngine(engines[index].id, &ignored_error);
      }
    }
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface, "MovePreeditCaret")) {
    // EngineHost currently has no direct preedit-caret API. Accept the signal
    // cleanly so Kimpanel hosts never cause crashes or noisy failures.
    return true;
  }

  if (dbus_message_is_signal(message, kImpanelInterface, "ReloadConfig") ||
      dbus_message_is_signal(message, kImpanelInterface, "Configure") ||
      dbus_message_is_signal(message, kImpanelInterface, "Exit")) {
    return true;
  }

  return false;
}

void KimpanelClient::SetAttachedForTest(bool attached) {
  UpdateAttached(attached);
}

bool KimpanelClient::ConnectBus(std::string* error) {
  DBusError dbus_error;
  dbus_error_init(&dbus_error);
  DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus_error);
  if (conn == nullptr) {
    SetError(error, dbus_error.message != nullptr
                        ? std::string(dbus_error.message)
                        : "failed to connect to session bus");
    dbus_error_free(&dbus_error);
    return false;
  }
  dbus_connection_set_exit_on_disconnect(conn, FALSE);
  conn_ = conn;
  dbus_error_free(&dbus_error);
  return true;
}

void KimpanelClient::AddMatches() {
  if (conn_ == nullptr) {
    return;
  }
  DBusError err;
  dbus_error_init(&err);
  dbus_bus_add_match(
      conn_,
      "type='signal',sender='org.freedesktop.DBus',"
      "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
      "arg0='org.kde.impanel'",
      &err);
  dbus_error_free(&err);
  dbus_error_init(&err);
  dbus_bus_add_match(
      conn_,
      "type='signal',sender='org.freedesktop.DBus',"
      "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
      "arg0='org.kde.impanel2'",
      &err);
  dbus_error_free(&err);
  dbus_error_init(&err);
  dbus_bus_add_match(conn_,
                     "type='signal',interface='org.kde.impanel',"
                     "path='/org/freedesktop/impanel'",
                     &err);
  dbus_error_free(&err);
}

void KimpanelClient::RemoveMatches() {
  if (conn_ == nullptr) {
    return;
  }
  dbus_bus_remove_match(
      conn_,
      "type='signal',sender='org.freedesktop.DBus',"
      "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
      "arg0='org.kde.impanel'",
      nullptr);
  dbus_bus_remove_match(
      conn_,
      "type='signal',sender='org.freedesktop.DBus',"
      "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
      "arg0='org.kde.impanel2'",
      nullptr);
  dbus_bus_remove_match(conn_,
                        "type='signal',interface='org.kde.impanel',"
                        "path='/org/freedesktop/impanel'",
                        nullptr);
}

void KimpanelClient::RefreshOwnership() {
  impanel_owned_ = NameHasOwner(kImpanelService);
  impanel2_owned_ = NameHasOwner(kImpanel2Service);
  UpdateAttached(impanel_owned_ || impanel2_owned_ || mode_ == Mode::kOn);
  // In "on" mode we still try to behave as attached/publish when possible;
  // actual panel visibility depends on panel presence. We keep attached_ as
  // "panel is available" for auto, and for on we attach when panel appears.
  // Recompute for on: attach only when owned.
  if (mode_ == Mode::kOn) {
    UpdateAttached(impanel_owned_ || impanel2_owned_);
  } else {
    UpdateAttached(impanel_owned_ || impanel2_owned_);
  }
}

bool KimpanelClient::NameHasOwner(const char* name) const {
  if (conn_ == nullptr || name == nullptr) {
    return false;
  }
  DBusMessage* msg = dbus_message_new_method_call(
      kDbusService, kDbusPath, kDbusInterface, "NameHasOwner");
  if (msg == nullptr) {
    return false;
  }
  const char* arg = name;
  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(msg);
    return false;
  }
  DBusError err;
  dbus_error_init(&err);
  DBusMessage* reply =
      dbus_connection_send_with_reply_and_block(conn_, msg, 250, &err);
  dbus_message_unref(msg);
  if (reply == nullptr) {
    dbus_error_free(&err);
    return false;
  }
  dbus_bool_t owned = FALSE;
  if (!dbus_message_get_args(reply, &err, DBUS_TYPE_BOOLEAN, &owned,
                             DBUS_TYPE_INVALID)) {
    dbus_error_free(&err);
    dbus_message_unref(reply);
    return false;
  }
  dbus_error_free(&err);
  dbus_message_unref(reply);
  return owned == TRUE;
}

void KimpanelClient::HandleNameOwnerChanged(DBusMessage* message) {
  const std::string name = ReadStringArg(message, 0);
  const std::string old_owner = ReadStringArg(message, 1);
  const std::string new_owner = ReadStringArg(message, 2);
  (void)old_owner;
  if (name != kImpanelService && name != kImpanel2Service) {
    return;
  }
  const bool owned = IsOwnedName(new_owner.c_str());
  if (name == kImpanelService) {
    impanel_owned_ = owned;
  }
  if (name == kImpanel2Service) {
    impanel2_owned_ = owned;
  }
  const bool now_attached = impanel_owned_ || impanel2_owned_;
  const bool was_attached = attached_;
  UpdateAttached(now_attached);
  if (now_attached && !was_attached) {
    RegisterProperties();
    SendCurrentState(true);
  }
}

void KimpanelClient::OnPanelCreated() {
  RefreshOwnership();
  if (!attached_) {
    UpdateAttached(true);
  }
  RegisterProperties();
  SendCurrentState(true);
}

void KimpanelClient::RegisterProperties() {
  if (conn_ == nullptr) {
    return;
  }
  std::vector<std::string> props;
  props.push_back(EngineProperty(host_ != nullptr ? host_->engine_id() : ""));
  DBusMessage* msg = dbus_message_new_signal(kImpanelPath, kInputMethodInterface,
                                             "RegisterProperties");
  if (msg == nullptr) {
    return;
  }
  DBusMessageIter iter;
  dbus_message_iter_init_append(msg, &iter);
  AppendStringArray(&iter, props);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
  if (host_ != nullptr) {
    SendSignalString("UpdateProperty",
                     EngineProperty(host_->engine_id()));
  }
}

void KimpanelClient::SendCurrentState(bool force) {
  if (conn_ == nullptr || !attached_) {
    return;
  }
  if (host_ == nullptr) {
    return;
  }
  const HostState& state = host_->state();
  const std::string engine_id = host_->engine_id();
  const std::string engine_prop = EngineProperty(engine_id);
  if (force || !has_last_state_ || last_engine_id_ != engine_id) {
    SendSignalString("UpdateProperty", engine_prop);
  }

  const bool preedit_visible = state.preedit_visible;
  if (force || !has_last_state_ || last_preedit_visible_ != preedit_visible ||
      state.preedit != last_state_.preedit) {
    SendSignalString2("UpdatePreeditText", state.preedit, std::string());
    SendSignalBool("ShowPreedit", preedit_visible);
  }

  const bool aux_visible = !state.auxiliary.empty();
  if (force || !has_last_state_ || last_aux_visible_ != aux_visible ||
      state.auxiliary != last_state_.auxiliary) {
    SendSignalString2("UpdateAux", state.auxiliary, std::string());
    SendSignalBool("ShowAux", aux_visible);
  }

  const bool lookup_visible = state.candidates_visible;
  if (force || !has_last_state_ || last_lookup_visible_ != lookup_visible ||
      !StateEquivalentForKimpanel(state, last_state_)) {
    SendLookupTableV1(state);
    SendSignalBool("ShowLookupTable", lookup_visible);
    CallImpanel2SetLookupTable(state);
  }

  if (spot_dirty_ || force) {
    SendSignalInt2("UpdateSpotLocation", spot_x_, spot_y_);
    SendSignalInt4("UpdateSpotLocation2", spot_x_, spot_y_, spot_w_, spot_h_);
    CallImpanel2SetSpotRect();
    spot_dirty_ = false;
  }

  last_preedit_visible_ = preedit_visible;
  last_aux_visible_ = aux_visible;
  last_lookup_visible_ = lookup_visible;
  last_state_ = state;
  last_engine_id_ = engine_id;
  has_last_state_ = true;
}

void KimpanelClient::SendSignal(const char* name) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendSignalBool(const char* name, bool value) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  dbus_bool_t v = value ? TRUE : FALSE;
  dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &v, DBUS_TYPE_INVALID);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendSignalString(const char* name,
                                      const std::string& value) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  const char* c = value.c_str();
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &c, DBUS_TYPE_INVALID);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendSignalString2(const char* name,
                                       const std::string& first,
                                       const std::string& second) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  const char* a = first.c_str();
  const char* b = second.c_str();
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &a, DBUS_TYPE_STRING, &b,
                           DBUS_TYPE_INVALID);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendSignalInt2(const char* name,
                                    int32_t first,
                                    int32_t second) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  dbus_int32_t a = first;
  dbus_int32_t b = second;
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &a, DBUS_TYPE_INT32, &b,
                           DBUS_TYPE_INVALID);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendSignalInt4(const char* name,
                                    int32_t a,
                                    int32_t b,
                                    int32_t c,
                                    int32_t d) {
  if (conn_ == nullptr || name == nullptr) {
    return;
  }
  DBusMessage* msg =
      dbus_message_new_signal(kImpanelPath, kInputMethodInterface, name);
  if (msg == nullptr) {
    return;
  }
  dbus_int32_t va = a;
  dbus_int32_t vb = b;
  dbus_int32_t vc = c;
  dbus_int32_t vd = d;
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &va, DBUS_TYPE_INT32, &vb,
                           DBUS_TYPE_INT32, &vc, DBUS_TYPE_INT32, &vd,
                           DBUS_TYPE_INVALID);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::SendLookupTableV1(const HostState& state) {
  if (conn_ == nullptr) {
    return;
  }
  DBusMessage* msg = dbus_message_new_signal(
      kImpanelPath, kInputMethodInterface, "UpdateLookupTable");
  if (msg == nullptr) {
    return;
  }
  DBusMessageIter iter;
  dbus_message_iter_init_append(msg, &iter);
  const std::vector<std::string> labels = BuildLookupLabels(state.candidates);
  const std::vector<std::string> texts = BuildLookupTexts(state.candidates);
  const std::vector<std::string> attrs = BuildLookupAttrs(state.candidates);
  AppendStringArray(&iter, labels);
  AppendStringArray(&iter, texts);
  AppendStringArray(&iter, attrs);
  dbus_bool_t has_prev = HasPrevPage(state) ? TRUE : FALSE;
  dbus_bool_t has_next = HasNextPage(state) ? TRUE : FALSE;
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &has_prev);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &has_next);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::CallImpanel2SetLookupTable(const HostState& state) {
  if (conn_ == nullptr || !impanel2_owned_) {
    return;
  }
  const std::vector<std::string> labels = BuildLookupLabels(state.candidates);
  const std::vector<std::string> texts = BuildLookupTexts(state.candidates);
  const std::vector<std::string> attrs = BuildLookupAttrs(state.candidates);
  const dbus_bool_t has_prev = HasPrevPage(state) ? TRUE : FALSE;
  const dbus_bool_t has_next = HasNextPage(state) ? TRUE : FALSE;
  const dbus_int32_t cursor = static_cast<dbus_int32_t>(state.cursor);
  const dbus_int32_t layout = 0;

  DBusMessage* msg = dbus_message_new_method_call(
      kImpanelService, kImpanelPath, kImpanel2Interface, "SetLookupTable");
  if (msg == nullptr) {
    return;
  }
  DBusMessageIter iter;
  dbus_message_iter_init_append(msg, &iter);
  AppendStringArray(&iter, labels);
  AppendStringArray(&iter, texts);
  AppendStringArray(&iter, attrs);

  DBusMessageIter bool_array;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "b", &bool_array);
  dbus_message_iter_append_basic(&bool_array, DBUS_TYPE_BOOLEAN, &has_prev);
  dbus_message_iter_append_basic(&bool_array, DBUS_TYPE_BOOLEAN, &has_next);
  dbus_message_iter_close_container(&iter, &bool_array);

  DBusMessageIter int_array;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &int_array);
  dbus_message_iter_append_basic(&int_array, DBUS_TYPE_INT32, &cursor);
  dbus_message_iter_append_basic(&int_array, DBUS_TYPE_INT32, &layout);
  dbus_message_iter_close_container(&iter, &int_array);

  dbus_message_set_no_reply(msg, TRUE);
  dbus_connection_send(conn_, msg, nullptr);
  dbus_message_unref(msg);
}

void KimpanelClient::CallImpanel2SetSpotRect() {
  if (conn_ == nullptr || !impanel2_owned_) {
    return;
  }
  {
    DBusMessage* msg = dbus_message_new_method_call(
        kImpanelService, kImpanelPath, kImpanel2Interface, "SetSpotRect");
    if (msg != nullptr) {
      dbus_int32_t x = spot_x_;
      dbus_int32_t y = spot_y_;
      dbus_int32_t w = spot_w_;
      dbus_int32_t h = spot_h_;
      dbus_message_append_args(msg, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                               DBUS_TYPE_INT32, &w, DBUS_TYPE_INT32, &h,
                               DBUS_TYPE_INVALID);
      dbus_message_set_no_reply(msg, TRUE);
      dbus_connection_send(conn_, msg, nullptr);
      dbus_message_unref(msg);
    }
  }
  {
    DBusMessage* msg = dbus_message_new_method_call(
        kImpanelService, kImpanelPath, kImpanel2Interface, "SetSpotRect2");
    if (msg != nullptr) {
      DBusMessageIter iter;
      dbus_message_iter_init_append(msg, &iter);
      dbus_int32_t x = spot_x_;
      dbus_int32_t y = spot_y_;
      dbus_int32_t w = spot_w_;
      dbus_int32_t h = spot_h_;
      double scale = 1.0;
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &x);
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &y);
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &w);
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &h);
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_DOUBLE, &scale);
      dbus_message_set_no_reply(msg, TRUE);
      dbus_connection_send(conn_, msg, nullptr);
      dbus_message_unref(msg);
    }
  }
}

void KimpanelClient::AppendStringArray(DBusMessageIter* iter,
                                       const std::vector<std::string>& values) {
  if (iter == nullptr) {
    return;
  }
  DBusMessageIter array_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array_iter);
  for (const std::string& value : values) {
    const char* c = value.c_str();
    dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &c);
  }
  dbus_message_iter_close_container(iter, &array_iter);
}

void KimpanelClient::UpdateAttached(bool attached) {
  if (attached_ == attached) {
    return;
  }
  attached_ = attached;
  if (!attached_) {
    last_preedit_visible_ = false;
    last_aux_visible_ = false;
    last_lookup_visible_ = false;
  }
}

}  // namespace cros_ime
