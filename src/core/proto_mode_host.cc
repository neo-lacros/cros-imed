// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/proto_mode_host.h"

#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cros_ime {
namespace {

std::string DlerrorText() {
  const char* error = dlerror();
  return error != nullptr ? std::string(error)
                          : std::string("unknown dynamic loader error");
}

void* Lookup(void* handle, const char* symbol) {
  dlerror();
  return dlsym(handle, symbol);
}

}  // namespace

class ProtoModeHost::PlatformImpl final : public ash::ime::ImeCrosPlatform {
 public:
  explicit PlatformImpl(ProtoModeHost* host) : host_(host) {}
  ~PlatformImpl() override = default;

  const char* GetImeBundleDir() override {
    host_->RecordPlatform("GetImeBundleDir");
    return host_->bundle_dir_.c_str();
  }

  void Unused3() override {}

  const char* GetImeUserHomeDir() override {
    host_->RecordPlatform("GetImeUserHomeDir");
    return host_->user_home_dir_.c_str();
  }

  void Unused1() override {}
  void Unused2() override {}

  void RunInMainSequence(ash::ime::ImeSequencedTask task, int task_id) override {
    host_->RecordPlatform("RunInMainSequence");
    if (task != nullptr) {
      task(task_id);
    }
  }

  bool IsFeatureEnabled(const char* feature_name) override {
    const std::string name = feature_name != nullptr ? feature_name : "";
    host_->RecordPlatform("IsFeatureEnabled:" + name);
    const auto it = host_->features_.find(name);
    return it != host_->features_.end() && it->second;
  }

  int SimpleDownloadToFileV2(
      const char* url,
      const char* file_path,
      ash::ime::SimpleDownloadCallbackV2 callback) override {
    (void)url;
    (void)file_path;
    (void)callback;
    host_->RecordPlatform("SimpleDownloadToFileV2");
    return -1;
  }

  const void* Unused4() override { return nullptr; }

  const char* GetFieldTrialParamValueByFeature(
      const char* feature_name,
      const char* param_name) override {
    (void)feature_name;
    (void)param_name;
    host_->RecordPlatform("GetFieldTrialParamValueByFeature");
    return "";
  }

  const MojoSystemThunks2* GetMojoSystemThunks2() override {
    host_->RecordPlatform("GetMojoSystemThunks2");
    return nullptr;
  }

 private:
  ProtoModeHost* host_;
};

class ProtoModeHost::DelegateImpl final : public ash::ime::ImeClientDelegate {
 public:
  explicit DelegateImpl(ProtoModeHost* host) : host_(host) {}

  void Unused1() override {}

  void Process(const uint8_t* data, size_t size) override {
    if (host_ != nullptr && data != nullptr && size > 0) {
      host_->RecordClientMessage(data, size);
    }
  }

  void Destroy() override {
    ProtoModeHost* host = host_;
    host_ = nullptr;
    if (host != nullptr) {
      host->OnDelegateDestroyed();
    }
    delete this;
  }

 private:
  ~DelegateImpl() override = default;

  ProtoModeHost* host_;
};

ProtoModeHost::ProtoModeHost() = default;

ProtoModeHost::~ProtoModeHost() {
  Close();
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
  platform_.reset();
}

std::vector<std::string> ProtoModeHost::Probe(const std::string& lib_path,
                                               std::string* error) {
  std::vector<std::string> results;
  dlerror();
  void* handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    if (error != nullptr) {
      *error = "failed to load " + lib_path + ": " + DlerrorText();
    }
    results.push_back("(load failed)");
    return results;
  }

  static const char* const kRequired[] = {
      "SetImeEngineLogger", "InitProtoMode",     "CloseProtoMode",
      "ImeDecoderSupports", "ImeDecoderActivateIme", "ImeDecoderProcess",
  };
  static const char* const kOptional[] = {
      "InitMojoMode",
      "CloseMojoMode",
      "IsInputMethodConnected",
  };

  for (const char* symbol : kRequired) {
    if (Lookup(handle, symbol) != nullptr) {
      results.push_back(symbol);
    } else {
      results.push_back(std::string(symbol) + " (missing)");
    }
  }
  for (const char* symbol : kOptional) {
    if (Lookup(handle, symbol) != nullptr) {
      results.push_back(std::string("optional:") + symbol);
    } else {
      results.push_back(std::string("optional:") + symbol + " (missing)");
    }
  }

  dlclose(handle);
  if (error != nullptr) {
    error->clear();
  }
  return results;
}

bool ProtoModeHost::Load(const std::string& lib_path, std::string* error) {
  Close();
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
  platform_.reset();

  dlerror();
  void* handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    if (error != nullptr) {
      *error = "failed to load " + lib_path + ": " + DlerrorText();
    }
    return false;
  }

  set_logger_ = reinterpret_cast<void (*)(ash::ime::ChromeLoggerFunc)>(
      Lookup(handle, "SetImeEngineLogger"));
  init_proto_ = reinterpret_cast<void (*)(ash::ime::ImeCrosPlatform*)>(
      Lookup(handle, "InitProtoMode"));
  close_proto_ =
      reinterpret_cast<void (*)()>(Lookup(handle, "CloseProtoMode"));
  supports_ = reinterpret_cast<bool (*)(const char*)>(
      Lookup(handle, "ImeDecoderSupports"));
  activate_ = reinterpret_cast<bool (*)(const char*,
                                        ash::ime::ImeClientDelegate*)>(
      Lookup(handle, "ImeDecoderActivateIme"));
  process_ = reinterpret_cast<void (*)(const uint8_t*, size_t)>(
      Lookup(handle, "ImeDecoderProcess"));

  init_mojo_ = reinterpret_cast<void (*)(ash::ime::ImeCrosPlatform*)>(
      Lookup(handle, "InitMojoMode"));
  close_mojo_ =
      reinterpret_cast<void (*)()>(Lookup(handle, "CloseMojoMode"));
  is_connected_ =
      reinterpret_cast<bool (*)()>(Lookup(handle, "IsInputMethodConnected"));

  std::vector<std::string> missing;
  if (init_proto_ == nullptr) {
    missing.push_back("InitProtoMode");
  }
  if (close_proto_ == nullptr) {
    missing.push_back("CloseProtoMode");
  }
  if (supports_ == nullptr) {
    missing.push_back("ImeDecoderSupports");
  }
  if (activate_ == nullptr) {
    missing.push_back("ImeDecoderActivateIme");
  }
  if (process_ == nullptr) {
    missing.push_back("ImeDecoderProcess");
  }
  if (!missing.empty()) {
    std::string message = "failed to load " + lib_path + ": missing symbols";
    for (const std::string& symbol : missing) {
      message += " " + symbol;
    }
    const std::string dlerror_detail = DlerrorText();
    message += " (" + dlerror_detail + ")";
    if (error != nullptr) {
      *error = message;
    }
    dlclose(handle);
    set_logger_ = nullptr;
    init_proto_ = nullptr;
    close_proto_ = nullptr;
    supports_ = nullptr;
    activate_ = nullptr;
    process_ = nullptr;
    init_mojo_ = nullptr;
    close_mojo_ = nullptr;
    is_connected_ = nullptr;
    return false;
  }

  handle_ = handle;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool ProtoModeHost::Init(std::string* error) {
  if (handle_ == nullptr || init_proto_ == nullptr) {
    if (error != nullptr) {
      *error = "no decoder library is loaded";
    }
    return false;
  }
  if (initialized_) {
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }

  if (platform_ == nullptr) {
    platform_ = std::make_unique<PlatformImpl>(this);
  }
  if (set_logger_ != nullptr) {
    set_logger_(nullptr);
  }
  init_proto_(platform_.get());
  initialized_ = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool ProtoModeHost::Supports(const std::string& ime_spec) {
  if (handle_ == nullptr || supports_ == nullptr) {
    return false;
  }
  return supports_(ime_spec.c_str());
}

bool ProtoModeHost::Activate(const std::string& ime_spec, std::string* error) {
  if (handle_ == nullptr || activate_ == nullptr) {
    if (error != nullptr) {
      *error = "no decoder library is loaded";
    }
    return false;
  }

  if (delegate_ != nullptr) {
    delegate_->Destroy();
  }
  delegate_ = new DelegateImpl(this);

  if (!activate_(ime_spec.c_str(), delegate_)) {
    if (error != nullptr) {
      *error = "ImeDecoderActivateIme rejected '" + ime_spec + "'";
    }
    if (delegate_ != nullptr) {
      delegate_->Destroy();
    }
    activated_ = false;
    return false;
  }

  activated_ = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void ProtoModeHost::Process(const uint8_t* data, size_t size) {
  if (handle_ == nullptr || process_ == nullptr) {
    return;
  }
  if (size > 0 && data == nullptr) {
    return;
  }
  process_(data, size);
}

std::vector<std::string> ProtoModeHost::TakeClientMessages() {
  std::vector<std::string> messages = std::move(client_messages_);
  client_messages_.clear();
  return messages;
}

void ProtoModeHost::Close() {
  if (handle_ != nullptr && close_proto_ != nullptr && initialized_) {
    close_proto_();
  }
  initialized_ = false;
  if (delegate_ != nullptr) {
    delegate_->Destroy();
  }
  activated_ = false;
}

void ProtoModeHost::SetBundleDir(std::string bundle_dir) {
  bundle_dir_ = std::move(bundle_dir);
}

void ProtoModeHost::SetUserHomeDir(std::string user_home_dir) {
  user_home_dir_ = std::move(user_home_dir);
}

void ProtoModeHost::SetFeature(const std::string& name, bool enabled) {
  features_[name] = enabled;
}

const std::vector<std::string>& ProtoModeHost::platform_log() const {
  return platform_log_;
}

bool ProtoModeHost::activated() const {
  return activated_;
}

int ProtoModeHost::delegate_destroy_count() const {
  return delegate_destroy_count_;
}

bool ProtoModeHost::loaded() const {
  return handle_ != nullptr;
}

void ProtoModeHost::RecordPlatform(const std::string& call) {
  platform_log_.push_back(call);
}

void ProtoModeHost::RecordClientMessage(const uint8_t* data, size_t size) {
  client_messages_.emplace_back(reinterpret_cast<const char*>(data), size);
}

void ProtoModeHost::OnDelegateDestroyed() {
  delegate_ = nullptr;
  ++delegate_destroy_count_;
}

}  // namespace cros_ime
