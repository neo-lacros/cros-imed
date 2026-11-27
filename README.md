# cros-imed

A ChromiumOS-style input-method daemon for Arch Linux. It hosts ChromeOS 1P
proto-mode decoders (`libimedecoder.so`) and open-source engine plugins, and
exposes the same composition core to Wayland IMv2, IBus D-Bus and XIM/CLI
frontends plus a Material 3 popup UI.

Stage 1 provides the shared ABI headers, the UTF-8/text/compose/assistive core,
the plugin registry, the engine host, the proto-mode host and the self-test
framework. Frontends, the UI and the three OSS plugins arrive in later stages.

## Build

```sh
make -j8 all
```

Requires clang++ (or g++) with C++20, `wayland-client`, `dbus-1`, `wayland-scanner`
and `xmllint` (protocols are already vendored under `protocols/`).

## Run

```sh
./build/cros-imed --engine rulebased
./build/cros-imed --engine fake --plugin-dir build/mock --ipc-socket /tmp/cros-imed.sock
```

The daemon serves a JSON-lines control socket while `--frontend none` (the
default). Request frontends explicitly with `--frontend wayland|ibus|xim` once
they are built.

## Self-test

```sh
make test
# or
./build/cros-imed --self-test
```

The test suite covers UTF-8 validation, the text buffer, Vietnamese composition,
the XKB compose engine, assistive tables, the clipboard ring, JSON/IPC helpers,
the mock proto-mode decoder and the plugin registry.

## Layout

```
include/cros_ime/   stable plugin ABI + ChromeOS shared-lib ABI mirror
protocols/          vendored Wayland protocol XML
src/core/           UTF-8, text buffer, compose, assistive, registry,
                    engine host, proto-mode host, IPC, frontend registry
src/daemon/         cros-imed entry point and self-test driver
src/selftest/       registration framework and test suites
tests/mock/         mock libimedecoder.so and sample plugins
tests/stubs/        temporary CLI/UI entry points for stage 1
```
