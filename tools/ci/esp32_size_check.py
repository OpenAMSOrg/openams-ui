#!/usr/bin/env python3
"""
Read `idf.py size --format json2` from stdin and check that DRAM free >= floor
and that the app image leaves at least 5% headroom in one OTA slot
(platform/esp32/partitions.csv).

Usage:
    idf.py size --format json2 | python3 esp32_size_check.py FLOOR_BYTES

Exits 0 when both checks pass, 1 when either fails, 2 on usage error.
"""
import json
import os
import sys

SLOT_HEADROOM_FRACTION = 0.95  # the image may use at most this much of a slot
# (0.90 until 2026-09-29; raised by the owner to carry eleven languages and
# their fonts in one image)


def parse_size(text):
    """Parses a partitions.csv size field: hex ("0x1F0000"), a decimal byte
    count, or one with a "K"/"M" suffix."""
    text = text.strip()
    if not text:
        return None
    if text[-1] in "kK":
        return int(text[:-1], 0) * 1024
    if text[-1] in "mM":
        return int(text[:-1], 0) * 1024 * 1024
    return int(text, 0)


def ota_slot_size(partitions_csv):
    """Returns the size in bytes of the ota_0 (or factory) app partition, or
    None if the file is missing or has no such row."""
    if not os.path.isfile(partitions_csv):
        return None
    with open(partitions_csv, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [p.strip() for p in line.split(",")]
            if len(fields) < 5:
                continue
            _name, ptype, subtype, _offset, size = fields[:5]
            if ptype == "app" and subtype in ("ota_0", "factory"):
                return parse_size(size)
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: esp32_size_check.py FLOOR_BYTES", file=sys.stderr)
        sys.exit(2)

    floor = int(sys.argv[1])
    text = sys.stdin.read()

    # idf.py size --format json2 may emit status lines before the JSON object.
    start = text.find("{")
    if start == -1:
        print("esp32_size_check: no JSON object in input", file=sys.stderr)
        print(text[:200], file=sys.stderr)
        sys.exit(1)

    data = json.loads(text[start:])
    mem = {m["name"]: m for m in data["layout"]}
    dram = mem["DRAM"]
    iram = mem["IRAM"]
    image_size = data["total_size"]

    print(
        "DRAM used %d free %d (%.2f%%); IRAM free %d; image %d bytes"
        % (
            dram["used"],
            dram["free"],
            100.0 * dram["used"] / dram["total"],
            iram["free"],
            image_size,
        )
    )

    ok = True

    if dram["free"] < floor:
        print(
            "esp32_size_check: FAIL — DRAM free %d < floor %d"
            % (dram["free"], floor),
            file=sys.stderr,
        )
        ok = False

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    partitions_csv = os.path.join(root, "platform", "esp32", "partitions.csv")
    slot_size = ota_slot_size(partitions_csv)
    if slot_size is None:
        print(
            "esp32_size_check: FAIL — could not read an app slot size from %s"
            % partitions_csv,
            file=sys.stderr,
        )
        ok = False
    else:
        max_image = int(slot_size * SLOT_HEADROOM_FRACTION)
        headroom = slot_size - image_size
        print(
            "OTA slot %d bytes, image %d bytes (%.2f%%), headroom %d bytes"
            % (slot_size, image_size, 100.0 * image_size / slot_size, headroom)
        )
        if image_size > max_image:
            print(
                "esp32_size_check: FAIL — image %d bytes > %.0f%% of the %d-byte OTA slot"
                % (image_size, SLOT_HEADROOM_FRACTION * 100.0, slot_size),
                file=sys.stderr,
            )
            ok = False

    if not ok:
        sys.exit(1)

    print("esp32_size_check: OK")


if __name__ == "__main__":
    main()
