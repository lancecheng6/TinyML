#!/usr/bin/env python3
"""check_parity.py — compare PC-computed int8 tensors with the board dumps.

Usage:
    python3 check_parity.py <raw_dir>

For each <name>.i8 (PC) there must be <name>.board.i8 (dumped by the board).
Exit code 0 if all identical, 1 otherwise.
"""
import glob
import os
import sys

d = sys.argv[1] if len(sys.argv) > 1 else "."
ok = bad = missing = 0
for p in sorted(glob.glob(os.path.join(d, "*.i8"))):
    if p.endswith(".board.i8"):
        continue
    b = p[:-3] + ".board.i8"
    if not os.path.exists(b):
        print(os.path.basename(p), "MISSING board dump")
        missing += 1
        continue
    a = open(p, "rb").read()
    c = open(b, "rb").read()
    same = (a == c)
    print("%-16s %-9s %d/%d" % (os.path.basename(p), "IDENTICAL" if same else "DIFF", len(a), len(c)))
    ok += same
    bad += (not same)

print("identical=%d diff=%d missing=%d" % (ok, bad, missing))
sys.exit(0 if (bad == 0 and missing == 0 and ok > 0) else 1)
