// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stable C ABI for cros-imed open-source engine plugins.
//
// Every plugin is a shared object named libcros_ime_<id>.so that exports the
// two symbols below.  The host dlopen()s the object with RTLD_NOW|RTLD_LOCAL,
// validates the ABI version, then keeps the returned vtable for its lifetime.
//
// Rules for plugin authors:
//   * This header must remain valid C (C11) and C++ (C++20).
//   * Never put C++ types, STL containers or exceptions across this boundary.
//   * All strings are NUL-terminated UTF-8.
//   * The caller zero-initialises CrosImeOutput and sets struct_size; an engine
//     fills only the fields it understands and sets the matching flag bits.
//   * Every `const char*` returned inside CrosImeOutput is owned by the engine
//     and stays valid only until the next call into that same engine.  Hosts
//     must copy anything they intend to retain.
//   * Candidate page numbers are 1-based.  A page number of 0 means "unknown".
//   * Offsets and lengths are in Unicode codepoints unless the field name ends
//     in `_utf8`.
//   * UINT32_MAX in preedit_sel_start/preedit_sel_end means "no selection".

#ifndef CROS_IME_ENGINE_PLUGIN_H_
#define CROS_IME_ENGINE_PLUGIN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROS_IME_PLUGIN_ABI_VERSION 1u

#define CROS_IME_PLUGIN_V1_SYMBOL "cros_ime_plugin_v1"
#define CROS_IME_PLUGIN_ABI_SYMBOL "cros_ime_plugin_abi_version"

#if defined(__GNUC__) || defined(__clang__)
#define CROS_IME_EXPORT __attribute__((visibility("default")))
#else
#define CROS_IME_EXPORT
#endif

typedef enum CrosImeKeyType {
  CROS_IME_KEY_NONE = 0,
  CROS_IME_KEY_CHAR = 1,
  CROS_IME_KEY_BACKSPACE = 2,
  CROS_IME_KEY_ENTER = 3,
  CROS_IME_KEY_ESCAPE = 4,
  CROS_IME_KEY_SPACE = 5,
  CROS_IME_KEY_TAB = 6,
  CROS_IME_KEY_LEFT = 7,
  CROS_IME_KEY_RIGHT = 8,
  CROS_IME_KEY_UP = 9,
  CROS_IME_KEY_DOWN = 10,
  CROS_IME_KEY_PAGE_UP = 11,
  CROS_IME_KEY_PAGE_DOWN = 12,
  CROS_IME_KEY_DELETE = 13,
  CROS_IME_KEY_HOME = 14,
  CROS_IME_KEY_END = 15,
  CROS_IME_KEY_F1 = 16,
  CROS_IME_KEY_F2 = 17,
  CROS_IME_KEY_F3 = 18,
  CROS_IME_KEY_F4 = 19,
  CROS_IME_KEY_F5 = 20,
  CROS_IME_KEY_F6 = 21,
  CROS_IME_KEY_F7 = 22,
  CROS_IME_KEY_F8 = 23,
  CROS_IME_KEY_F9 = 24,
  CROS_IME_KEY_F10 = 25,
  CROS_IME_KEY_F11 = 26,
  CROS_IME_KEY_F12 = 27
} CrosImeKeyType;

// Modifier bit field carried in CrosImeKeyEvent.modifiers.
#define CROS_IME_MOD_SHIFT (1u << 0)
#define CROS_IME_MOD_CONTROL (1u << 1)
#define CROS_IME_MOD_ALT (1u << 2)
#define CROS_IME_MOD_SUPER (1u << 3)
#define CROS_IME_MOD_CAPS_LOCK (1u << 4)
#define CROS_IME_MOD_NUM_LOCK (1u << 5)

typedef struct CrosImeKeyEvent {
  uint32_t struct_size;       // sizeof(CrosImeKeyEvent) as seen by the caller.
  uint32_t type;              // One of CrosImeKeyType.
  uint32_t codepoint;         // Valid when type == CROS_IME_KEY_CHAR.
  uint32_t modifiers;        // CROS_IME_MOD_* bit mask.
  uint64_t keysym;            // X11 keysym when known, otherwise 0.
  uint64_t time_ms;           // Monotonic event timestamp in milliseconds.
  uint32_t hardware_keycode;  // Raw hardware keycode when known, else 0.
  uint32_t reserved;          // Must be zero.
} CrosImeKeyEvent;

typedef enum CrosImeResultFlags {
  CROS_IME_RESULT_CONSUMED = 1u << 0,
  CROS_IME_RESULT_PREEDIT_CHANGED = 1u << 1,
  CROS_IME_RESULT_CANDIDATES_CHANGED = 1u << 2,
  CROS_IME_RESULT_AUXILIARY_CHANGED = 1u << 3,
  CROS_IME_RESULT_HAS_COMMIT = 1u << 4,
  CROS_IME_RESULT_DELETE_SURROUNDING = 1u << 5,
  CROS_IME_RESULT_SHOW_LONGPRESS = 1u << 6,
  CROS_IME_RESULT_HIDE_LONGPRESS = 1u << 7,
  CROS_IME_RESULT_BEEP = 1u << 8,
  CROS_IME_RESULT_RESET = 1u << 9
} CrosImeResultFlags;

typedef struct CrosImeCandidate {
  const char* text_utf8;        // Required.
  const char* label_utf8;       // Optional, may be NULL.
  const char* annotation_utf8;  // Optional, may be NULL.
} CrosImeCandidate;

// Input-method mode flags reported by the engine.
#define CROS_IME_MODE_LETTERS_ONLY (1u << 0)
#define CROS_IME_MODE_ASCII (1u << 1)
#define CROS_IME_MODE_KATAKANA (1u << 2)
#define CROS_IME_MODE_HALF_WIDTH (1u << 3)

typedef struct CrosImeOutput {
  uint32_t struct_size;  // Caller sets this to sizeof(CrosImeOutput).

  uint32_t flags;  // CrosImeResultFlags bit mask.

  // Commit text.  Only honoured when CROS_IME_RESULT_HAS_COMMIT is set.
  const char* commit_utf8;

  // Surrounding-text deletion, honoured when CROS_IME_RESULT_DELETE_SURROUNDING
  // is set.  Counts are in Unicode codepoints.
  uint32_t delete_before_cursor;
  uint32_t delete_after_cursor;

  // Preedit text and codepoint offsets.  UINT32_MAX selections mean "none".
  const char* preedit_utf8;
  uint32_t preedit_cursor;
  uint32_t preedit_sel_start;
  uint32_t preedit_sel_end;

  const char* auxiliary_utf8;

  // Candidate list.  page is 1-based; page 0 means "unknown".
  uint32_t candidate_count;
  uint32_t candidate_page;
  uint32_t candidate_pages;
  uint32_t candidate_page_size;
  uint32_t candidate_cursor;
  const CrosImeCandidate* candidates;

  uint32_t mode_flags;  // CROS_IME_MODE_* bit mask.
} CrosImeOutput;

typedef struct CrosImeEngineInfo {
  uint32_t struct_size;      // sizeof(CrosImeEngineInfo).
  uint32_t abi_version;      // CROS_IME_PLUGIN_ABI_VERSION.
  const char* engine_id;     // Stable identifier, e.g. "rulebased".
  const char* display_name;  // Human readable name.
  const char* language_codes;  // Comma separated BCP-47 tags.
  const char* layout_id;       // Default layout, e.g. "us".
  const char* icon_name;       // Icon theme name, may be NULL.
  const char* description;     // Free-form description, may be NULL.
} CrosImeEngineInfo;

typedef struct CrosImeEngine CrosImeEngine;

typedef struct CrosImeEnginePluginV1 {
  uint32_t struct_size;  // sizeof(CrosImeEnginePluginV1).
  uint32_t abi_version;  // CROS_IME_PLUGIN_ABI_VERSION.

  const CrosImeEngineInfo* (*get_info)(void);
  CrosImeEngine* (*create)(const char* config);
  void (*destroy)(CrosImeEngine*);

  bool (*set_option)(CrosImeEngine*, const char* key, const char* value);
  const char* (*get_option)(CrosImeEngine*, const char* key);

  void (*reset)(CrosImeEngine*);
  void (*focus_in)(CrosImeEngine*, const char* context_id);
  void (*focus_out)(CrosImeEngine*);
  void (*set_surrounding_text)(CrosImeEngine*,
                               const char* before_cursor_utf8,
                               const char* selected_utf8);

  uint32_t (*process_key)(CrosImeEngine*,
                          const CrosImeKeyEvent*,
                          CrosImeOutput*);
  uint32_t (*select_candidate)(CrosImeEngine*,
                               uint32_t page,
                               uint32_t index,
                               CrosImeOutput*);
  uint32_t (*candidate_action)(CrosImeEngine*,
                               uint32_t action,
                               uint32_t arg,
                               CrosImeOutput*);

  // Writes at most `max` UTF-8 strings into `out_texts`, sets `out_count`,
  // and returns the number of variants written (may be 0).
  uint32_t (*longpress_query)(CrosImeEngine*,
                              uint32_t base_codepoint,
                              const char** out_texts,
                              uint32_t max,
                              uint32_t* out_count);

  // Optional JSON description of the engine's keyboard layout.
  const char* (*layout_json)(CrosImeEngine*);
} CrosImeEnginePluginV1;

// Values for CrosImeEnginePluginV1.candidate_action().
#define CROS_IME_CANDIDATE_NEXT_PAGE 0u
#define CROS_IME_CANDIDATE_PREV_PAGE 1u
#define CROS_IME_CANDIDATE_PICK 2u
#define CROS_IME_CANDIDATE_COMMIT_SELECTED 3u
#define CROS_IME_CANDIDATE_SET_PAGE 4u
#define CROS_IME_CANDIDATE_CANCEL 5u

// Every plugin must export these two symbols.
CROS_IME_EXPORT const CrosImeEnginePluginV1* cros_ime_plugin_v1(void);
CROS_IME_EXPORT uint32_t cros_ime_plugin_abi_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CROS_IME_ENGINE_PLUGIN_H_
