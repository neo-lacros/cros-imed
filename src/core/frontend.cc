// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frontend.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cros_ime {
namespace {

std::map<std::string, std::function<std::unique_ptr<Frontend>()>>&
Registry() {
  static std::map<std::string, std::function<std::unique_ptr<Frontend>()>>
      registry;
  return registry;
}

}  // namespace

void RegisterFrontend(const std::string& name,
                      std::function<std::unique_ptr<Frontend>()> factory) {
  if (name.empty() || !factory) {
    return;
  }
  Registry()[name] = std::move(factory);
}

std::unique_ptr<Frontend> CreateFrontend(const std::string& name) {
  const auto it = Registry().find(name);
  if (it == Registry().end()) {
    return nullptr;
  }
  return it->second();
}

std::vector<std::string> FrontendNames() {
  std::vector<std::string> names;
  names.reserve(Registry().size());
  for (const auto& entry : Registry()) {
    names.push_back(entry.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace cros_ime
