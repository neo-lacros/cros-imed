// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_PROTO_MODE_HOST_H_
#define CROS_IME_CORE_PROTO_MODE_HOST_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cros_ime/chromeos_shared_lib.h"

namespace cros_ime {

// Hosts a ChromeOS "1P" libimedecoder.so through the vendored ABI.
//
// The decoder library owns the ImeClientDelegate it is given and may call
// Destroy() at any time.  ProtoModeHost therefore never deletes that object
// directly; Destroy() reports back to the host first.
class ProtoModeHost {
 public:
  ProtoModeHost();
  ~ProtoModeHost();

  ProtoModeHost(const ProtoModeHost&) = delete;
  ProtoModeHost& operator=(const ProtoModeHost&) = delete;

  // Returns one entry per ABI symbol, suffixed with " (missing)" when absent.
  static std::vector<std::string> Probe(const std::string& lib_path,
                                        std::string* error);

  bool Load(const std::string& lib_path, std::string* error);
  bool Init(std::string* error);
  bool Supports(const std::string& ime_spec);
  bool Activate(const std::string& ime_spec, std::string* error);
  void Process(const uint8_t* data, size_t size);

  std::vector<std::string> TakeClientMessages();

  void Close();
  void SetBundleDir(std::string bundle_dir);
  void SetUserHomeDir(std::string user_home_dir);
  void SetFeature(const std::string& name, bool enabled);

  const std::vector<std::string>& platform_log() const;
  bool activated() const;
  int delegate_destroy_count() const;
  bool loaded() const;

 private:
  class PlatformImpl;
  class DelegateImpl;

  void RecordPlatform(const std::string& call);
  void RecordClientMessage(const uint8_t* data, size_t size);
  void OnDelegateDestroyed();

  std::unique_ptr<PlatformImpl> platform_;
  void* handle_ = nullptr;

  void (*set_logger_)(ash::ime::ChromeLoggerFunc) = nullptr;
  void (*init_proto_)(ash::ime::ImeCrosPlatform*) = nullptr;
  void (*close_proto_)() = nullptr;
  bool (*supports_)(const char*) = nullptr;
  bool (*activate_)(const char*, ash::ime::ImeClientDelegate*) = nullptr;
  void (*process_)(const uint8_t*, size_t) = nullptr;
  void (*init_mojo_)(ash::ime::ImeCrosPlatform*) = nullptr;
  void (*close_mojo_)() = nullptr;
  bool (*is_connected_)() = nullptr;

  std::string bundle_dir_;
  std::string user_home_dir_;
  std::map<std::string, bool> features_;
  std::vector<std::string> platform_log_;
  std::vector<std::string> client_messages_;
  DelegateImpl* delegate_ = nullptr;
  bool initialized_ = false;
  bool activated_ = false;
  int delegate_destroy_count_ = 0;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_PROTO_MODE_HOST_H_
