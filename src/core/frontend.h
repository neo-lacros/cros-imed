// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_FRONTEND_H_
#define CROS_IME_CORE_FRONTEND_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cros_ime {

class EngineHost;

struct FrontendOptions {
  std::string engine;
  std::string config;
  std::string ipc_socket;
  bool verbose = false;
};

class Frontend {
 public:
  virtual ~Frontend() = default;

  virtual std::string name() const = 0;
  virtual bool Start(EngineHost* host,
                     const FrontendOptions& options,
                     std::string* error) = 0;
  virtual int Run() = 0;
  virtual void Stop() = 0;
};

void RegisterFrontend(const std::string& name,
                      std::function<std::unique_ptr<Frontend>()> factory);
std::unique_ptr<Frontend> CreateFrontend(const std::string& name);
std::vector<std::string> FrontendNames();

}  // namespace cros_ime

#endif  // CROS_IME_CORE_FRONTEND_H_
