# Copyright 2026 The cros-imed Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CXX ?= clang++
CC ?= clang
INSTALL ?= install

PREFIX ?= /usr
DESTDIR ?=

CXXFLAGS ?= -O2
CXXFLAGS += -std=c++20 -Wall -Wextra -finput-charset=UTF-8
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra

CPPFLAGS += -Iinclude -Isrc -Ibuild/proto

# ---------------------------------------------------------------------------
# Dependency discovery.  A missing pkg-config module is a hard error: every
# frontend and the wayland-scanner output depend on these headers.
# ---------------------------------------------------------------------------
WAYLAND_FOUND := $(shell pkg-config --exists wayland-client && echo yes)
ifeq ($(strip $(WAYLAND_FOUND)),)
$(error pkg-config module 'wayland-client' was not found; install wayland-client)
endif
DBUS_FOUND := $(shell pkg-config --exists dbus-1 && echo yes)
ifeq ($(strip $(DBUS_FOUND)),)
$(error pkg-config module 'dbus-1' was not found; install dbus)
endif

WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client)
WAYLAND_LIBS := $(shell pkg-config --libs wayland-client)
DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1)
DBUS_LIBS := $(shell pkg-config --libs dbus-1)

# Optional UI deps.  Guarded so a machine without them still builds the core.
UI_PKG_FOUND := $(shell pkg-config --exists freetype2 fontconfig && echo yes)
UI_PKG_CFLAGS := $(if $(UI_PKG_FOUND),$(shell pkg-config --cflags freetype2 fontconfig))
UI_PKG_LIBS := $(if $(UI_PKG_FOUND),$(shell pkg-config --libs freetype2 fontconfig))

CPPFLAGS += $(UI_PKG_CFLAGS)

PLUGIN_CXXFLAGS := -fPIC -fvisibility=hidden

# ---------------------------------------------------------------------------
# Sources.  Wildcards mean later stages can add files without touching this
# Makefile.
# ---------------------------------------------------------------------------
SRC_CORE := $(wildcard src/core/*.cc)
SRC_DAEMON := src/daemon/main.cc
SRC_SELFTEST := $(wildcard src/selftest/*.cc)
SRC_FRONTEND := $(wildcard src/frontend/*.cc)
SRC_UI := $(wildcard src/ui/*.cc)
SRC_CLI := $(wildcard src/cli/*.cc)

obj_for = $(patsubst src/%.cc,build/obj/%.o,$(1))

CORE_OBJS := $(call obj_for,$(SRC_CORE))
DAEMON_OBJS := $(call obj_for,$(SRC_DAEMON))
SELFTEST_OBJS := $(call obj_for,$(SRC_SELFTEST))
FRONTEND_OBJS := $(call obj_for,$(SRC_FRONTEND))
UI_OBJS := $(call obj_for,$(SRC_UI))
CLI_OBJS := $(call obj_for,$(SRC_CLI))

# Stage 1 has no UI/CLI sources yet.  Build-and-link a small stub instead so
# `all` stays green; stage 3's real sources automatically replace the stubs.
CLI_STUB :=
UI_STUB :=
ifeq ($(strip $(SRC_CLI)),)
CLI_STUB := build/obj/stubs/cli_stub.o
endif
ifeq ($(strip $(SRC_UI)),)
UI_STUB := build/obj/stubs/ui_stub.o
endif

PLUGINS := $(patsubst src/plugins/plugin_%.cc,build/plugins/libcros_ime_%.so,$(wildcard src/plugins/plugin_*.cc))
MOCK_LIBS := build/mock/libimedecoder.so build/mock/libcros_ime_fake.so build/mock/libcros_ime_bad.so

ALL_CXX_OBJS := $(CORE_OBJS) $(DAEMON_OBJS) $(SELFTEST_OBJS) $(FRONTEND_OBJS) \
                $(UI_OBJS) $(CLI_OBJS) $(CLI_STUB) $(UI_STUB)
DEPS := $(patsubst %.o,%.d,$(ALL_CXX_OBJS))

# ---------------------------------------------------------------------------
# Wayland protocol generation (needed by frontends/UI in later stages).
# ---------------------------------------------------------------------------
PROTO_STEMS := input-method-unstable-v2 virtual-keyboard-unstable-v1
PROTO_HEADERS := $(addprefix build/proto/,$(addsuffix -client-protocol.h,$(PROTO_STEMS)))
PROTO_CODES := $(addprefix build/proto/,$(addsuffix -protocol.c,$(PROTO_STEMS)))
PROTO_GEN := $(PROTO_HEADERS) $(PROTO_CODES)
PROTO_OBJS := $(patsubst build/proto/%.c,build/obj/proto/%.o,$(PROTO_CODES))

# ---------------------------------------------------------------------------
# Build rules.
# ---------------------------------------------------------------------------
all: build/cros-imed build/cros-imectl build/cros-imed-ui $(PLUGINS) $(MOCK_LIBS) $(PROTO_GEN) $(PROTO_OBJS)

build/obj/%.o: src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(WAYLAND_CFLAGS) $(DBUS_CFLAGS) -MMD -MP -c -o $@ $<

build/obj/stubs/%.o: tests/stubs/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

build/obj/proto/%.o: build/proto/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -Ibuild/proto -c -o $@ $<

build/proto/%-client-protocol.h: protocols/%.xml
	@mkdir -p build/proto
	wayland-scanner client-header $< $@

build/proto/%-protocol.c: protocols/%.xml
	@mkdir -p build/proto
	wayland-scanner private-code $< $@

build/plugins/libcros_ime_%.so: src/plugins/plugin_%.cc
	@mkdir -p build/plugins
	$(CXX) $(CXXFLAGS) $(PLUGIN_CXXFLAGS) $(CPPFLAGS) -shared -o $@ $< -Wl,--no-undefined

build/mock/libimedecoder.so: tests/mock/libimedecoder_mock.cc
	@mkdir -p build/mock
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -shared -fPIC -o $@ $<

build/mock/libcros_ime_fake.so: tests/mock/fake_plugin.cc
	@mkdir -p build/mock
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -shared -fPIC -o $@ $<

build/mock/libcros_ime_bad.so: tests/mock/bad_plugin.cc
	@mkdir -p build/mock
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -shared -fPIC -o $@ $<

build/cros-imed: $(CORE_OBJS) $(DAEMON_OBJS) $(SELFTEST_OBJS) $(FRONTEND_OBJS) $(PROTO_OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(WAYLAND_LIBS) $(DBUS_LIBS) -ldl -lpthread -lm

build/cros-imectl: $(CORE_OBJS) $(CLI_OBJS) $(CLI_STUB)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(DBUS_LIBS) -ldl

build/cros-imed-ui: $(CORE_OBJS) $(UI_OBJS) $(UI_STUB) $(PROTO_OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(WAYLAND_LIBS) $(DBUS_LIBS) $(UI_PKG_LIBS) -ldl

test: all
	./build/cros-imed --self-test

# Optional IBus component descriptor: honoured only when the file exists, at
# Makefile parse time.
INSTALL_EXTRA :=
ifneq ($(wildcard share/ibus/component/cros-imed.xml),)
INSTALL_EXTRA := $(INSTALL) -Dm644 share/ibus/component/cros-imed.xml $(DESTDIR)$(PREFIX)/share/ibus/component/cros-imed.xml
endif

install: all
	$(INSTALL) -Dm755 build/cros-imed $(DESTDIR)$(PREFIX)/bin/cros-imed
	$(INSTALL) -Dm755 build/cros-imectl $(DESTDIR)$(PREFIX)/bin/cros-imectl
	$(INSTALL) -Dm755 build/cros-imed-ui $(DESTDIR)$(PREFIX)/bin/cros-imed-ui
	@for plugin in $(PLUGINS); do \
		$(INSTALL) -Dm755 "$$plugin" "$(DESTDIR)$(PREFIX)/lib/cros-imed/plugins/$$(basename $$plugin)"; \
	done
	$(INSTALL) -Dm644 include/cros_ime/engine_plugin.h $(DESTDIR)$(PREFIX)/include/cros-imed/engine_plugin.h
	$(INSTALL) -Dm644 systemd/cros-imed.service $(DESTDIR)$(PREFIX)/lib/systemd/user/cros-imed.service
	$(INSTALL) -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/cros-imed/LICENSE
	$(INSTALL_EXTRA)

clean:
	rm -rf build

-include $(DEPS)

.PHONY: all test install clean
