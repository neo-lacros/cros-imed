// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_CORE_COMPOSE_H_
#define CROS_IME_CORE_COMPOSE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cros_ime {

struct ComposeResult {
  bool matched = false;
  bool pending = false;
  std::string text;
  std::string sequence;
};

// XKB-style compose engine.  It loads the system Compose file (or the file
// named by CROS_IME_COMPOSE_FILE) and always merges a small built-in fallback
// table so dead keys keep working when no system file is available.
class XkbComposeEngine {
 public:
  static XkbComposeEngine LoadDefault();
  static XkbComposeEngine LoadFromFile(const std::string& path);

  XkbComposeEngine();
  ~XkbComposeEngine();
  XkbComposeEngine(XkbComposeEngine&&) noexcept;
  XkbComposeEngine& operator=(XkbComposeEngine&&) noexcept;
  XkbComposeEngine(const XkbComposeEngine&) = delete;
  XkbComposeEngine& operator=(const XkbComposeEngine&) = delete;

  void Reset();
  ComposeResult Feed(const std::string& keysym_name);
  ComposeResult FeedCodepoint(uint32_t codepoint);

  size_t TableSize() const;
  bool loaded_from_file() const;
  const std::string& source_path() const;

 private:
  struct Node {
    std::map<std::string, std::unique_ptr<Node>> children;
    bool has_text = false;
    std::string text;
  };

  bool ParseFile(const std::string& path);
  bool ParseLine(const std::string& line);
  void InsertSequence(const std::vector<std::string>& keys,
                      const std::string& text,
                      bool only_if_absent);
  void MergeBuiltinFallback();
  std::string KeysymNameForCodepoint(uint32_t codepoint) const;

  std::unique_ptr<Node> root_;
  Node* current_ = nullptr;
  std::vector<std::string> current_keys_;
  size_t terminal_count_ = 0;
  size_t node_count_ = 0;
  bool loaded_from_file_ = false;
  std::string source_path_;
  std::map<uint32_t, std::string> codepoint_to_name_;
};

}  // namespace cros_ime

#endif  // CROS_IME_CORE_COMPOSE_H_
