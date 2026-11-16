// SPDX-License-Identifier: GPL-2.0
/* End-of-section symbols. Must be linked last. See sections.h. */
#include "sections.h"

WASM_SECTION_MARK(__initcall_end,		".initcall7s.init");
WASM_SECTION_MARK(__con_initcall_end,		".con_initcall.init");
WASM_SECTION_MARK(__setup_end,			".init.setup");
WASM_SECTION_MARK(__sched_class_lowest,		"__idle_sched_class");
WASM_SECTION_MARK(__stop___ex_table,		"__ex_table");
