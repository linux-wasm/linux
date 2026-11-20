#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Verify, from the wasm-ld link map, the layout assumptions made by
# arch/wasm/kernel/sections_head.c:
#
#  - every listed section exists as its own output segment
#  - the sections of a group are adjacent and in the listed order, with no
#    padding between them (generic code walks straight from one into the
#    next: initcall levels, scheduler classes)
#
# Usage: check-sections.sh vmlinux.map

map="$1"

awk '
# Portable hex parsing: awk implementations disagree on "0x..." strings.
function hex(str,    i, c, v) {
	v = 0
	for (i = 1; i <= length(str); i++) {
		c = index("0123456789abcdef", substr(str, i, 1))
		v = v * 16 + c - 1
	}
	return v
}

BEGIN {
	groups[1] = ".initcallearly.init .initcall0.init .initcall0s.init " \
		    ".initcall1.init .initcall1s.init .initcall2.init " \
		    ".initcall2s.init .initcall3.init .initcall3s.init " \
		    ".initcall4.init .initcall4s.init .initcall5.init " \
		    ".initcall5s.init .initcallrootfs.init .initcall6.init " \
		    ".initcall6s.init .initcall7.init .initcall7s.init"
	groups[2] = "__stop_sched_class __dl_sched_class __rt_sched_class " \
		    "__fair_sched_class __ext_sched_class __idle_sched_class"
	groups[3] = ".con_initcall.init"
	groups[4] = ".init.setup"
	groups[5] = "__ex_table"
	groups[6] = ".notes"
	ngroups = 6
}

# Output segment lines: "<addr> <off> <size> <name>" with the name in the
# "Out" column (one space after the size); input sections and symbols are
# indented further.
/^ *[0-9a-f]+ +[0-9a-f]+ +[0-9a-f]+ [^ ]/ {
	addr[$4] = hex($1)
	size[$4] = hex($3)
	seen[$4] = 1
}

END {
	bad = 0
	for (g = 1; g <= ngroups; g++) {
		n = split(groups[g], s, " ")
		for (i = 1; i <= n; i++) {
			if (!seen[s[i]]) {
				printf "check-sections: segment %s is missing\n", s[i]
				bad = 1
				continue
			}
			if (i > 1 && seen[s[i - 1]] &&
			    addr[s[i - 1]] + size[s[i - 1]] != addr[s[i]]) {
				printf "check-sections: %s (0x%x+0x%x) is not directly followed by %s (0x%x)\n", \
					s[i - 1], addr[s[i - 1]], size[s[i - 1]], s[i], addr[s[i]]
				bad = 1
			}
		}
	}
	if (bad) {
		print "check-sections: wasm-ld did not produce the expected section layout"
		exit 1
	}
}
' "$map" >&2
