// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/plugin_registry.h"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cros_ime {
namespace {

constexpr uint32_t kMaxStructSize = 4096;

std::string DlerrorText() {
  const char* error = dlerror();
  return error != nullptr ? std::string(error)
                          : std::string("unknown dynamic loader error");
}

void* LookupSymbol(void* handle, const char* symbol, std::string* error) {
  dlerror();
  void* address = dlsym(handle, symbol);
  if (address != nullptr) {
    return address;
  }
  const char* reason = dlerror();
  *error = std::string("missing symbol ") + symbol + ": " +
           (reason != nullptr ? reason : "not found");
  return nullptr;
}

bool IsPluginFileName(const std::string& name) {
  constexpr const char kPrefix[] = "libcros_ime_";
  constexpr const char kSuffix[] = ".so";
  const size_t prefix_len = sizeof(kPrefix) - 1;
  const size_t suffix_len = sizeof(kSuffix) - 1;
  if (name.size() <= prefix_len + suffix_len) {
    return false;
  }
  return name.compare(0, prefix_len, kPrefix) == 0 &&
         name.compare(name.size() - suffix_len, suffix_len, kSuffix) == 0;
}

bool ValidatePlugin(const CrosImeEnginePluginV1* plugin,
                    const CrosImeEngineInfo** info_out,
                    std::string* error) {
  if (plugin == nullptr) {
    *error = "plugin vtable is null";
    return false;
  }
  if (plugin->struct_size < 2 * sizeof(uint32_t) ||
      plugin->struct_size > kMaxStructSize) {
    *error = "plugin struct_size is implausible";
    return false;
  }
  if (plugin->abi_version != CROS_IME_PLUGIN_ABI_VERSION) {
    *error = "unsupported plugin ABI version " +
             std::to_string(plugin->abi_version);
    return false;
  }
  if (plugin->get_info == nullptr) {
    *error = "plugin has no get_info()";
    return false;
  }

  const CrosImeEngineInfo* info = plugin->get_info();
  if (info == nullptr) {
    *error = "get_info() returned null";
    return false;
  }
  if (info->struct_size < sizeof(uint32_t) ||
      info->struct_size > kMaxStructSize) {
    *error = "engine info struct_size is implausible";
    return false;
  }
  if (info->engine_id == nullptr || info->engine_id[0] == '\0') {
    *error = "get_info() returned an empty engine_id";
    return false;
  }

  *info_out = info;
  return true;
}

EngineDescriptor MakeDescriptor(const CrosImeEngineInfo& info,
                                const std::string& path) {
  EngineDescriptor descriptor;
  descriptor.id = info.engine_id;
  descriptor.path = path;
  descriptor.display_name =
      info.display_name != nullptr && info.display_name[0] != '\0'
          ? info.display_name
          : descriptor.id;
  descriptor.language_codes =
      info.language_codes != nullptr ? info.language_codes : "";
  descriptor.layout_id =
      info.layout_id != nullptr ? info.layout_id : "";
  descriptor.description =
      info.description != nullptr ? info.description : "";
  return descriptor;
}

void AppendSearchPath(std::vector<std::string>* paths,
                      std::set<std::string>* seen,
                      const std::string& path) {
  if (path.empty()) {
    return;
  }
  if (seen->insert(path).second) {
    paths->push_back(path);
  }
}

}  // namespace

LoadedPlugin::LoadedPlugin() = default;

LoadedPlugin::~LoadedPlugin() {
  if (handle != nullptr) {
    dlclose(handle);
    handle = nullptr;
  }
  plugin = nullptr;
}

LoadedPlugin::LoadedPlugin(LoadedPlugin&& other) noexcept
    : handle(other.handle),
      plugin(other.plugin),
      desc(std::move(other.desc)) {
  other.handle = nullptr;
  other.plugin = nullptr;
}

LoadedPlugin& LoadedPlugin::operator=(LoadedPlugin&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (handle != nullptr) {
    dlclose(handle);
  }
  handle = other.handle;
  plugin = other.plugin;
  desc = std::move(other.desc);
  other.handle = nullptr;
  other.plugin = nullptr;
  return *this;
}

std::vector<std::string> DefaultPluginSearchPaths() {
  std::vector<std::string> paths;
  std::set<std::string> seen;

  const char* environment = std::getenv("CROS_IME_PLUGIN_DIR");
  if (environment != nullptr) {
    std::string value(environment);
    size_t start = 0;
    while (start <= value.size()) {
      const size_t end = value.find(':', start);
      const std::string part =
          value.substr(start, end == std::string::npos ? std::string::npos
                                                       : end - start);
      AppendSearchPath(&paths, &seen, part);
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }

  char executable[4096];
  const ssize_t length =
      readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  if (length > 0) {
    executable[length] = '\0';
    const std::filesystem::path exe_dir =
        std::filesystem::path(executable).parent_path();
    AppendSearchPath(&paths, &seen, (exe_dir / "plugins").string());
  }

  AppendSearchPath(&paths, &seen, "./build/plugins");
  AppendSearchPath(&paths, &seen, "/usr/lib/cros-imed/plugins");
  return paths;
}

PluginRegistry::PluginRegistry(std::vector<std::string> search_paths)
    : search_paths_(std::move(search_paths)) {
  if (search_paths_.empty()) {
    search_paths_ = DefaultPluginSearchPaths();
  }
}

std::vector<EngineDescriptor> PluginRegistry::Scan() {
  engines_.clear();
  diagnostics_.clear();
  std::set<std::string> seen_ids;

  for (const std::string& directory : search_paths_) {
    std::error_code directory_error;
    std::filesystem::directory_iterator iterator(directory, directory_error);
    if (directory_error) {
      continue;  // Missing search directories are normal.
    }

    std::vector<std::string> candidates;
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
      const std::filesystem::directory_entry& entry = *iterator;
      std::error_code type_error;
      if (entry.is_regular_file(type_error) && !type_error) {
        const std::string name = entry.path().filename().string();
        if (IsPluginFileName(name)) {
          candidates.push_back(entry.path().string());
        }
      }

      // increment(ec) reports filesystem races without throwing.
      iterator.increment(type_error);
      if (type_error) {
        diagnostics_.push_back("scan " + directory + ": " +
                               type_error.message());
        break;
      }
    }
    std::sort(candidates.begin(), candidates.end());

    for (const std::string& path : candidates) {
      void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (handle == nullptr) {
        diagnostics_.push_back("skip " + path + ": " + DlerrorText());
        continue;
      }

      std::string error;
      auto abi_function = reinterpret_cast<uint32_t (*)()>(
          LookupSymbol(handle, CROS_IME_PLUGIN_ABI_SYMBOL, &error));
      if (abi_function == nullptr) {
        diagnostics_.push_back("skip " + path + ": " + error);
        dlclose(handle);
        continue;
      }
      const uint32_t abi_version = abi_function();
      if (abi_version != CROS_IME_PLUGIN_ABI_VERSION) {
        diagnostics_.push_back("skip " + path +
                               ": unsupported plugin ABI version " +
                               std::to_string(abi_version));
        dlclose(handle);
        continue;
      }

      auto plugin_function = reinterpret_cast<const CrosImeEnginePluginV1* (*)()>(
          LookupSymbol(handle, CROS_IME_PLUGIN_V1_SYMBOL, &error));
      if (plugin_function == nullptr) {
        diagnostics_.push_back("skip " + path + ": " + error);
        dlclose(handle);
        continue;
      }

      const CrosImeEnginePluginV1* plugin = plugin_function();
      const CrosImeEngineInfo* info = nullptr;
      if (!ValidatePlugin(plugin, &info, &error)) {
        diagnostics_.push_back("skip " + path + ": " + error);
        dlclose(handle);
        continue;
      }

      const std::string id = info->engine_id;
      if (seen_ids.insert(id).second) {
        engines_.push_back(MakeDescriptor(*info, path));
      }
      dlclose(handle);
    }
  }

  return engines_;
}

const EngineDescriptor* PluginRegistry::Find(const std::string& id) const {
  for (const EngineDescriptor& descriptor : engines_) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

LoadedPlugin PluginRegistry::Load(const std::string& id, std::string* error) {
  const EngineDescriptor* descriptor = Find(id);
  if (descriptor == nullptr) {
    if (error != nullptr) {
      *error = "engine '" + id + "' was not found in any plugin directory";
    }
    return LoadedPlugin();
  }

  dlerror();
  void* handle = dlopen(descriptor->path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    if (error != nullptr) {
      *error = "failed to load " + descriptor->path + ": " + DlerrorText();
    }
    return LoadedPlugin();
  }

  std::string lookup_error;
  auto abi_function = reinterpret_cast<uint32_t (*)()>(
      LookupSymbol(handle, CROS_IME_PLUGIN_ABI_SYMBOL, &lookup_error));
  auto plugin_function = reinterpret_cast<const CrosImeEnginePluginV1* (*)()>(
      LookupSymbol(handle, CROS_IME_PLUGIN_V1_SYMBOL, &lookup_error));
  if (abi_function == nullptr || plugin_function == nullptr) {
    if (error != nullptr) {
      *error = "failed to load " + descriptor->path + ": " + lookup_error;
    }
    dlclose(handle);
    return LoadedPlugin();
  }

  const uint32_t abi_version = abi_function();
  if (abi_version != CROS_IME_PLUGIN_ABI_VERSION) {
    if (error != nullptr) {
      *error = "failed to load " + descriptor->path +
               ": unsupported plugin ABI version " +
               std::to_string(abi_version);
    }
    dlclose(handle);
    return LoadedPlugin();
  }

  const CrosImeEnginePluginV1* plugin = plugin_function();
  const CrosImeEngineInfo* info = nullptr;
  std::string validation_error;
  if (!ValidatePlugin(plugin, &info, &validation_error)) {
    if (error != nullptr) {
      *error = "failed to load " + descriptor->path + ": " + validation_error;
    }
    dlclose(handle);
    return LoadedPlugin();
  }

  LoadedPlugin loaded;
  loaded.handle = handle;
  loaded.plugin = plugin;
  loaded.desc = *descriptor;
  if (error != nullptr) {
    error->clear();
  }
  return loaded;
}

const std::vector<std::string>& PluginRegistry::diagnostics() const {
  return diagnostics_;
}

const std::vector<std::string>& PluginRegistry::search_paths() const {
  return search_paths_;
}

}  // namespace cros_ime
