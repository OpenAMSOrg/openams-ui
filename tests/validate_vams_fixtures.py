#!/usr/bin/env python3
"""Validate the recorded fixtures' `openams_ui` payloads against the host contract.

Every event of kind `openams_ui` in tests/fixtures/recorded/*.json (the *.view.json
goldens are not fixtures) must validate against openams_ui_v1.schema.json, so a
hand-written fixture cannot drift from what a host may send. The fixture shape
itself ({scenario, seed, schema_version, family_map, events:[{t, kind, unit,
payload}]}) is checked too, and every `openams_ui` unit must be named in
family_map with that family (a variant label such as "ams1-rev2" is fine).

Recorded fixtures that predate the five-element `lane.stage` (a three-element
one, which the mapper still reads: C2 5.3) are tolerated on that one point and
counted; the hand-written `cap_*` fixtures must validate in full.

The schema is the host's own file. It is found at
$OPENAMS_ROOT/docs/contracts/schemas/openams_ui_v1.schema.json, else in a
sibling checkout ../openams, else $OPENAMS_UI_SCHEMA names the file directly.
With no schema (or no `jsonschema` module) the script exits 77, which CTest
reports as skipped; CI treats a skip as a failure where the host is available.

Usage: python3 tests/validate_vams_fixtures.py   (from anywhere)
"""
import glob
import json
import os
import sys

SKIP = 77
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures", "vams")
KINDS = {"openams_ui", "c2_status", "component_status"}


def find_schema():
    explicit = os.environ.get("OPENAMS_UI_SCHEMA")
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    rel = os.path.join("docs", "contracts", "schemas", "openams_ui_v1.schema.json")
    roots = [os.environ.get("OPENAMS_ROOT"), os.path.join(HERE, "..", "..", "openams")]
    for root in roots:
        if root and os.path.isfile(os.path.join(root, rel)):
            return os.path.join(root, rel)
    return None


def main():
    try:
        import jsonschema
    except ImportError:
        print("SKIP: the jsonschema module is not installed")
        return SKIP
    path = find_schema()
    if not path:
        print("SKIP: openams_ui_v1.schema.json not found (set OPENAMS_ROOT)")
        return SKIP
    with open(path, encoding="utf-8") as f:
        schema = json.load(f)
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)

    errors = []
    checked = 0
    legacy = 0
    for fixture in sorted(glob.glob(os.path.join(FIXTURES, "*.json"))):
        name = os.path.basename(fixture)
        if name.endswith(".view.json"):
            continue
        with open(fixture, encoding="utf-8") as f:
            doc = json.load(f)
        for key in ("scenario", "seed", "schema_version", "family_map", "events"):
            if key not in doc:
                errors.append(f"{name}: missing '{key}'")
        if doc.get("scenario") != name[:-len(".json")]:
            errors.append(f"{name}: scenario is '{doc.get('scenario')}'")
        for i, event in enumerate(doc.get("events", [])):
            where = f"{name} event {i}"
            if set(event) != {"t", "kind", "unit", "payload"}:
                errors.append(f"{where}: keys are {sorted(event)}")
                continue
            if event["kind"] not in KINDS:
                errors.append(f"{where}: unknown kind '{event['kind']}'")
                continue
            if event["kind"] != "openams_ui":
                continue
            payload = event["payload"]
            for err in sorted(validator.iter_errors(payload), key=lambda e: list(e.path)):
                loc = "/".join(str(p) for p in err.path) or "<root>"
                stage = (payload.get("lane") or {}).get("stage")
                if (loc == "lane/stage" and isinstance(stage, list) and len(stage) == 3
                        and not name.startswith("cap_")):
                    legacy += 1
                    continue
                errors.append(f"{where}: {loc}: {err.message}")
            unit = payload.get("unit")
            if event["unit"] != unit:
                errors.append(f"{where}: event unit '{event['unit']}' is not payload unit '{unit}'")
            # a recorder may label a variant ("ams1-rev2"): the family leads
            if not str(doc.get("family_map", {}).get(unit)).startswith(str(payload.get("family"))):
                errors.append(f"{where}: family_map does not say {unit} is {payload.get('family')}")
            checked += 1

    for line in errors:
        print("FAIL " + line)
    if errors:
        return 1
    note = f" ({legacy} older recordings carry a three-element lane.stage)" if legacy else ""
    print(f"ok: {checked} openams_ui payloads validate against {os.path.basename(path)}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
