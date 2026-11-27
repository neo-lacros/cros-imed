// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fallback entry point used only when no src/cli/*.cc sources exist yet.
// Stage 3 adds the real cros-imectl, at which point the Makefile stops linking
// this stub.

#include <iostream>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::cerr << "cros-imectl: CLI not built in this build\n";
  return 2;
}
