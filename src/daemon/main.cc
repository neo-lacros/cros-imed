// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/engine_host.h"
#include "core/frontend.h"
#include "core/ipc.h"
#include "core/kimpanel.h"
#include "cros_ime/engine_plugin.h"
#include "selftest/registry.h"

namespace {

volatile sig_atomic_t g_stop_requested = 0;

void HandleSignal(int signal_number) {
  (void)signal_number;
  g_stop_requested = 1;
}

std::string ExecutableDirectory() {
  char buffer[4096];
  const ssize_t length =
      readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return std::string();
  }
  buffer[length] = '\0';
  return std::filesystem::path(buffer).parent_path().string();
}

int RunSelfTests() {
  std::string build_root = ExecutableDirectory();
  if (build_root.empty()) {
    build_root = "./build";
  }
  ::cros_ime::selftest::SetBuildRoot(build_root);

  std::cout << "cros-imed self-test (plugin ABI "
            << CROS_IME_PLUGIN_ABI_VERSION << ")\n";

  ::cros_ime::selftest::Checker checker;
  for (const ::cros_ime::selftest::Registration& registration :
       ::cros_ime::selftest::Registrations()) {
    checker.Section(registration.name);
    registration.fn(checker);
  }
  checker.Finish();

  const int total = checker.passed() + checker.failed();
  if (checker.failed() == 0) {
    std::cout << "PASS " << checker.passed() << "/" << total << " checks\n";
    return 0;
  }

  std::cout << "FAIL " << checker.failed() << "/" << total << " checks\n";
  for (const std::string& failure : checker.failures()) {
    std::cout << "  " << failure << '\n';
  }
  return 1;
}

void PrintUsage(std::ostream& out) {
  out << "Usage: cros-imed [options]\n"
         "\n"
         "Options:\n"
         "  --engine ID            select an engine (default: rulebased)\n"
         "  --plugin-dir DIR       add a plugin search directory (repeatable)\n"
         "  --frontend NAME        none|wayland|ibus|xim (default: none)\n"
         "  --ibus                 short for --frontend ibus\n"
         "  --xim                  short for --frontend xim\n"
         "  --ipc-socket PATH      control socket path\n"
         "  --config k=v[,k=v]     engine configuration\n"
         "  --kimpanel MODE        auto|on|off (default: auto)\n"
         "  --self-test            run built-in self-tests and exit\n"
         "  --version              print version and exit\n"
         "  --help, -h             print this help\n"
         "  -v, --verbose          verbose diagnostics\n";
}

struct Options {
  std::string engine = "rulebased";
  std::vector<std::string> plugin_dirs;
  std::string frontend = "none";
  std::string ipc_socket;
  std::string config;
  std::string kimpanel = "auto";
  bool self_test = false;
  bool show_version = false;
  bool show_help = false;
  bool verbose = false;
};

}  // namespace

int main(int argc, char** argv) {
  Options options;

  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    std::string inline_value;
    bool has_inline_value = false;
    const size_t equals = argument.find('=');
    if (equals != std::string::npos) {
      inline_value = argument.substr(equals + 1);
      argument = argument.substr(0, equals);
      has_inline_value = true;
    }

    const auto value_for = [&](const std::string& flag) -> std::string {
      if (has_inline_value) {
        return inline_value;
      }
      if (index + 1 >= argc) {
        std::cerr << "cros-imed: " << flag << " requires a value\n";
        std::exit(2);
      }
      return std::string(argv[++index]);
    };

    if (argument == "--engine") {
      options.engine = value_for(argument);
    } else if (argument == "--plugin-dir") {
      options.plugin_dirs.push_back(value_for(argument));
    } else if (argument == "--frontend") {
      options.frontend = value_for(argument);
    } else if (argument == "--ibus") {
      options.frontend = "ibus";
    } else if (argument == "--xim") {
      options.frontend = "xim";
    } else if (argument == "--ipc-socket") {
      options.ipc_socket = value_for(argument);
    } else if (argument == "--config") {
      options.config = value_for(argument);
    } else if (argument == "--kimpanel") {
      options.kimpanel = value_for(argument);
    } else if (argument == "--self-test") {
      options.self_test = true;
    } else if (argument == "--version") {
      options.show_version = true;
    } else if (argument == "--help" || argument == "-h") {
      options.show_help = true;
    } else if (argument == "--verbose" || argument == "-v") {
      options.verbose = true;
    } else {
      std::cerr << "cros-imed: unknown option '" << argument << "'\n";
      return 2;
    }
  }

  if (options.show_help) {
    PrintUsage(std::cout);
    return 0;
  }
  if (options.show_version) {
    std::cout << "cros-imed 0.1.0 (plugin ABI " << CROS_IME_PLUGIN_ABI_VERSION
              << ")\n";
    return 0;
  }
  if (options.self_test) {
    return RunSelfTests();
  }

  struct sigaction action {};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  std::string plugin_dir;
  for (const std::string& directory : options.plugin_dirs) {
    if (!plugin_dir.empty()) {
      plugin_dir.push_back(':');
    }
    plugin_dir += directory;
  }

  ::cros_ime::EngineHost host(plugin_dir, options.config);
  std::string error;
  if (!options.engine.empty() &&
      !host.SetEngine(options.engine, &error)) {
    std::cerr << "cros-imed: engine '" << options.engine
              << "' unavailable: " << error << '\n';
  }

  const std::string socket_path = options.ipc_socket.empty()
                                      ? ::cros_ime::DefaultIpcSocketPath()
                                      : options.ipc_socket;

  ::cros_ime::KimpanelClient::Mode kimpanel_mode =
      ::cros_ime::KimpanelClient::Mode::kAuto;
  if (!::cros_ime::KimpanelClient::ParseMode(options.kimpanel,
                                             &kimpanel_mode)) {
    std::cerr << "cros-imed: invalid --kimpanel mode '" << options.kimpanel
              << "' (expected auto|on|off)\n";
    return 2;
  }
  ::cros_ime::KimpanelClient kimpanel;
  std::string kimpanel_error;
  if (!kimpanel.Init(kimpanel_mode, &host, &kimpanel_error)) {
    std::cerr << "cros-imed: kimpanel init failed: " << kimpanel_error
              << '\n';
  } else if (options.verbose && kimpanel.enabled()) {
    std::cerr << "cros-imed: kimpanel mode=" << options.kimpanel << '\n';
  }

  if (options.frontend != "none") {
    std::unique_ptr<::cros_ime::Frontend> frontend =
        ::cros_ime::CreateFrontend(options.frontend);
    if (!frontend) {
      std::cerr << "cros-imed: frontend '" << options.frontend
                << "' not built in this build\n";
      kimpanel.Shutdown();
      return 2;
    }

    ::cros_ime::FrontendOptions frontend_options;
    frontend_options.engine = options.engine;
    frontend_options.config = options.config;
    frontend_options.ipc_socket = socket_path;
    frontend_options.verbose = options.verbose;

    if (!frontend->Start(&host, frontend_options, &error)) {
      std::cerr << "cros-imed: frontend '" << options.frontend
                << "' failed to start: " << error << '\n';
      kimpanel.Shutdown();
      return 1;
    }
    const int result = frontend->Run();
    frontend->Stop();
    kimpanel.Shutdown();
    return result;
  }

  ::cros_ime::IpcServer server;
  if (!server.Start(socket_path, &host, &error)) {
    std::cerr << "cros-imed: IPC server failed: " << error << '\n';
    kimpanel.Shutdown();
    return 1;
  }
  if (options.verbose) {
    std::cerr << "cros-imed: listening on " << server.path() << '\n';
    for (const std::string& diagnostic : host.diagnostics()) {
      std::cerr << "cros-imed: " << diagnostic << '\n';
    }
  }

  while (g_stop_requested == 0 && server.running()) {
    const int timeout_ms = kimpanel.enabled() ? 50 : 500;
    server.Poll(timeout_ms);
    kimpanel.Tick();
    kimpanel.SyncState(host.state(), host.engine_id());
  }
  server.Stop();
  kimpanel.Shutdown();
  return 0;
}
