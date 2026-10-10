#!/usr/bin/env python3
"""Measure what src/ui/fonts/ costs in flash, and hold it to a budget.

Each generated face is compiled on its own with the same -Os the ESP32 build
uses, and its Berkeley text and data are summed. Those two columns are what
the linker puts in the image; bss is the slots' RAM, not flash, and is left
out.

The budget is written as today's total (96,648 bytes, measured with this
script before the CJK faces existed) plus 327,000 for them. 327,000 is the
largest CJK set that still leaves 128 KiB free in the ESP32 app slot.

Run from the repository root:  python3 tools/font_budget.py
Exits 1 when the total is over the budget.
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "src", "ui", "fonts")

# The faces that were already in the image when this script was written.
BASE_TOTAL = 96648
# What the CJK faces are allowed on top of that.
CJK_ALLOWANCE = 327000
LIMIT = BASE_TOTAL + CJK_ALLOWANCE

CFLAGS = ["-Os", "-c", "-DLV_CONF_INCLUDE_SIMPLE", "-I.", "-Ilib", "-Ilib/lvgl"]


def measure(path, work):
    """The Berkeley text and data of one compiled face."""
    obj = os.path.join(work, os.path.basename(path)[:-2] + ".o")
    proc = subprocess.run(["gcc"] + CFLAGS + [path, "-o", obj],
                          cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        sys.exit("error: %s does not compile" % os.path.relpath(path, ROOT))
    proc = subprocess.run(["size", obj], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        sys.exit("error: size failed on %s" % os.path.basename(obj))
    fields = proc.stdout.splitlines()[-1].split()
    return int(fields[0]), int(fields[1])


def main():
    faces = sorted(f for f in os.listdir(FONT_DIR)
                   if f.startswith("ui_font_") and f.endswith(".c"))
    if not faces:
        sys.exit("error: no faces in %s" % os.path.relpath(FONT_DIR, ROOT))

    work = tempfile.mkdtemp(prefix="font-budget-")
    rows = []
    try:
        for face in faces:
            text, data = measure(os.path.join(FONT_DIR, face), work)
            rows.append((face[:-2], text, data))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    total_text = sum(row[1] for row in rows)
    total_data = sum(row[2] for row in rows)
    total = total_text + total_data

    name = max(len(row[0]) for row in rows)
    print("%-*s %9s %8s %9s" % (name, "face", "text", "data", "total"))
    for face, text, data in rows:
        print("%-*s %9d %8d %9d" % (name, face, text, data, text + data))
    print("-" * (name + 29))
    print("%-*s %9d %8d %9d" % (name, "TOTAL", total_text, total_data, total))
    print()
    print("budget: %d today + %d for the CJK faces = %d" % (BASE_TOTAL, CJK_ALLOWANCE, LIMIT))
    print("headroom: %d bytes" % (LIMIT - total))
    if total > LIMIT:
        print("OVER BUDGET by %d bytes" % (total - LIMIT))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
