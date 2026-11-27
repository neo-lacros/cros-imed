// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Deliberately invalid plugin: it exports the ABI symbols but advertises an
// unsupported ABI version.  The registry must reject it with a diagnostic and
// must not crash.

#include "cros_ime/engine_plugin.h"

#include <cstdint>

namespace {

const CrosImeEngineInfo kBadInfo = {
    sizeof(CrosImeEngineInfo), 2, "bad", "Bad Plugin", "en-US", "us",
    "",                        "Wrong ABI version",
};

const CrosImeEngineInfo* BadGetInfo(void) {
  return &kBadInfo;
}

const CrosImeEnginePluginV1 kBadPlugin = {
    sizeof(CrosImeEnginePluginV1),
    2,
    BadGetInfo,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

extern "C" CROS_IME_EXPORT uint32_t cros_ime_plugin_abi_version(void) {
  return 2;
}

extern "C" CROS_IME_EXPORT const CrosImeEnginePluginV1* cros_ime_plugin_v1(
    void) {
  return &kBadPlugin;
}
