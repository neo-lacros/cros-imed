// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/ipc.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/engine_host.h"
#include "core/utf8.h"

namespace cros_ime {
namespace {

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  bool ParseObject(std::map<std::string, JsonValue>* out) {
    SkipWhitespace();
    if (!ParseObjectValue(out)) {
      return false;
    }
    SkipWhitespace();
    if (position_ != text_.size()) {
      return Fail("trailing garbage");
    }
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  bool Fail(const std::string& message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  void SkipWhitespace() {
    while (position_ < text_.size()) {
      const char ch = text_[position_];
      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
        break;
      }
      ++position_;
    }
  }

  bool Consume(char expected) {
    if (position_ >= text_.size() || text_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool Match(const char* literal) {
    const size_t length = std::strlen(literal);
    if (text_.size() - position_ < length ||
        text_.compare(position_, length, literal) != 0) {
      return false;
    }
    position_ += length;
    return true;
  }

  bool ParseObjectValue(std::map<std::string, JsonValue>* out) {
    if (!Consume('{')) {
      return Fail("expected object");
    }
    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }

    while (true) {
      SkipWhitespace();
      std::string key;
      if (!ParseString(&key)) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return Fail("expected ':'");
      }
      JsonValue value;
      if (!ParseValue(&value)) {
        return false;
      }
      (*out)[key] = std::move(value);
      SkipWhitespace();
      if (Consume(',')) {
        continue;
      }
      if (Consume('}')) {
        return true;
      }
      return Fail("expected ',' or '}'");
    }
  }

  bool ParseValue(JsonValue* value) {
    SkipWhitespace();
    if (position_ >= text_.size()) {
      return Fail("unexpected end of input");
    }
    const char ch = text_[position_];
    if (ch == '"') {
      value->type = JsonValue::Type::String;
      return ParseString(&value->string);
    }
    if (ch == 't') {
      if (!Match("true")) {
        return Fail("invalid literal");
      }
      value->type = JsonValue::Type::Bool;
      value->boolean = true;
      return true;
    }
    if (ch == 'f') {
      if (!Match("false")) {
        return Fail("invalid literal");
      }
      value->type = JsonValue::Type::Bool;
      value->boolean = false;
      return true;
    }
    if (ch == 'n') {
      if (!Match("null")) {
        return Fail("invalid literal");
      }
      value->type = JsonValue::Type::Null;
      return true;
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
      return ParseNumber(value);
    }
    return Fail("unsupported value");
  }

  bool ParseNumber(JsonValue* value) {
    const size_t start = position_;
    if (Consume('-')) {
      // Sign consumed.
    }
    bool has_digits = false;
    while (position_ < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
      ++position_;
      has_digits = true;
    }
    if (!has_digits) {
      return Fail("invalid number");
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      bool fraction_digits = false;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
        ++position_;
        fraction_digits = true;
      }
      if (!fraction_digits) {
        return Fail("invalid number");
      }
    }
    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      bool exponent_digits = false;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
        ++position_;
        exponent_digits = true;
      }
      if (!exponent_digits) {
        return Fail("invalid number");
      }
    }

    const std::string number(text_.substr(start, position_ - start));
    value->type = JsonValue::Type::Number;
    value->number = std::strtod(number.c_str(), nullptr);
    return true;
  }

  bool ParseString(std::string* out) {
    if (!Consume('"')) {
      return Fail("expected string");
    }
    out->clear();
    while (position_ < text_.size()) {
      const unsigned char ch =
          static_cast<unsigned char>(text_[position_++]);
      if (ch == '"') {
        return true;
      }
      if (ch < 0x20) {
        return Fail("control character in string");
      }
      if (ch != '\\') {
        out->push_back(static_cast<char>(ch));
        continue;
      }
      if (position_ >= text_.size()) {
        return Fail("truncated escape");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
          out->push_back('"');
          break;
        case '\\':
          out->push_back('\\');
          break;
        case '/':
          out->push_back('/');
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'u': {
          uint32_t codepoint = 0;
          if (!ParseHex4(&codepoint)) {
            return false;
          }
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            const size_t saved = position_;
            if (position_ + 1 < text_.size() && text_[position_] == '\\' &&
                text_[position_ + 1] == 'u') {
              position_ += 2;
              uint32_t low = 0;
              if (!ParseHex4(&low)) {
                return false;
              }
              if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint =
                    0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
              } else {
                position_ = saved;
              }
            }
          }
          *out += EncodeUtf8(codepoint);
          break;
        }
        default:
          return Fail("invalid escape");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseHex4(uint32_t* value) {
    if (text_.size() - position_ < 4) {
      return Fail("truncated \\u escape");
    }
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[position_ + static_cast<size_t>(i)];
      int digit = 0;
      if (ch >= '0' && ch <= '9') {
        digit = ch - '0';
      } else if (ch >= 'a' && ch <= 'f') {
        digit = ch - 'a' + 10;
      } else if (ch >= 'A' && ch <= 'F') {
        digit = ch - 'A' + 10;
      } else {
        return Fail("invalid \\u escape");
      }
      result = result * 16 + static_cast<uint32_t>(digit);
    }
    position_ += 4;
    *value = result;
    return true;
  }

  std::string_view text_;
  size_t position_ = 0;
  std::string error_;
};

std::string MakeError(const std::string& message) {
  return "{\"ok\":false,\"error\":" + JsonQuote(message) + "}";
}

std::string JsonStringArray(const std::vector<std::string>& items) {
  std::string out = "[";
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += JsonQuote(items[i]);
  }
  out.push_back(']');
  return out;
}

std::string StateCandidatesJson(const HostState& state) {
  std::vector<std::string> texts;
  texts.reserve(state.candidates.size());
  for (const CandidateView& candidate : state.candidates) {
    texts.push_back(candidate.text);
  }
  return JsonStringArray(texts);
}

bool GetString(const std::map<std::string, JsonValue>& request,
               const std::string& key,
               std::string* out) {
  const auto it = request.find(key);
  if (it == request.end() || !it->second.IsString()) {
    return false;
  }
  *out = it->second.string;
  return true;
}

bool GetInt(const std::map<std::string, JsonValue>& request,
            const std::string& key,
            long long* out) {
  const auto it = request.find(key);
  if (it == request.end() || !it->second.IsNumber()) {
    return false;
  }
  *out = it->second.AsInt();
  return true;
}

bool KeyNameToType(const std::string& name, uint32_t* type) {
  static const std::map<std::string, uint32_t> kKeys = {
      {"backspace", CROS_IME_KEY_BACKSPACE},
      {"enter", CROS_IME_KEY_ENTER},
      {"escape", CROS_IME_KEY_ESCAPE},
      {"space", CROS_IME_KEY_SPACE},
      {"tab", CROS_IME_KEY_TAB},
      {"left", CROS_IME_KEY_LEFT},
      {"right", CROS_IME_KEY_RIGHT},
      {"up", CROS_IME_KEY_UP},
      {"down", CROS_IME_KEY_DOWN},
      {"pageup", CROS_IME_KEY_PAGE_UP},
      {"page_up", CROS_IME_KEY_PAGE_UP},
      {"pagedown", CROS_IME_KEY_PAGE_DOWN},
      {"page_down", CROS_IME_KEY_PAGE_DOWN},
      {"delete", CROS_IME_KEY_DELETE},
      {"home", CROS_IME_KEY_HOME},
      {"end", CROS_IME_KEY_END},
  };
  const auto it = kKeys.find(AsciiToLower(name));
  if (it == kKeys.end()) {
    return false;
  }
  *type = it->second;
  return true;
}

}  // namespace

std::string JsonValue::AsString(const std::string& fallback) const {
  return type == Type::String ? string : fallback;
}

long long JsonValue::AsInt(long long fallback) const {
  if (type != Type::Number) {
    return fallback;
  }
  return static_cast<long long>(number);
}

bool JsonValue::AsBool(bool fallback) const {
  return type == Type::Bool ? boolean : fallback;
}

bool ParseJsonObject(std::string_view line,
                     std::map<std::string, JsonValue>* out,
                     std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "null output";
    }
    return false;
  }
  out->clear();
  JsonParser parser(line);
  if (!parser.ParseObject(out)) {
    if (error != nullptr) {
      *error = parser.error();
    }
    return false;
  }
  return true;
}

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  static const char kHex[] = "0123456789ABCDEF";
  for (char raw : text) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out.push_back(kHex[(ch >> 4) & 0x0F]);
          out.push_back(kHex[ch & 0x0F]);
        } else {
          out.push_back(raw);
        }
        break;
    }
  }
  return out;
}

std::string JsonQuote(std::string_view text) {
  return "\"" + JsonEscape(text) + "\"";
}

std::string DefaultIpcSocketPath() {
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
    return std::string(runtime_dir) + "/cros-imed.sock";
  }
  return "/tmp/cros-imed-" + std::to_string(static_cast<long long>(getuid())) +
         ".sock";
}

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
  Stop();
}

bool IpcServer::Start(const std::string& path,
                      EngineHost* host,
                      std::string* error) {
  Stop();
  last_error_.clear();
  path_ = path.empty() ? DefaultIpcSocketPath() : path;
  host_ = host;

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    SetError("socket: " + std::string(std::strerror(errno)));
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path_.size() >= sizeof(address.sun_path)) {
    ::close(fd);
    SetError("socket path too long: " + path_);
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }
  std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);

  if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
    ::close(fd);
    SetError("cannot remove stale socket " + path_ + ": " +
             std::strerror(errno));
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    ::close(fd);
    SetError("bind " + path_ + ": " + std::strerror(errno));
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  if (::chmod(path_.c_str(), 0600) != 0) {
    const std::string reason = std::strerror(errno);
    ::close(fd);
    ::unlink(path_.c_str());
    SetError("chmod " + path_ + ": " + reason);
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  if (::listen(fd, 16) != 0) {
    const std::string reason = std::strerror(errno);
    ::close(fd);
    ::unlink(path_.c_str());
    SetError("listen " + path_ + ": " + reason);
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    const std::string reason = std::strerror(errno);
    ::close(fd);
    ::unlink(path_.c_str());
    SetError("fcntl " + path_ + ": " + reason);
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  listen_fd_ = fd;
  owns_path_ = true;
  clients_.clear();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void IpcServer::Stop() {
  for (Client& client : clients_) {
    if (client.fd >= 0) {
      ::close(client.fd);
    }
  }
  clients_.clear();
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (owns_path_ && !path_.empty()) {
    if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
      SetError("unlink " + path_ + ": " + std::strerror(errno));
    }
  }
  owns_path_ = false;
}

int IpcServer::Poll(int timeout_ms) {
  if (listen_fd_ < 0) {
    SetError("server is not running");
    return 0;
  }

  const size_t polled_clients = clients_.size();
  std::vector<pollfd> descriptors;
  descriptors.reserve(polled_clients + 1);
  descriptors.push_back(pollfd{listen_fd_, POLLIN, 0});
  for (const Client& client : clients_) {
    descriptors.push_back(
        pollfd{client.fd,
               static_cast<short>(POLLIN |
                                  (client.output.empty() ? 0 : POLLOUT)),
               0});
  }

  const int poll_result =
      ::poll(descriptors.data(), descriptors.size(), timeout_ms);
  if (poll_result < 0) {
    if (errno == EINTR) {
      return 0;
    }
    SetError("poll: " + std::string(std::strerror(errno)));
    return 0;
  }
  if (poll_result == 0) {
    return 0;
  }

  int handled = 0;
  if ((descriptors[0].revents & POLLIN) != 0) {
    while (true) {
      const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
      if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        SetError("accept: " + std::string(std::strerror(errno)));
        break;
      }
      const int flags = ::fcntl(client_fd, F_GETFL, 0);
      if (flags >= 0) {
        ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
      }
      Client client;
      client.fd = client_fd;
      clients_.push_back(std::move(client));
      ++handled;
    }
  }

  bool should_stop = false;
  for (size_t index = polled_clients; index-- > 0;) {
    Client& client = clients_[index];
    const short revents = descriptors[index + 1].revents;
    if (revents == 0) {
      continue;
    }
    if ((revents & (POLLERR | POLLNVAL)) != 0) {
      client.closing = true;
    }
    if ((revents & POLLHUP) != 0) {
      client.closing = true;
    }

    if ((revents & POLLIN) != 0) {
      char buffer[4096];
      while (true) {
        const ssize_t received =
            ::recv(client.fd, buffer, sizeof(buffer), 0);
        if (received > 0) {
          client.input.append(buffer, static_cast<size_t>(received));
          ++handled;
          continue;
        }
        if (received == 0) {
          client.closing = true;
          break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        client.closing = true;
        break;
      }
    }

    size_t newline = client.input.find('\n');
    while (newline != std::string::npos) {
      std::string line = client.input.substr(0, newline);
      client.input.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      bool quit = false;
      const std::string response = HandleRequest(line, &quit);
      if (!response.empty()) {
        client.output += response;
        client.output.push_back('\n');
      }
      ++handled;
      if (quit) {
        should_stop = true;
      }
      newline = client.input.find('\n');
    }

    while (!client.output.empty()) {
      const ssize_t sent = ::send(client.fd, client.output.data(),
                                  client.output.size(), MSG_NOSIGNAL);
      if (sent > 0) {
        client.output.erase(0, static_cast<size_t>(sent));
        continue;
      }
      if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      }
      client.closing = true;
      client.output.clear();
      break;
    }

    if (client.closing && client.output.empty()) {
      CloseClient(index);
    }
  }

  if (should_stop) {
    Stop();
  }
  return handled;
}

const std::string& IpcServer::path() const {
  return path_;
}

std::string IpcServer::last_error() const {
  return last_error_;
}

bool IpcServer::running() const {
  return listen_fd_ >= 0;
}

ClipboardRing* IpcServer::clipboard() {
  return &clipboard_;
}

void IpcServer::CloseClient(size_t index) {
  if (index >= clients_.size()) {
    return;
  }
  if (clients_[index].fd >= 0) {
    ::close(clients_[index].fd);
  }
  clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
}

void IpcServer::SetError(const std::string& error) {
  last_error_ = error;
}

std::string IpcServer::HandleRequest(const std::string& line, bool* quit) {
  if (quit == nullptr) {
    return MakeError("internal error");
  }
  *quit = false;

  std::map<std::string, JsonValue> request;
  std::string parse_error;
  if (!ParseJsonObject(line, &request, &parse_error)) {
    return MakeError("bad json");
  }

  const auto op_it = request.find("op");
  if (op_it == request.end() || !op_it->second.IsString()) {
    return MakeError("bad json");
  }
  const std::string op = op_it->second.string;

  if (op == "ping") {
    return "{\"ok\":true,\"pong\":true,\"pid\":" +
           std::to_string(static_cast<long long>(getpid())) +
           ",\"version\":\"0.1.0\"}";
  }

  if (op == "list") {
    const std::vector<EngineDescriptor> engines =
        host_ != nullptr ? host_->ListAvailable()
                         : std::vector<EngineDescriptor>();
    std::string out = "{\"ok\":true,\"engines\":[";
    for (size_t i = 0; i < engines.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      out += "{\"id\":" + JsonQuote(engines[i].id) +
             ",\"display_name\":" + JsonQuote(engines[i].display_name) +
             ",\"languages\":" + JsonQuote(engines[i].language_codes) +
             ",\"layout\":" + JsonQuote(engines[i].layout_id) + "}";
    }
    out += "],\"current\":" +
           JsonQuote(host_ != nullptr ? host_->engine_id() : std::string()) +
           "}";
    return out;
  }

  if (host_ == nullptr) {
    return MakeError("no engine host");
  }

  if (op == "engine") {
    std::string id;
    if (!GetString(request, "id", &id)) {
      return MakeError("missing id");
    }
    std::string error;
    if (!host_->SetEngine(id, &error)) {
      return MakeError(error);
    }
    return "{\"ok\":true,\"engine\":" + JsonQuote(host_->engine_id()) + "}";
  }

  if (op == "status") {
    const HostState& state = host_->state();
    return "{\"ok\":true,\"engine\":" + JsonQuote(host_->engine_id()) +
           ",\"preedit\":" + JsonQuote(state.preedit) +
           ",\"auxiliary\":" + JsonQuote(state.auxiliary) +
           ",\"page\":" + std::to_string(state.page) +
           ",\"pages\":" + std::to_string(state.pages) +
           ",\"candidates\":" + StateCandidatesJson(state) +
           ",\"clipboard\":" +
           std::to_string(static_cast<long long>(clipboard_.Items().size())) +
           "}";
  }

  if (op == "option") {
    std::string key;
    std::string value;
    if (!GetString(request, "key", &key) ||
        !GetString(request, "value", &value)) {
      return MakeError("missing key or value");
    }
    if (!host_->SetOption(key, value)) {
      return MakeError("option rejected: " + key);
    }
    return "{\"ok\":true}";
  }

  if (op == "get-option") {
    std::string key;
    if (!GetString(request, "key", &key)) {
      return MakeError("missing key");
    }
    return "{\"ok\":true,\"key\":" + JsonQuote(key) +
           ",\"value\":" + JsonQuote(host_->GetOption(key)) + "}";
  }

  if (op == "type") {
    std::string text;
    if (!GetString(request, "text", &text)) {
      return MakeError("missing text");
    }
    const std::vector<HostResult> results = host_->TypeUtf8(text);
    std::string commit;
    HostState state = host_->state();
    for (const HostResult& result : results) {
      commit += result.commit;
      state = result.state;
    }
    return "{\"ok\":true,\"preedit\":" + JsonQuote(state.preedit) +
           ",\"commit\":" + JsonQuote(commit) +
           ",\"candidates\":" + StateCandidatesJson(state) + "}";
  }

  if (op == "key") {
    uint32_t key_type = 0;
    const auto type_it = request.find("type");
    if (type_it == request.end()) {
      return MakeError("missing type");
    }
    if (type_it->second.IsNumber()) {
      key_type = static_cast<uint32_t>(type_it->second.AsInt());
    } else if (type_it->second.IsString()) {
      if (!KeyNameToType(type_it->second.string, &key_type)) {
        return MakeError("unknown key type: " + type_it->second.string);
      }
    } else {
      return MakeError("bad key type");
    }

    const HostResult result = host_->Key(EngineHost::MakeKey(key_type));
    return "{\"ok\":true,\"preedit\":" + JsonQuote(result.state.preedit) +
           ",\"commit\":" + JsonQuote(result.commit) +
           ",\"candidates\":" + StateCandidatesJson(result.state) + "}";
  }

  if (op == "select") {
    long long page = 0;
    long long index = 0;
    if (!GetInt(request, "page", &page) || !GetInt(request, "index", &index)) {
      return MakeError("missing page or index");
    }
    const HostResult result = host_->SelectCandidate(
        static_cast<uint32_t>(page), static_cast<uint32_t>(index));
    return "{\"ok\":true,\"preedit\":" + JsonQuote(result.state.preedit) +
           ",\"commit\":" + JsonQuote(result.commit) +
           ",\"candidates\":" + StateCandidatesJson(result.state) + "}";
  }

  if (op == "longpress") {
    long long codepoint = 0;
    if (!GetInt(request, "cp", &codepoint)) {
      return MakeError("missing cp");
    }
    const HostResult result =
        host_->Longpress(static_cast<uint32_t>(codepoint));
    return "{\"ok\":true,\"variants\":" +
           JsonStringArray(result.state.longpress) + "}";
  }

  if (op == "clipboard") {
    return "{\"ok\":true,\"items\":" +
           JsonStringArray(clipboard_.Items()) + "}";
  }

  if (op == "quit") {
    *quit = true;
    return "{\"ok\":true,\"bye\":true}";
  }

  return MakeError("unknown op: " + op);
}

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
  Close();
}

bool IpcClient::Connect(const std::string& path, std::string* error) {
  Close();
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "socket: " + std::string(std::strerror(errno));
    }
    return false;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    ::close(fd);
    if (error != nullptr) {
      *error = "socket path too long: " + path;
    }
    return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    if (error != nullptr) {
      *error = "connect " + path + ": " + std::strerror(errno);
    }
    ::close(fd);
    return false;
  }

  fd_ = fd;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool IpcClient::Call(const std::string& request_line,
                     std::string* response,
                     std::string* error) {
  if (fd_ < 0) {
    if (error != nullptr) {
      *error = "not connected";
    }
    return false;
  }
  if (response == nullptr) {
    if (error != nullptr) {
      *error = "null response";
    }
    return false;
  }

  std::string request = request_line;
  if (request.empty() || request.back() != '\n') {
    request.push_back('\n');
  }

  size_t sent_total = 0;
  while (sent_total < request.size()) {
    const ssize_t sent =
        ::send(fd_, request.data() + sent_total,
               request.size() - sent_total, MSG_NOSIGNAL);
    if (sent > 0) {
      sent_total += static_cast<size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    if (error != nullptr) {
      *error = "send: " + std::string(std::strerror(errno));
    }
    return false;
  }

  std::string data;
  while (true) {
    pollfd descriptor{fd_, POLLIN, 0};
    const int poll_result = ::poll(&descriptor, 1, 5000);
    if (poll_result == 0) {
      if (error != nullptr) {
        *error = "timed out waiting for response";
      }
      return false;
    }
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (error != nullptr) {
        *error = "poll: " + std::string(std::strerror(errno));
      }
      return false;
    }

    char buffer[4096];
    const ssize_t received = ::recv(fd_, buffer, sizeof(buffer), 0);
    if (received > 0) {
      data.append(buffer, static_cast<size_t>(received));
      if (data.find('\n') != std::string::npos) {
        break;
      }
      continue;
    }
    if (received == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    if (error != nullptr) {
      *error = "recv: " + std::string(std::strerror(errno));
    }
    return false;
  }

  if (data.empty()) {
    if (error != nullptr) {
      *error = "connection closed before a response";
    }
    return false;
  }

  const size_t newline = data.find('\n');
  if (newline == std::string::npos) {
    *response = data;
  } else {
    *response = data.substr(0, newline);
  }
  if (!response->empty() && response->back() == '\r') {
    response->pop_back();
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void IpcClient::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace cros_ime
