#!/usr/bin/env python3
"""PC-side helper for the display's spool tag writer (its "tag" console
commands over the USB serial port; see docs/guide/tag-writer.md).

  tagtool.py cmd <command...>        send tag commands ("status; decode"), print the replies
  tagtool.py decode --key K --openams-src DIR
                                     dump the tag on the reader and check it with the
                                     OpenAMS bambu_lab_tag_processor
  tagtool.py library --lib DIR [q]   search a local Bambu-Lab-RFID-Library checkout
  tagtool.py clonefrom --lib DIR q   clone one library tag onto the magic tag on the reader
  tagtool.py libupload --lib DIR [q] fill the display's on-device library
  tagtool.py libclear                empty it
  tagtool.py export DIR              capture the genuine tag on the reader into DIR

The port does not reset the display when it opens. The Bambu master key is
only needed by `decode`, and only on the PC (--key or --key-file).
"""

import argparse
import pathlib
import re
import sys
import time
import types

import serial

PROMPT = b"\n> "


def open_port(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.05
    s.dtr = False  # keep IO0 high and EN released: opening must not reset
    s.rts = False
    s.open()
    time.sleep(0.05)
    s.reset_input_buffer()
    return s


def run(s, command, until=PROMPT, timeout=30.0):
    """Sends one tag command and returns its reply, up to the prompt. Flushes
    any stale bytes first and warns on a timeout, so one slow reply does not
    shift every later one."""
    s.reset_input_buffer()
    s.write(("tag " + command).encode() + b"\n")
    data = b""
    end = time.time() + timeout
    while time.time() < end:
        chunk = s.read(65536)
        if chunk:
            data += chunk
            if until in data:
                break
    else:
        sys.stderr.write("tagtool: timed out waiting for the reply to %r\n" % command)
    return data.decode(errors="replace")


def master_key(args):
    text = args.key
    if not text and args.key_file:
        text = pathlib.Path(args.key_file).read_text()
    found = re.sub(r"0x|[^0-9a-fA-F]", "", text or "")
    if len(found) != 32:
        sys.exit("decode needs the Bambu master key: --key <32 hex> or --key-file <file>")
    return found


def load_processor(src, key):
    if not src:
        sys.exit("decode needs --openams-src (the directory with bambu_lab_tag_processor.py)")
    sys.path.insert(0, str(src))
    import bambu_lab_tag_processor
    config = types.SimpleNamespace(get=lambda name: key, error=ValueError)
    return bambu_lab_tag_processor.BambuLabTagProcessor(config)


def cmd_decode(s, args):
    reply = run(s, "dump")
    blocks = dict((int(n), h) for n, h in re.findall(r"BLOCK (\d\d) (\S{32})", reply))
    if len(blocks) != 64:
        print(reply)
        sys.exit("dump incomplete")
    card = re.search(r'\{"uid":"([0-9A-F]+)".*?"kind":"([^"]+)"\}', reply)
    uid = bytes.fromhex(card.group(1))
    print("tag %s (%s)" % (card.group(1), card.group(2)))
    unread = [n for n, h in blocks.items() if "?" in h]
    if unread:
        print("unreadable blocks:", unread)
    image = bytearray(1024)
    for n, h in blocks.items():
        if "?" not in h:
            image[n * 16:(n + 1) * 16] = bytes.fromhex(h)

    processor = load_processor(args.openams_src, master_key(args))
    keys = processor.derive_sector_keys(uid)
    trailers_ok = all(image[s * 64 + 48:s * 64 + 54] == keys[s] for s in range(16))
    print("sector keys match HKDF(master, uid):", trailers_ok)

    calls = []

    def read_block(block, key, tag_uid, key_b=False):
        # The display reads with whichever key opened the sector; this checks
        # that the decoder asks for exactly the derived key A.
        calls.append(bytes(key) == keys[block // 4] and not key_b)
        return bytes(image[block * 16:(block + 1) * 16])

    decoded = processor.read_tag(types.SimpleNamespace(read_block=read_block), uid)
    print("decoder used derived key A for every block:", all(calls))
    for name in sorted(decoded):
        value = decoded[name]
        if name == "color_argb":
            value = "0x%08X" % value
        print("  %-16s %s" % (name, value))
    return 0 if trailers_ok and all(calls) else 1


def send_lines(s, command, image):
    """Sends a command that reads 64 hex lines (loadimage, libadd), then the
    image, and returns the reply up to the prompt."""
    assert len(image) == 1024
    s.reset_input_buffer()
    s.write(("tag " + command).encode() + b"\n")
    time.sleep(0.4)  # the reader comes up and the command asks for the lines
    s.read(4096)
    for block in range(64):
        s.write(image[block * 16:(block + 1) * 16].hex().encode() + b"\n")
        time.sleep(0.025)
    reply = b""
    end = time.time() + 10
    while time.time() < end and PROMPT not in reply:
        reply += s.read(4096)
    return reply.decode(errors="replace")


def library_index(lib):
    """Every (-dump.bin path) under a local Bambu-Lab-RFID-Library checkout,
    with its material/color from the folder layout."""
    lib = pathlib.Path(lib)
    out = []
    for binpath in sorted(lib.rglob("*-dump.bin")):
        rel = binpath.relative_to(lib).parts
        if len(rel) >= 4:  # Category / Material / Color / UID / file
            out.append((binpath, rel[0], rel[1], rel[2], rel[3]))
    return out


def cmd_library(s, args):
    index = library_index(args.lib)
    q = " ".join(args.query).lower()
    hits = [r for r in index if q in ("%s %s %s" % (r[1], r[2], r[3])).lower()] if q else index
    for binpath, cat, material, color, uid in hits[:args.limit]:
        print("%-10s %-22s %-20s %s" % (cat, material, color, uid))
    print("# %d match%s%s" % (len(hits), "" if len(hits) == 1 else "es",
                              "" if len(hits) <= args.limit else " (showing %d)" % args.limit))


def cmd_clonefrom(s, args):
    index = library_index(args.lib)
    q = " ".join(args.query).lower()
    hits = [r for r in index if q in ("%s %s %s %s" % (r[1], r[2], r[3], r[4])).lower()]
    if not hits:
        sys.exit("no library tag matches %r" % q)
    if len(hits) > 1 and not args.uid:
        print("ambiguous -- %d matches, pick one by UID with --uid:" % len(hits))
        for _, cat, material, color, uid in hits[:20]:
            print("  %-22s %-20s %s" % (material, color, uid))
        sys.exit(1)
    if args.uid:
        hits = [r for r in hits if r[4].upper() == args.uid.upper()] or sys.exit("no such UID")
    binpath, cat, material, color, uid = hits[0]
    image = binpath.read_bytes()
    if len(image) != 1024:
        sys.exit("%s is %d bytes, expected 1024" % (binpath, len(image)))
    print("cloning %s / %s / %s (UID %s)" % (cat, material, color, uid))
    print(send_lines(s, "loadimage", image), end="")
    print(run(s, "clone"), end="")


def cmd_libupload(s, args):
    """Uploads matching library tags into the display's on-device library, so
    they can be cloned from the touchscreen without a PC."""
    index = library_index(args.lib)
    q = " ".join(args.query).lower()
    hits = [r for r in index if (not q or q in ("%s %s %s" % (r[1], r[2], r[3])).lower())]
    # One entry per material+color by default, so the device library stays a
    # curated set of identities rather than every scanned duplicate.
    if not args.all:
        seen, unique = set(), []
        for r in hits:
            key = (r[2], r[3])  # (material name, color)
            if key not in seen:
                seen.add(key)
                unique.append(r)
        hits = unique
    hits = hits[:args.limit]
    if not hits:
        sys.exit("no library tag matches %r" % q)
    ok = 0
    for binpath, cat, material, color, uid in hits:
        image = binpath.read_bytes()
        if len(image) != 1024:
            print("skip %s (%d bytes)" % (uid, len(image)))
            continue
        reply = send_lines(s, "libadd %s|%s" % (material, color), image)
        time.sleep(0.15)  # let the 4 KB flash erase settle before the next tag
        lines = [l for l in reply.strip().splitlines() if l.startswith(("OK", "ERROR"))]
        status = lines[-1] if lines else ""
        print("%-22s %-20s %s  %s" % (material, color, uid, status))
        ok += status.startswith("OK")
    print("# uploaded %d/%d" % (ok, len(hits)))


def cmd_export(s, args):
    """Captures the genuine tag on the reader and saves it under out_dir in the
    library's layout (so your own scans can fill the gaps)."""
    print(run(s, "capture"), end="")
    text = run(s, "image", until=b"\nEND")
    m = re.search(r"IMAGE ([0-9A-F]+)", text)
    if not m:
        sys.exit("capture failed: %s" % text[:200])
    uid = m.group(1)
    lines = re.findall(r"^([0-9A-Fa-f]{32})\r?$", text, re.M)
    if len(lines) != 64:
        sys.exit("got %d blocks, need 64" % len(lines))
    image = bytes.fromhex("".join(lines))
    out = pathlib.Path(args.out_dir) / ("hf-mf-%s-dump.bin" % uid)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    print("saved %s (%d bytes)" % (out, len(image)))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--key", help="Bambu master key (32 hex), for decode")
    ap.add_argument("--key-file", help="a file holding the Bambu master key, for decode")
    ap.add_argument("--openams-src", type=pathlib.Path,
                    help="directory with OpenAMS's bambu_lab_tag_processor.py, for decode")
    sub = ap.add_subparsers(dest="action", required=True)
    c = sub.add_parser("cmd")
    c.add_argument("words", nargs="+")
    sub.add_parser("decode")
    sub.add_parser("magic")
    lb = sub.add_parser("library")
    lb.add_argument("--lib", required=True, help="path to a Bambu-Lab-RFID-Library checkout")
    lb.add_argument("--limit", type=int, default=40)
    lb.add_argument("query", nargs="*")
    cf = sub.add_parser("clonefrom")
    cf.add_argument("--lib", required=True)
    cf.add_argument("--uid", help="disambiguate when several tags match")
    cf.add_argument("query", nargs="+")
    lu = sub.add_parser("libupload")
    lu.add_argument("--lib", required=True)
    lu.add_argument("--limit", type=int, default=50)
    lu.add_argument("--all", action="store_true",
                    help="every matching dump, not one per material+color")
    lu.add_argument("query", nargs="*")
    sub.add_parser("libclear")
    ex = sub.add_parser("export")
    ex.add_argument("out_dir")
    args = ap.parse_args()

    if args.action == "library":
        return cmd_library(None, args)
    s = open_port(args.port)
    if args.action == "cmd":
        bad = False
        for command in " ".join(args.words).split(";"):
            reply = run(s, command.strip())
            print(reply, end="")
            if "ERROR" in reply:
                bad = True
        return 1 if bad else 0
    if args.action == "decode":
        return cmd_decode(s, args)
    if args.action == "magic":
        print(run(s, "magic"), end="")
        return 0
    if args.action == "clonefrom":
        return cmd_clonefrom(s, args)
    if args.action == "libupload":
        return cmd_libupload(s, args)
    if args.action == "libclear":
        print(run(s, "libclear yes"), end="")
        return 0
    return cmd_export(s, args)


if __name__ == "__main__":
    sys.exit(main())
