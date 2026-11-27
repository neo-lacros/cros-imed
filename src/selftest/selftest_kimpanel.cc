// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus/dbus.h>

#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#include "core/engine_host.h"
#include "core/kimpanel.h"
#include "selftest/registry.h"

namespace cros_ime {
namespace {

std::string ReadStringArg(DBusMessage* message, int index) {
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
  return value != nullptr ? value : "";
}

bool ReadBoolArg(DBusMessage* message, int index) {
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return false;
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return false;
    }
  }
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN) {
    return false;
  }
  dbus_bool_t value = FALSE;
  dbus_message_iter_get_basic(&iter, &value);
  return value == TRUE;
}

std::vector<std::string> ReadStringArrayArg(DBusMessage* message, int index) {
  std::vector<std::string> out;
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return out;
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return out;
    }
  }
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return out;
  }
  DBusMessageIter array_iter;
  dbus_message_iter_recurse(&iter, &array_iter);
  while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
    const char* value = nullptr;
    dbus_message_iter_get_basic(&array_iter, &value);
    out.push_back(value != nullptr ? value : "");
    if (!dbus_message_iter_next(&array_iter)) {
      break;
    }
  }
  return out;
}

std::vector<bool> ReadBoolArrayArg(DBusMessage* message, int index) {
  std::vector<bool> out;
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return out;
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return out;
    }
  }
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return out;
  }
  DBusMessageIter array_iter;
  dbus_message_iter_recurse(&iter, &array_iter);
  while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t value = FALSE;
    dbus_message_iter_get_basic(&array_iter, &value);
    out.push_back(value == TRUE);
    if (!dbus_message_iter_next(&array_iter)) {
      break;
    }
  }
  return out;
}

std::vector<int32_t> ReadInt32ArrayArg(DBusMessage* message, int index) {
  std::vector<int32_t> out;
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return out;
  }
  for (int i = 0; i < index; ++i) {
    if (!dbus_message_iter_next(&iter)) {
      return out;
    }
  }
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return out;
  }
  DBusMessageIter array_iter;
  dbus_message_iter_recurse(&iter, &array_iter);
  while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_INT32) {
    dbus_int32_t value = 0;
    dbus_message_iter_get_basic(&array_iter, &value);
    out.push_back(static_cast<int32_t>(value));
    if (!dbus_message_iter_next(&array_iter)) {
      break;
    }
  }
  return out;
}

}  // namespace

CROS_IME_SELFTEST("kimpanel protocol", 160, TestKimpanelProtocol) {
  checker.ExpectStr(
      "FormatProperty key:label:icon:tip:hint",
      KimpanelClient::FormatProperty("/CrosImed/Engine", "EN", "input-keyboard",
                                     "ChromiumOS IME", "label=EN"),
      "/CrosImed/Engine:EN:input-keyboard:ChromiumOS IME:label=EN");

  checker.ExpectStr("EngineProperty default EN",
                    KimpanelClient::EngineProperty(""), 
                    "/CrosImed/Engine:EN:input-keyboard:ChromiumOS IME:label=EN");
  checker.ExpectStr(
      "EngineProperty mozc",
      KimpanelClient::EngineProperty("mozc"),
      "/CrosImed/Engine:mozc:input-keyboard:ChromiumOS IME:label=mozc");
  checker.ExpectStr(
      "EngineProperty pinyin",
      KimpanelClient::EngineProperty("pinyin"),
      "/CrosImed/Engine:pinyin:input-keyboard:ChromiumOS IME:label=pinyin");

  KimpanelClient::Mode mode = KimpanelClient::Mode::kOff;
  checker.Expect("ParseMode auto", KimpanelClient::ParseMode("auto", &mode));
  checker.Expect("ParseMode auto value", mode == KimpanelClient::Mode::kAuto);
  checker.Expect("ParseMode on", KimpanelClient::ParseMode("on", &mode));
  checker.Expect("ParseMode on value", mode == KimpanelClient::Mode::kOn);
  checker.Expect("ParseMode off", KimpanelClient::ParseMode("off", &mode));
  checker.Expect("ParseMode off value", mode == KimpanelClient::Mode::kOff);
  checker.Expect("ParseMode rejects invalid",
                 !KimpanelClient::ParseMode("bogus", &mode));

  std::vector<CandidateView> candidates;
  candidates.push_back(CandidateView{"ni", "1", ""});
  candidates.push_back(CandidateView{"你", "2", "zh"});
  candidates.push_back(CandidateView{"泥", "", ""});

  const std::vector<std::string> labels =
      KimpanelClient::BuildLookupLabels(candidates);
  const std::vector<std::string> texts =
      KimpanelClient::BuildLookupTexts(candidates);
  const std::vector<std::string> attrs =
      KimpanelClient::BuildLookupAttrs(candidates);
  checker.ExpectInt("lookup labels size", static_cast<long long>(labels.size()), 3);
  checker.ExpectInt("lookup texts size", static_cast<long long>(texts.size()), 3);
  checker.ExpectInt("lookup attrs size", static_cast<long long>(attrs.size()), 3);
  if (labels.size() == 3) {
    checker.ExpectStr("label 1", labels[0], "1");
    checker.ExpectStr("label 2", labels[1], "2");
    checker.ExpectStr("label 3 fallback", labels[2], "3");
  }
  if (texts.size() == 3) {
    checker.ExpectStr("text 1", texts[0], "ni");
    checker.ExpectStr("text 2", texts[1], "你");
    checker.ExpectStr("text 3", texts[2], "泥");
  }

  HostState page_state;
  page_state.page = 1;
  page_state.pages = 3;
  checker.Expect("HasPrevPage true", KimpanelClient::HasPrevPage(page_state));
  checker.Expect("HasNextPage true", KimpanelClient::HasNextPage(page_state));
  page_state.page = 0;
  checker.Expect("HasPrevPage false on first", !KimpanelClient::HasPrevPage(page_state));
  page_state.page = 2;
  checker.Expect("HasNextPage false on last", !KimpanelClient::HasNextPage(page_state));

  // Encode UpdatePreeditText payload into a DBus signal and verify args.
  DBusMessage* preedit_msg = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.kimpanel.inputmethod",
      "UpdatePreeditText");
  checker.Expect("dbus preedit message created", preedit_msg != nullptr);
  if (preedit_msg != nullptr) {
    const char* text = "ni hao";
    const char* attr = "";
    dbus_message_append_args(preedit_msg, DBUS_TYPE_STRING, &text,
                             DBUS_TYPE_STRING, &attr, DBUS_TYPE_INVALID);
    checker.ExpectStr("UpdatePreeditText text arg", ReadStringArg(preedit_msg, 0),
                      "ni hao");
    checker.ExpectStr("UpdatePreeditText attr arg", ReadStringArg(preedit_msg, 1),
                      "");
    dbus_message_unref(preedit_msg);
  }

  // Encode SetLookupTable/UpdateLookupTable payload and verify encoding.
  DBusMessage* lookup_msg = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.kimpanel.inputmethod",
      "UpdateLookupTable");
  checker.Expect("dbus lookup message created", lookup_msg != nullptr);
  if (lookup_msg != nullptr) {
    DBusMessageIter iter;
    dbus_message_iter_init_append(lookup_msg, &iter);
    KimpanelClient::AppendStringArray(&iter, labels);
    KimpanelClient::AppendStringArray(&iter, texts);
    KimpanelClient::AppendStringArray(&iter, attrs);
    dbus_bool_t has_prev = TRUE;
    dbus_bool_t has_next = FALSE;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &has_prev);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &has_next);

    const std::vector<std::string> got_labels = ReadStringArrayArg(lookup_msg, 0);
    const std::vector<std::string> got_texts = ReadStringArrayArg(lookup_msg, 1);
    const std::vector<std::string> got_attrs = ReadStringArrayArg(lookup_msg, 2);
    checker.ExpectInt("encoded lookup labels", static_cast<long long>(got_labels.size()), 3);
    checker.ExpectInt("encoded lookup texts", static_cast<long long>(got_texts.size()), 3);
    checker.ExpectInt("encoded lookup attrs", static_cast<long long>(got_attrs.size()), 3);
    checker.Expect("encoded lookup has_prev", ReadBoolArg(lookup_msg, 3));
    checker.Expect("encoded lookup has_next false", !ReadBoolArg(lookup_msg, 4));
    if (got_texts.size() == 3) {
      checker.ExpectStr("encoded text utf8", got_texts[1], "你");
    }
    dbus_message_unref(lookup_msg);
  }

  // Verify SetLookupTable impanel2 method encoding shape (as, as, as, ab, ai).
  DBusMessage* set_lookup = dbus_message_new_method_call(
      "org.kde.impanel", "/org/freedesktop/impanel", "org.kde.impanel2",
      "SetLookupTable");
  checker.Expect("dbus SetLookupTable message created", set_lookup != nullptr);
  if (set_lookup != nullptr) {
    DBusMessageIter iter;
    dbus_message_iter_init_append(set_lookup, &iter);
    KimpanelClient::AppendStringArray(&iter, labels);
    KimpanelClient::AppendStringArray(&iter, texts);
    KimpanelClient::AppendStringArray(&iter, attrs);
    DBusMessageIter bool_array;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "b", &bool_array);
    dbus_bool_t has_prev = FALSE;
    dbus_bool_t has_next = TRUE;
    dbus_message_iter_append_basic(&bool_array, DBUS_TYPE_BOOLEAN, &has_prev);
    dbus_message_iter_append_basic(&bool_array, DBUS_TYPE_BOOLEAN, &has_next);
    dbus_message_iter_close_container(&iter, &bool_array);
    DBusMessageIter int_array;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &int_array);
    dbus_int32_t cursor = 2;
    dbus_int32_t layout = 0;
    dbus_message_iter_append_basic(&int_array, DBUS_TYPE_INT32, &cursor);
    dbus_message_iter_append_basic(&int_array, DBUS_TYPE_INT32, &layout);
    dbus_message_iter_close_container(&iter, &int_array);

    const std::vector<bool> got_bools = ReadBoolArrayArg(set_lookup, 3);
    const std::vector<int32_t> got_ints = ReadInt32ArrayArg(set_lookup, 4);
    checker.ExpectInt("SetLookupTable bools size",
                      static_cast<long long>(got_bools.size()), 2);
    checker.ExpectInt("SetLookupTable ints size",
                      static_cast<long long>(got_ints.size()), 2);
    if (got_bools.size() == 2) {
      checker.Expect("SetLookupTable has_prev false", !got_bools[0]);
      checker.Expect("SetLookupTable has_next true", got_bools[1]);
    }
    if (got_ints.size() == 2) {
      checker.ExpectInt("SetLookupTable cursor", got_ints[0], 2);
      checker.ExpectInt("SetLookupTable layout", got_ints[1], 0);
    }
    dbus_message_unref(set_lookup);
  }
}

CROS_IME_SELFTEST("kimpanel dispatch", 161, TestKimpanelDispatch) {
  // SelectCandidate dispatch applies to EngineHost.
  EngineHost host("", "");
  // Create a fake candidate state by using a plugin if available; otherwise
  // verify the signal is recognized and handled safely.
  KimpanelClient client;
  client.SetAttachedForTest(true);

  DBusMessage* select = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "SelectCandidate");
  checker.Expect("SelectCandidate signal created", select != nullptr);
  if (select != nullptr) {
    dbus_int32_t index = 1;
    dbus_message_append_args(select, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);
    // No host attached to client: should be handled cleanly.
    checker.Expect("SelectCandidate handled without host",
                   client.HandleSignal(select));
    dbus_message_unref(select);
  }

  // Attach a host and verify SelectCandidate is recognized.
  EngineHost host2("", "");
  KimpanelClient client2;
  std::string init_error;
  // Init off is always safe; then attach host for signal dispatch testing.
  checker.Expect("KimpanelClient Init off",
                 client2.Init(KimpanelClient::Mode::kOff, &host2, &init_error));
  checker.Expect("off mode not enabled", !client2.enabled());
  client2.SetAttachedForTest(true);

  DBusMessage* select2 = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "SelectCandidate");
  if (select2 != nullptr) {
    dbus_int32_t index = 0;
    dbus_message_append_args(select2, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);
    checker.Expect("SelectCandidate handled with host",
                   client2.HandleSignal(select2));
    dbus_message_unref(select2);
  }

  // TriggerProperty engine cycle is handled cleanly (no crash without engines).
  DBusMessage* trigger = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "TriggerProperty");
  if (trigger != nullptr) {
    const char* key = "/CrosImed/Engine";
    dbus_message_append_args(trigger, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
    checker.Expect("TriggerProperty handled", client2.HandleSignal(trigger));
    dbus_message_unref(trigger);
  }

  // PanelCreated2 should be handled cleanly and mark attached state.
  KimpanelClient client3;
  DBusMessage* created2 = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "PanelCreated2");
  if (created2 != nullptr) {
    checker.Expect("PanelCreated2 handled", client3.HandleSignal(created2));
    checker.Expect("PanelCreated2 marks attached", client3.attached());
    dbus_message_unref(created2);
  }

  DBusMessage* move_caret = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "MovePreeditCaret");
  if (move_caret != nullptr) {
    dbus_int32_t pos = 1;
    dbus_message_append_args(move_caret, DBUS_TYPE_INT32, &pos,
                             DBUS_TYPE_INVALID);
    checker.Expect("MovePreeditCaret handled cleanly",
                   client3.HandleSignal(move_caret));
    dbus_message_unref(move_caret);
  }

  DBusMessage* reload = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "ReloadConfig");
  if (reload != nullptr) {
    checker.Expect("ReloadConfig handled", client3.HandleSignal(reload));
    dbus_message_unref(reload);
  }

  DBusMessage* configure = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "Configure");
  if (configure != nullptr) {
    checker.Expect("Configure handled", client3.HandleSignal(configure));
    dbus_message_unref(configure);
  }

  DBusMessage* exit_msg = dbus_message_new_signal(
      "/org/freedesktop/impanel", "org.kde.impanel", "Exit");
  if (exit_msg != nullptr) {
    checker.Expect("Exit handled", client3.HandleSignal(exit_msg));
    dbus_message_unref(exit_msg);
  }
}

CROS_IME_SELFTEST("kimpanel lifecycle", 162, TestKimpanelLifecycle) {
  EngineHost host("", "");

  KimpanelClient off_client;
  std::string error;
  checker.Expect("Init off succeeds",
                 off_client.Init(KimpanelClient::Mode::kOff, &host, &error));
  checker.Expect("off mode disabled", !off_client.enabled());
  checker.Expect("off mode not attached", !off_client.attached());
  off_client.Tick();
  off_client.SyncState(host.state(), host.engine_id());
  off_client.Shutdown();

  KimpanelClient auto_client;
  error.clear();
  // In auto mode, Init should be tolerant when no session bus/panel is present.
  // In dbus-run-session it may connect; either way it must not crash.
  const bool auto_ok =
      auto_client.Init(KimpanelClient::Mode::kAuto, &host, &error);
  checker.Expect("Init auto does not fail hard", auto_ok);
  const char* session_bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  if (session_bus != nullptr && session_bus[0] != '\0') {
    checker.Expect("auto mode enabled with session bus", auto_client.enabled());
  } else {
    checker.Expect("auto mode init without session bus is graceful", auto_ok);
  }
  auto_client.Tick();
  auto_client.SyncState(host.state(), host.engine_id());
  auto_client.SetSpotRect(10, 20, 30, 40);
  auto_client.Shutdown();
  checker.Expect("auto shutdown leaves disabled", !auto_client.enabled());

  KimpanelClient on_client;
  error.clear();
  // "on" mode may fail without session bus in minimal environments, but in
  // dbus-run-session (used by CI) it should initialize cleanly. Accept either
  // a clean init or a clean error with no crash.
  const bool on_ok =
      on_client.Init(KimpanelClient::Mode::kOn, &host, &error);
  checker.Expect("Init on is clean (ok or graceful error)",
                 on_ok || !error.empty());
  if (on_ok) {
    on_client.Tick();
    on_client.SyncState(host.state(), host.engine_id());
    on_client.Shutdown();
  }
}
}  // namespace cros_ime
