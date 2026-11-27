// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Mock ChromeOS libimedecoder.so used by ProtoModeHost self-tests.  It
// implements the full vendored C ABI and two test-only introspection hooks.

#include "cros_ime/chromeos_shared_lib.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

ash::ime::ImeCrosPlatform* g_platform = nullptr;
ash::ime::ImeClientDelegate* g_delegate = nullptr;
std::string g_bundle_dir;
int g_supports_calls = 0;
int g_activate_calls = 0;
int g_process_calls = 0;
int g_close_calls = 0;
int g_feature_calls = 0;

void SendToClient(const std::string& message) {
  if (g_delegate == nullptr) {
    return;
  }
  g_delegate->Process(reinterpret_cast<const uint8_t*>(message.data()),
                      message.size());
}

}  // namespace

extern "C" {

void SetImeEngineLogger(ash::ime::ChromeLoggerFunc logger_func) {
  (void)logger_func;
}

void InitProtoMode(ash::ime::ImeCrosPlatform* platform) {
  g_platform = platform;
  if (platform != nullptr) {
    const char* bundle = platform->GetImeBundleDir();
    g_bundle_dir = bundle != nullptr ? bundle : "";
  }
}

void CloseProtoMode() {
  ++g_close_calls;
  g_delegate = nullptr;  // The host owns delegate destruction.
}

bool ImeDecoderSupports(const char* ime_spec) {
  ++g_supports_calls;
  if (ime_spec == nullptr) {
    return false;
  }
  const std::string spec(ime_spec);
  return spec == "xkb:us::eng" || spec.rfind("proto:", 0) == 0;
}

bool ImeDecoderActivateIme(const char* ime_spec,
                           ash::ime::ImeClientDelegate* delegate) {
  ++g_activate_calls;
  g_delegate = delegate;
  const std::string spec = ime_spec != nullptr ? ime_spec : "";
  SendToClient("hello-from-mock:" + spec);
  return true;
}

void ImeDecoderProcess(const uint8_t* data, size_t size) {
  ++g_process_calls;
  if (data == nullptr || size == 0) {
    return;
  }
  std::string text(reinterpret_cast<const char*>(data), size);
  for (char& ch : text) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  SendToClient("echo:" + text);
}

void InitMojoMode(ash::ime::ImeCrosPlatform* platform) {
  g_platform = platform;
}

void CloseMojoMode() {}

bool InitializeConnectionFactory(uint32_t receiver_connection_factory_handle) {
  (void)receiver_connection_factory_handle;
  return false;
}

bool InitializeConnectionFactoryV2(
    uintptr_t receiver_connection_factory_handle) {
  (void)receiver_connection_factory_handle;
  return false;
}

bool IsInputMethodConnected() {
  return false;
}

void DeleteSerializedProto(C_SerializedProto proto) {
  (void)proto;
}

void InitUserDataService(ash::ime::ImeCrosPlatform* platform) {
  g_platform = platform;
}

C_SerializedProto ProcessUserDataRequest(C_SerializedProto args) {
  (void)args;
  return C_SerializedProto{nullptr, 0};
}

// Test-only hooks.  They intentionally are part of this mock library and never
// installed anywhere.
const char* mock_stats(void) {
  static std::string stats;
  stats = "supports=" + std::to_string(g_supports_calls) +
          ";activate=" + std::to_string(g_activate_calls) +
          ";process=" + std::to_string(g_process_calls) +
          ";close=" + std::to_string(g_close_calls) +
          ";platform_bundle=" + g_bundle_dir +
          ";feature_calls=" + std::to_string(g_feature_calls);
  return stats.c_str();
}

int mock_query_feature(const char* feature_name) {
  ++g_feature_calls;
  if (g_platform == nullptr || feature_name == nullptr) {
    return 0;
  }
  return g_platform->IsFeatureEnabled(feature_name) ? 1 : 0;
}

}  // extern "C"
