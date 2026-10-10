#!/usr/bin/env python3
"""Export tests/test_actions_map.c's cases as tests/fixtures/actions_vectors.json.

The native test is the source of truth; the wasm build (web/core/test) replays
the same lines through core_action() and must agree. Run from anywhere:

    python3 tools/export_actions_vectors.py            # rewrite the JSON
    python3 tools/export_actions_vectors.py --check    # fail when it is stale

What is exported: every expect("line", "script"), expect_local("line") and
expect_unaddressable("line", "reason") call, all made against the model the
"loaded" recorded fixture builds. A call made while the test has poked the model
into another state (g_mmu.busy = ..., a positioning flag, ...) is skipped,
because the JS side has no way to set that state: the script tracks each
`g_mmu.<field> = <value>;` assignment and treats the model as clean again only
once every field it changed is back to false / MMU_BUSY_NONE / -1.

The RPC cases are asserted field by field in the C test rather than through a
helper, so they are listed in RPC_CASES below, and each line is cross-checked
against the C source so the two cannot drift apart silently.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tests", "test_actions_map.c")
OUT = os.path.join(ROOT, "tests", "fixtures", "actions_vectors.json")

FIXTURE = "loaded"

# (line, method, params) - mirrors test_rpc_* in test_actions_map.c. The
# spool-confirm bay is "<unit name>-<bay>" for oams11 = unit oams1, bay 0.
RPC_CASES = [
    ("spool list", "server.spoolman.proxy",
     {"request_method": "GET", "path": "/v1/spool"}),
    ("spool confirm oams11 750", "server.openams_spoolman.confirm",
     {"bay": "oams1-0", "remaining_g": 750}),
    ("vendor list", "server.spoolman.proxy",
     {"request_method": "GET", "path": "/v1/vendor"}),
]
# (line, reason): an RPC line the model refuses. The "loaded" fixture has no
# component status, so the host has no edit endpoint as far as it can tell.
RPC_ERRORS = [
    ("spool edit oams11 color=255", "Spoolman is not set up"),
]
# Lines an RPC mapper does not know a slot for: local, not an error.
RPC_LOCAL = ["spool confirm oams99 500"]

CALL = re.compile(r'\b(expect|expect_local|expect_unaddressable)\s*\(\s*'
                  r'("(?:[^"\\]|\\.)*")\s*(?:,\s*("(?:[^"\\]|\\.)*"))?\s*\)\s*;')
CLEAN_VALUES = {"false", "MMU_BUSY_NONE", "-1", "0"}


def c_string(lit):
    """Decode a C string literal (the few escapes the test uses)."""
    body = lit[1:-1]
    return re.sub(r'\\(.)', lambda m: {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}[m.group(1)], body)


def functions(text):
    """Yield (name, body) for each top-level `static void test_*(void)`."""
    for m in re.finditer(r'^static void (test_\w+)\(void\)\n\{\n(.*?)^\}\n', text, re.S | re.M):
        yield m.group(1), m.group(2)


def events(body):
    """The body's model pokes and helper calls, in source order."""
    ev = []
    for m in re.finditer(r'^[ \t]*(g_mmu\.[A-Za-z0-9_.\[\]]+|[a-z_]\w*->[A-Za-z0-9_.\[\]>-]+)[ \t]*=[ \t]*([^;=]+);', body, re.M):
        ev.append((m.start(), "assign", m.group(1), m.group(2).strip()))
    # forget_host_lists(): the test drops the host's own action lists from the
    # model, which the JS replay (it keeps them) cannot do: what follows is
    # not exported, like any other poke
    for m in re.finditer(r'^[ \t]*forget_host_lists\(', body, re.M):
        ev.append((m.start(), "forget", "", ""))
    for m in re.finditer(r'^[ \t]*build\(', body, re.M):
        ev.append((m.start(), "rebuild", "", ""))
    for m in CALL.finditer(body):
        ev.append((m.start(), "call", m.group(1), (m.group(2), m.group(3))))
    # k_local[]: the lines test_local_vocabulary expects to be local
    arr = re.search(r'k_local\[\]\s*=\s*\{(.*?)\};', body, re.S)
    if arr:
        for m in re.finditer(r'"(?:[^"\\]|\\.)*"', arr.group(1)):
            ev.append((arr.start() + m.start(), "call", "expect_local", (m.group(0), None)))
    return sorted(ev, key=lambda e: e[0])


def build():
    text = open(SRC).read()
    vectors = []
    skipped = 0
    seen = set()
    forgotten = False   # lasts across functions, until a build() makes the model again
    for name, body in functions(text):
        if name == "test_small_out":
            continue  # buffer-size checks, not vectors
        dirty = set()
        for _, what, a, b in events(body):
            if what == "forget":
                forgotten = True
                continue
            if what == "rebuild":
                forgotten = False
                dirty.clear()
                continue
            if what == "assign":
                if b in CLEAN_VALUES:
                    dirty.discard(a)
                else:
                    dirty.add(a)
                continue
            fn, line = a, c_string(b[0])
            if dirty or forgotten:
                skipped += 1
                continue
            key = (fn, line)
            if key in seen:
                continue
            seen.add(key)
            if fn == "expect":
                vectors.append({"line": line, "kind": "gcode", "script": c_string(b[1])})
            elif fn == "expect_local":
                vectors.append({"line": line, "kind": "local"})
            else:
                vectors.append({"line": line, "kind": "error", "reason": c_string(b[1])})

    for line, method, params in RPC_CASES:
        if '"%s"' % line not in text:
            sys.exit("export_actions_vectors: RPC case %r is no longer in %s" % (line, SRC))
        vectors.append({"line": line, "kind": "rpc", "method": method, "params": params})
    for line, reason in RPC_ERRORS:
        if '"%s"' % line not in text:
            sys.exit("export_actions_vectors: RPC case %r is no longer in %s" % (line, SRC))
        vectors.append({"line": line, "kind": "error", "reason": reason})
    for line in RPC_LOCAL:
        if '"%s"' % line not in text:
            sys.exit("export_actions_vectors: RPC case %r is no longer in %s" % (line, SRC))
        vectors.append({"line": line, "kind": "local"})

    doc = {
        "source": "tests/test_actions_map.c (tools/export_actions_vectors.py)",
        "fixture": FIXTURE,
        "note": "Each line is resolved against the model the fixture builds "
                "(tests/fixtures/recorded/<fixture>.json applied in order). kind: "
                "gcode {script}, rpc {method, params}, error {reason}, local.",
        "vectors": vectors,
    }
    return json.dumps(doc, indent=2) + "\n", len(vectors), skipped


def main():
    text, n, skipped = build()
    if "--check" in sys.argv:
        current = open(OUT).read() if os.path.exists(OUT) else ""
        if current != text:
            sys.exit("export_actions_vectors: %s is stale; run tools/export_actions_vectors.py" % OUT)
        print("actions_vectors.json is current (%d vectors)" % n)
        return
    with open(OUT, "w") as f:
        f.write(text)
    print("wrote %s: %d vectors (%d skipped: model in a poked state)" % (OUT, n, skipped))


if __name__ == "__main__":
    main()
