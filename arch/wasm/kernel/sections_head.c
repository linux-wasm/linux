// SPDX-License-Identifier: GPL-2.0
/* Start-of-section symbols. Must be linked first. See sections.h. */
#include "sections.h"

/* Order matters: this is the order INIT_CALLS has in a linker script. */
WASM_SECTION_MARK(__initcall_start,		".initcallearly.init");
WASM_SECTION_MARK(__initcall0_start,		".initcall0.init");
WASM_SECTION_MARK(__wasm_initcall0s_start,	".initcall0s.init");
WASM_SECTION_MARK(__initcall1_start,		".initcall1.init");
WASM_SECTION_MARK(__wasm_initcall1s_start,	".initcall1s.init");
WASM_SECTION_MARK(__initcall2_start,		".initcall2.init");
WASM_SECTION_MARK(__wasm_initcall2s_start,	".initcall2s.init");
WASM_SECTION_MARK(__initcall3_start,		".initcall3.init");
WASM_SECTION_MARK(__wasm_initcall3s_start,	".initcall3s.init");
WASM_SECTION_MARK(__initcall4_start,		".initcall4.init");
WASM_SECTION_MARK(__wasm_initcall4s_start,	".initcall4s.init");
WASM_SECTION_MARK(__initcall5_start,		".initcall5.init");
WASM_SECTION_MARK(__wasm_initcall5s_start,	".initcall5s.init");
WASM_SECTION_MARK(__wasm_initcallrootfs_start,	".initcallrootfs.init");
WASM_SECTION_MARK(__initcall6_start,		".initcall6.init");
WASM_SECTION_MARK(__wasm_initcall6s_start,	".initcall6s.init");
WASM_SECTION_MARK(__initcall7_start,		".initcall7.init");
WASM_SECTION_MARK(__wasm_initcall7s_start,	".initcall7s.init");

WASM_SECTION_MARK(__con_initcall_start,		".con_initcall.init");
WASM_SECTION_MARK(__setup_start,		".init.setup");

/* SCHED_DATA: the scheduler compares and walks these by address. */
WASM_SECTION_MARK(__sched_class_highest,	"__stop_sched_class");
WASM_SECTION_MARK(__wasm_dl_sched_class,	"__dl_sched_class");
WASM_SECTION_MARK(__wasm_rt_sched_class,	"__rt_sched_class");
WASM_SECTION_MARK(__wasm_fair_sched_class,	"__fair_sched_class");
WASM_SECTION_MARK(__wasm_ext_sched_class,	"__ext_sched_class");
WASM_SECTION_MARK(__wasm_idle_sched_class,	"__idle_sched_class");

WASM_SECTION_MARK(__start___ex_table,		"__ex_table");
