// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_PLUGIN_REGISTRY_H_
#define CROS_IME_CORE_PLUGIN_REGISTRY_H_

#include <string>
#include <vector>

#include "cros_ime/engine_plugin.h"

namespace cros_ime {

struct EngineDescriptor {
  std::string id;
  std::string path;
  std::string display_name;
  std::string language_codes;
  std::string layout_id;
  std::string description;
};

// Owns a dlopen() handle and the plugin vtable.  Destroy the engine instance
// created from `plugin` before this object goes away.
struct LoadedPlugin {
  void* handle = nullptr;
  const CrosImeEnginePluginV1* plugin = nullptr;
  EngineDescriptor desc;

  LoadedPlugin();
  ~LoadedPlugin();

  LoadedPlugin(LoadedPlugin&& other) noexcept;
  LoadedPlugin& operator=(LoadedPlugin&& other) noexcept;
  LoadedPlugin(const LoadedPlugin&) = delete;
  LoadedPlugin& operator=(const LoadedPlugin&) = delete;

  explicit operator bool() const { return handle != nullptr && plugin != nullptr; }
};

// Default search order used when the caller does not provide directories.
std::vector<std::string> DefaultPluginSearchPaths();

class PluginRegistry {
 public:
  explicit PluginRegistry(std::vector<std::string> search_paths);

  // Scans every search path.  Bad libraries are skipped and recorded in
  // diagnostics(); this never throws or crashes.
  std::vector<EngineDescriptor> Scan();

  const EngineDescriptor* Find(const std::string& id) const;

  // Loads the plugin for `id`.  On failure returns an empty LoadedPlugin and
  // writes a path-qualified error including dlerror() output.
  LoadedPlugin Load(const std::string& id, std::string* error);

  const std::vector<std::string>& diagnostics() const;
  const std::vector<std::string>& search_paths() const;

 private:
  std::vector<std::string> search_paths_;
  std::vector<EngineDescriptor> engines_;
  std::vector<std::string> diagnostics_;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_PLUGIN_REGISTRY_H_
