// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_IPC_H_
#define CROS_IME_CORE_IPC_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/assistive.h"

namespace cros_ime {

class EngineHost;

// Minimal JSON value used by the control protocol.
struct JsonValue {
  enum class Type { Null, Bool, Number, String };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;

  bool IsString() const { return type == Type::String; }
  bool IsNumber() const { return type == Type::Number; }
  bool IsBool() const { return type == Type::Bool; }

  std::string AsString(const std::string& fallback = std::string()) const;
  long long AsInt(long long fallback = 0) const;
  bool AsBool(bool fallback = false) const;
};

// Strict single-object parser.  Rejects trailing garbage and malformed
// strings/numbers.  Values may be strings, numbers, booleans or null.
bool ParseJsonObject(std::string_view line,
                     std::map<std::string, JsonValue>* out,
                     std::string* error);

// Escapes only the contents of a JSON string (no surrounding quotes).
std::string JsonEscape(std::string_view text);
// Escapes and wraps in double quotes.
std::string JsonQuote(std::string_view text);

// $XDG_RUNTIME_DIR/cros-imed.sock, or /tmp/cros-imed-<uid>.sock.
std::string DefaultIpcSocketPath();

// JSON-lines AF_UNIX control server.  One request object per line, one
// response object per line.  Not thread-safe; call Poll() from one thread.
class IpcServer {
 public:
  IpcServer();
  ~IpcServer();

  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;

  bool Start(const std::string& path, EngineHost* host, std::string* error);
  void Stop();
  int Poll(int timeout_ms);

  const std::string& path() const;
  std::string last_error() const;
  bool running() const;

  // The daemon may push commits here.  EngineHost itself stays clipboard-free.
  ClipboardRing* clipboard();

 private:
  struct Client {
    int fd = -1;
    std::string input;
    std::string output;
    bool closing = false;
  };

  std::string HandleRequest(const std::string& line, bool* quit);
  void CloseClient(size_t index);
  void SetError(const std::string& error);

  int listen_fd_ = -1;
  std::string path_;
  EngineHost* host_ = nullptr;
  std::vector<Client> clients_;
  std::string last_error_;
  ClipboardRing clipboard_;
  bool owns_path_ = false;
};

// Synchronous request/response client.  Used by cros-imectl and tests.
class IpcClient {
 public:
  IpcClient();
  ~IpcClient();

  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  bool Connect(const std::string& path, std::string* error);
  bool Call(const std::string& request_line,
            std::string* response,
            std::string* error);
  void Close();

 private:
  int fd_ = -1;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_IPC_H_
