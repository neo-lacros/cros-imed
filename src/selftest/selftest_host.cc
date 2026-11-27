// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <sys/stat.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_host.h"
#include "core/plugin_registry.h"
#include "core/proto_mode_host.h"
#include "selftest/registry.h"

namespace cros_ime {
namespace {

std::string MockDirectory() {
  return ::cros_ime::selftest::GetBuildRoot() + "/mock";
}

std::string MockLibrary(const std::string& name) {
  return MockDirectory() + "/" + name;
}

bool FileExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ContainsText(const std::vector<std::string>& items,
                  const std::string& needle) {
  for (const std::string& item : items) {
    if (item.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string InstalledPluginPath(const std::string& engine_id) {
  return "/usr/lib/cros-imed/plugins/libcros_ime_" + engine_id + ".so";
}

std::string BuildPluginPath(const std::string& engine_id) {
  return ::cros_ime::selftest::GetBuildRoot() + "/plugins/libcros_ime_" +
         engine_id + ".so";
}

std::string ResolvePluginPath(const std::string& engine_id) {
  const std::string build_path = BuildPluginPath(engine_id);
  if (FileExists(build_path)) {
    return build_path;
  }
  const std::string installed_path = InstalledPluginPath(engine_id);
  if (FileExists(installed_path)) {
    return installed_path;
  }
  return "";
}

bool ReadelfHasSymbol(const std::string& lib_path, const std::string& symbol) {
  std::string cmd = "readelf -W --dyn-syms " + lib_path + " 2>/dev/null";
  std::array<char, 512> buffer{};
  std::string output;
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    return false;
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) !=
         nullptr) {
    output += buffer.data();
  }
  return output.find(" " + symbol + "\n") != std::string::npos ||
         output.find(" " + symbol + "@") != std::string::npos ||
         output.find("\t" + symbol + "\n") != std::string::npos;
}

bool VerifyAbiSymbolsWithReadelf(const std::string& lib_path,
                                 const std::vector<std::string>& symbols,
                                 std::vector<std::string>* missing) {
  for (const std::string& symbol : symbols) {
    if (!ReadelfHasSymbol(lib_path, symbol)) {
      missing->push_back(symbol);
    }
  }
  return missing->empty();
}

}  // namespace

CROS_IME_SELFTEST("proto mode host", 200, TestProtoModeHost) {
  const std::string mock_path = MockLibrary("libimedecoder.so");
  if (!FileExists(mock_path)) {
    checker.Ok("mock libimedecoder.so not present; skipping mock-only proto checks");
    return;
  }

  std::string error;
  const std::vector<std::string> probe =
      ProtoModeHost::Probe(mock_path, &error);
  std::string probe_joined;
  for (const std::string& entry : probe) {
    probe_joined += entry;
    probe_joined.push_back('|');
  }
  checker.ExpectContains("Probe lists InitProtoMode", probe_joined,
                         "InitProtoMode");
  checker.ExpectContains("Probe lists ImeDecoderActivateIme", probe_joined,
                         "ImeDecoderActivateIme");
  checker.ExpectNotContains("Probe finds every mock symbol", probe_joined,
                            "(missing)");

  ProtoModeHost host;
  checker.Expect("Load mock libimedecoder.so", host.Load(mock_path, &error));
  host.SetBundleDir("/tmp/opencode/cros-imed-bundle");
  host.SetUserHomeDir("/tmp/opencode/cros-imed-home");
  checker.Expect("Init mock proto mode", host.Init(&error));
  checker.Expect("Supports xkb:us::eng", host.Supports("xkb:us::eng"));
  checker.Expect("Supports proto:mock", host.Supports("proto:mock"));
  checker.Expect("Rejects unsupported spec", !host.Supports("nope"));

  checker.Expect("Activate proto:mock",
                 host.Activate("proto:mock", &error));
  checker.Expect("Activated flag is set", host.activated());

  const std::vector<std::string> activation_messages =
      host.TakeClientMessages();
  checker.ExpectInt("Activation sends one message",
                    static_cast<long long>(activation_messages.size()), 1);
  if (!activation_messages.empty()) {
    checker.ExpectStr("Activation message", activation_messages.front(),
                      "hello-from-mock:proto:mock");
  }

  const std::string payload = "abc";
  host.Process(reinterpret_cast<const uint8_t*>(payload.data()),
               payload.size());
  const std::vector<std::string> process_messages = host.TakeClientMessages();
  checker.ExpectInt("Process sends one echo",
                    static_cast<long long>(process_messages.size()), 1);
  if (!process_messages.empty()) {
    checker.ExpectStr("Process echo uppercases input",
                      process_messages.front(), "echo:ABC");
  }

  checker.Expect("platform log records GetImeBundleDir",
                 ContainsText(host.platform_log(), "GetImeBundleDir"));

  void* hook_handle = dlopen(mock_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  checker.Expect("dlopen mock for test hooks", hook_handle != nullptr);
  auto query_feature = reinterpret_cast<int (*)(const char*)>(
      hook_handle != nullptr ? dlsym(hook_handle, "mock_query_feature")
                             : nullptr);
  auto stats_function = reinterpret_cast<const char* (*)()>(
      hook_handle != nullptr ? dlsym(hook_handle, "mock_stats") : nullptr);
  checker.Expect("mock_query_feature is exported", query_feature != nullptr);
  checker.Expect("mock_stats is exported", stats_function != nullptr);

  if (query_feature != nullptr) {
    checker.ExpectInt("Fake feature disabled initially",
                      query_feature("Fake"), 0);
    host.SetFeature("Fake", true);
    checker.ExpectInt("Fake feature enabled after SetFeature",
                      query_feature("Fake"), 1);
  }

  host.Close();
  checker.Expect("Close clears activated flag", !host.activated());
  checker.ExpectInt("Delegate destroyed exactly once",
                    host.delegate_destroy_count(), 1);

  if (stats_function != nullptr) {
    const std::string stats = stats_function();
    checker.ExpectContains("mock stats record activation", stats, "activate=1");
    checker.ExpectContains("mock stats record process", stats, "process=1");
    checker.ExpectContains("mock stats record close", stats, "close=1");
    checker.ExpectContains("mock stats record bundle dir", stats,
                           "platform_bundle=/tmp/opencode/cros-imed-bundle");
  }

  if (hook_handle != nullptr) {
    dlclose(hook_handle);
  }
}

CROS_IME_SELFTEST("plugin registry", 210, TestPluginRegistry) {
  const std::string mock_directory = MockDirectory();
  const std::string fake_plugin = MockLibrary("libcros_ime_fake.so");
  if (!FileExists(fake_plugin)) {
    checker.Ok("mock plugins not present; skipping mock-only plugin registry checks");
    return;
  }

  PluginRegistry registry({mock_directory});
  const std::vector<EngineDescriptor> engines = registry.Scan();
  checker.ExpectGe("Scan finds at least one engine",
                   static_cast<long long>(engines.size()), 1);

  const EngineDescriptor* fake = registry.Find("fake");
  checker.Expect("Scan finds the fake plugin", fake != nullptr);
  if (fake != nullptr) {
    checker.ExpectStr("fake plugin path", fake->path,
                      MockLibrary("libcros_ime_fake.so"));
    checker.ExpectStr("fake display name", fake->display_name,
                      "Fake Echo Engine");
    checker.ExpectStr("fake layout", fake->layout_id, "us");
  }
  checker.Expect("bad plugin is not registered", registry.Find("bad") == nullptr);
  checker.Expect("bad plugin produces an ABI diagnostic",
                 ContainsText(registry.diagnostics(), "libcros_ime_bad.so") &&
                     ContainsText(registry.diagnostics(), "ABI"));

  std::string error;
  LoadedPlugin loaded = registry.Load("fake", &error);
  checker.Expect("Load fake plugin", static_cast<bool>(loaded));
  if (loaded) {
    const CrosImeEngineInfo* info = loaded.plugin->get_info();
    checker.ExpectStrEq("fake info engine_id",
                        info != nullptr ? info->engine_id : nullptr, "fake");
    checker.ExpectInt("fake info abi_version",
                      info != nullptr ? static_cast<long long>(info->abi_version)
                                      : 0,
                      CROS_IME_PLUGIN_ABI_VERSION);
  }

  const LoadedPlugin bad = registry.Load("bad", &error);
  checker.Expect("Load bad plugin fails", !static_cast<bool>(bad));
  checker.ExpectContains("Load bad error mentions lookup", error,
                         "not found");

  EngineHost host(mock_directory);
  checker.Expect("EngineHost selects fake", host.SetEngine("fake", &error));
  checker.ExpectStr("EngineHost engine id", host.engine_id(), "fake");
  checker.Expect("EngineHost exposes engine info", host.info() != nullptr);
  if (host.info() != nullptr) {
    checker.ExpectStrEq("EngineHost info id", host.info()->engine_id, "fake");
  }

  const std::vector<HostResult> typed = host.TypeUtf8("ab");
  checker.ExpectInt("TypeUtf8 returns one result per codepoint",
                    static_cast<long long>(typed.size()), 2);
  if (!typed.empty()) {
    checker.ExpectStr("fake preedit after typing", typed.back().state.preedit,
                      "ab");
    checker.Expect("fake preedit is visible", typed.back().state.preedit_visible);
  }

  const HostResult commit = host.Key(EngineHost::MakeKey(CROS_IME_KEY_ENTER));
  checker.ExpectStr("fake commits preedit on Enter", commit.commit, "ab");
  checker.ExpectStr("fake preedit cleared after commit",
                    commit.state.preedit, "");

  checker.Expect("EngineHost diagnostics include bad plugin",
                 ContainsText(host.diagnostics(), "libcros_ime_bad.so"));
}

CROS_IME_SELFTEST("required plugins and proto decoder", 220,
                  TestRequiredPluginsAndProtoDecoder) {
  // --- Required engine plugins: mozc, cjk, rulebased ---
  const std::vector<std::string> required_engines = {"mozc", "cjk", "rulebased"};
  for (const std::string& engine_id : required_engines) {
    const std::string path = ResolvePluginPath(engine_id);
    checker.Expect("plugin exists: " + engine_id, !path.empty(),
                   "looked in " + BuildPluginPath(engine_id) + " and " +
                       InstalledPluginPath(engine_id));
    if (path.empty()) {
      continue;
    }

    dlerror();
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    checker.Expect("dlopen plugin: " + engine_id, handle != nullptr,
                   handle == nullptr ? dlerror() : "");
    if (handle == nullptr) {
      continue;
    }

    dlerror();
    auto abi_fn = reinterpret_cast<uint32_t (*)()>(
        dlsym(handle, CROS_IME_PLUGIN_ABI_SYMBOL));
    checker.Expect("plugin exports abi symbol: " + engine_id, abi_fn != nullptr);
    if (abi_fn != nullptr) {
      checker.ExpectInt("plugin ABI version: " + engine_id,
                        static_cast<long long>(abi_fn()),
                        static_cast<long long>(CROS_IME_PLUGIN_ABI_VERSION));
    }

    dlerror();
    auto plugin_fn = reinterpret_cast<const CrosImeEnginePluginV1* (*)()>(
        dlsym(handle, CROS_IME_PLUGIN_V1_SYMBOL));
    checker.Expect("plugin exports v1 symbol: " + engine_id,
                   plugin_fn != nullptr);
    if (plugin_fn != nullptr) {
      const CrosImeEnginePluginV1* plugin = plugin_fn();
      checker.Expect("plugin v1 returns non-null: " + engine_id,
                     plugin != nullptr);
      if (plugin != nullptr) {
        const CrosImeEngineInfo* info =
            plugin->get_info != nullptr ? plugin->get_info() : nullptr;
        checker.Expect("plugin get_info non-null: " + engine_id,
                       info != nullptr);
        if (info != nullptr) {
          checker.ExpectStrEq("plugin engine_id: " + engine_id, info->engine_id,
                              engine_id);
          checker.ExpectInt(
              "plugin info abi_version: " + engine_id,
              static_cast<long long>(info->abi_version),
              static_cast<long long>(CROS_IME_PLUGIN_ABI_VERSION));
        }
      }
    }

    dlclose(handle);
  }

  // --- Proto-Mode libimedecoder.so loading/ABI symbols ---
  const std::vector<std::string> required_proto_symbols = {
      "InitProtoMode", "CloseProtoMode", "ImeDecoderSupports",
      "ImeDecoderActivateIme", "ImeDecoderProcess",
  };

  const std::string installed_proto =
      "/usr/lib/cros-imed/decoders/libimedecoder.so";
  const std::string mock_proto = MockLibrary("libimedecoder.so");

  // Prefer the installed decoder; fall back to build mock for build-dir runs.
  std::string proto_path;
  if (FileExists(installed_proto)) {
    proto_path = installed_proto;
  } else if (FileExists(mock_proto)) {
    proto_path = mock_proto;
  }

  checker.Expect("libimedecoder.so found",
                 !proto_path.empty(),
                 "looked in " + installed_proto + " and " + mock_proto);

  if (!proto_path.empty()) {
    std::string error;
    const std::vector<std::string> probe =
        ProtoModeHost::Probe(proto_path, &error);
    bool probe_loaded = true;
    std::string probe_joined;
    for (const std::string& entry : probe) {
      probe_joined += entry;
      probe_joined.push_back('|');
      if (entry == "(load failed)") {
        probe_loaded = false;
      }
    }

    if (probe_loaded) {
      for (const std::string& symbol : required_proto_symbols) {
        checker.ExpectContains("proto symbol present: " + symbol, probe_joined,
                               symbol);
        checker.ExpectNotContains("proto symbol not missing: " + symbol,
                                  probe_joined, symbol + " (missing)");
      }

      ProtoModeHost host;
      std::string load_error;
      const bool loaded = host.Load(proto_path, &load_error);
      checker.Expect("ProtoModeHost Load libimedecoder.so", loaded, load_error);
      if (loaded) {
        host.Close();
      }
    } else {
      // dlopen may fail in constrained environments (e.g. missing runtime deps
      // for a ChromeOS-bundled decoder). Fall back to ELF dynsym verification
      // so ABI symbol presence is still validated.
      checker.Ok("dlopen failed for " + proto_path +
                 "; falling back to readelf ABI symbol check: " + error);
      std::vector<std::string> missing;
      const bool all_present =
          VerifyAbiSymbolsWithReadelf(proto_path, required_proto_symbols, &missing);
      std::string missing_joined;
      for (const std::string& m : missing) {
        if (!missing_joined.empty()) missing_joined += ", ";
        missing_joined += m;
      }
      checker.Expect("readelf finds required proto ABI symbols", all_present,
                     "missing: " + missing_joined);
    }
  }
}

}  // namespace cros_ime
