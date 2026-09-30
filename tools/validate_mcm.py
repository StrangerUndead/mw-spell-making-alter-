#!/usr/bin/env python3
"""Validate the MCM Helper configuration of Lost Art of Spellmaking.

Checks, in order:
  1. mcm/config.json against MCM Helper's JSON schema (mcm/schema/config.schema.json,
     vendored from github.com/Exit-9B/MCM-Helper docs/) with python-jsonschema.
  2. Keys the MCM Helper parser actually accepts (src/Json/*Handler.cpp), which is
     stricter than the schema in places and looser in others.
  3. Every setting id is `key:Section`, typed by its prefix (b/i/f), present in
     mcm/settings.ini, and the set of keys, their sections and defaults equal
     docs/dev/CONTRACTS.md section 5. Enum option counts match the contract's value
     lists; slider defaults lie on the slider's range and step.
  4. Script bindings: every action function and text-readout property exists in
     scripts/Source/LostArt_MCM.psc.
  5. Every "$LA_MCM_*" key used by config.json and the Papyrus sources has a line in
     mcm/LostArt_MCM_ENGLISH.src.tsv (UTF-8, key<TAB>value), and the TSV has no unused
     or duplicate keys.

Exit status 0 when everything passes. Usage: python3 tools/validate_mcm.py [--repo DIR]
"""
from __future__ import annotations

import argparse
import configparser
import json
import re
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:  # pragma: no cover
    jsonschema = None

# Keys MCM Helper's JSON handlers accept (MCM-Helper src/Json, 2026-08).
TOP_KEYS = {"$schema", "modName", "displayName", "minMcmVersion", "pluginRequirements",
            "content", "customContent", "cursorFillMode", "pages"}
PAGE_KEYS = {"pageDisplayName", "content", "customContent", "cursorFillMode"}
CONTROL_KEYS = {"id", "position", "text", "help", "type", "groupCondition", "groupBehavior",
                "groupControl", "formatString", "ignoreConflicts", "action", "valueOptions"}
VALUE_KEYS = {"min", "max", "step", "value", "formatString", "options", "shortNames",
              "sourceType", "sourceForm", "scriptName", "propertyName", "defaultValue"}
ACTION_KEYS = {"type", "form", "scriptName", "script", "function", "command", "params"}
SOURCE_FOR_PREFIX = {"b": "ModSettingBool", "i": "ModSettingInt", "f": "ModSettingFloat"}


def corrected_schema(schema: dict) -> tuple[dict, list[str]]:
    """Apply the minimal corrections the upstream schema needs to express what MCM
    Helper's parser accepts. Without them the upstream schema rejects every enum and
    stepper control with options - including MCM Helper's own SkyUI_SE example config -
    and jsonschema raises on its invalid "type" values. Returns (schema, notes)."""
    import copy
    s = copy.deepcopy(schema)
    notes = []
    content_rules = s["$defs"]["content"]["items"]["allOf"]
    for rule in content_rules:
        props = rule.get("then", {}).get("properties", {})
        if rule.get("$comment") == "Keymap properties" and props.get("ignoreConflicts", {}).get("type") == "bool":
            props["ignoreConflicts"]["type"] = "boolean"
            notes.append('keymap ignoreConflicts: "type": "bool" -> "boolean"')
        if rule.get("$comment") == "Generic numeric control value options":
            enum_list = rule["if"]["properties"]["type"]["enum"]
            for t in ("enum", "stepper"):
                if t in enum_list:
                    enum_list.remove(t)
                    notes.append(f'generic numeric valueOptions no longer applies to "{t}" '
                                 f'(its own rule already includes the value source; the generic '
                                 f'rule\'s unevaluatedProperties rejected "options")')
    ts = s["$defs"]["valueOptions-textSource"]["properties"]["sourceType"]
    if isinstance(ts.get("type"), dict):
        ts["enum"] = ts.pop("type")["enum"]
        notes.append('text sourceType: "type": {"enum": ...} -> "enum": ...')
    cc = s["$defs"]["content-or-customContent"]["oneOf"][1]["properties"]["customContent"]["properties"]
    for axis in ("x", "y"):
        if cc[axis].get("exclusiveMaximum") is True:
            cc[axis]["exclusiveMaximum"] = cc[axis].pop("maximum")
            notes.append(f'customContent.{axis}: draft-04 "exclusiveMaximum": true -> numeric bound')
    page_props = s["properties"]["pages"]["items"]["properties"]
    if "cursorFillMode" not in page_props:
        page_props["cursorFillMode"] = s["properties"]["cursorFillMode"]
        notes.append("pages[].cursorFillMode allowed (PagesHandler.cpp reads it)")
    return s, notes


class Report:
    def __init__(self):
        self.errors: list[str] = []
        self.notes: list[str] = []

    def err(self, msg: str):
        self.errors.append(msg)

    def note(self, msg: str):
        self.notes.append(msg)


def parse_contract_settings(contracts: Path) -> dict[str, dict]:
    """Parse the settings block of CONTRACTS section 5 into
    {"key:Section": {"default": str, "values": int|None}}."""
    text = contracts.read_text(encoding="utf-8")
    sec5 = text.split("## 5.", 1)[1].split("\n## ", 1)[0]
    block = sec5.split("```", 2)[1]
    out: dict[str, dict] = {}
    section = None
    for line in block.splitlines():
        m = re.match(r"\s*\[(\w+)\]\s*(.*)", line)
        if m:
            section, rest = m.group(1), m.group(2)
        else:
            rest = line
        if section is None:
            continue
        # key=value optionally followed by "(0 A, 1 B, ...)"
        for km in re.finditer(r"([bifs][A-Za-z0-9]+)=([^\s(]+)(\s*\(([^)]*)\))?", rest):
            key, default, enum_list = km.group(1), km.group(2), km.group(4)
            values = None
            if enum_list:
                values = len(re.findall(r"(?:^|,)\s*\d+\s", enum_list + " "))
            out[f"{key}:{section}"] = {"default": default, "values": values}
    return out


def check(repo: Path) -> Report:
    rep = Report()
    mcm = repo / "mcm"
    cfg_path = mcm / "config.json"
    ini_path = mcm / "settings.ini"
    tsv_path = mcm / "LostArt_MCM_ENGLISH.src.tsv"
    schema_path = mcm / "schema" / "config.schema.json"
    psc_path = repo / "scripts" / "Source" / "LostArt_MCM.psc"
    contracts = repo / "docs" / "dev" / "CONTRACTS.md"

    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))

    # 1. JSON schema
    if jsonschema is None:
        rep.err("python-jsonschema is not installed (pip install jsonschema)")
    else:
        upstream = json.loads(schema_path.read_text(encoding="utf-8"))
        schema, fixes = corrected_schema(upstream)
        for f in fixes:
            rep.note(f"schema correction: {f}")
        try:
            jsonschema.Draft202012Validator.check_schema(schema)
        except jsonschema.SchemaError as e:
            rep.err(f"corrected schema is not valid draft 2020-12: {e.message[:200]}")
        v = jsonschema.Draft202012Validator(schema)
        errs = sorted(v.iter_errors(cfg), key=lambda e: list(e.absolute_path))
        for e in errs:
            rep.err(f"schema: /{'/'.join(map(str, e.absolute_path))}: {e.message[:200]}")
        if not errs:
            rep.note(f"config.json is valid against {schema_path.relative_to(repo)}")

    # 2. parser keys
    for k in cfg:
        if k not in TOP_KEYS:
            rep.err(f"top-level key {k!r} is not read by MCM Helper")
    if cfg.get("modName") != "LostArt":
        rep.err("modName must be 'LostArt' (the plugin LostArt.esp that owns the MCM quest)")
    controls = []
    for pi, page in enumerate(cfg.get("pages", [])):
        for k in page:
            if k not in PAGE_KEYS:
                rep.err(f"pages[{pi}]: key {k!r} is not read by MCM Helper")
        for ci, c in enumerate(page.get("content", [])):
            where = f"pages[{pi}] ({page.get('pageDisplayName')}) content[{ci}]"
            controls.append((where, page.get("pageDisplayName"), c))
            for k in c:
                if k not in CONTROL_KEYS:
                    rep.err(f"{where}: key {k!r} is not read by MCM Helper")
            for k in c.get("valueOptions", {}):
                if k not in VALUE_KEYS:
                    rep.err(f"{where}: valueOptions key {k!r} is not read by MCM Helper")
            for k in c.get("action", {}):
                if k not in ACTION_KEYS:
                    rep.err(f"{where}: action key {k!r} is not read by MCM Helper")
            if c.get("type") == "text" and "id" in c and "propertyName" not in c.get("valueOptions", {}):
                rep.err(f"{where}: a text control with an id becomes a ModSettingString; "
                        f"drop the id or bind propertyName")

    # 3. settings
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str  # keep key case
    ini.read(ini_path, encoding="utf-8")
    ini_keys = {f"{k}:{s}": v for s in ini.sections() for k, v in ini.items(s)}
    contract = parse_contract_settings(contracts)
    if not contract:
        rep.err("could not parse any settings from CONTRACTS.md section 5")

    ids_seen: dict[str, str] = {}
    for where, _page, c in controls:
        sid = c.get("id")
        if not sid or c.get("type") not in ("toggle", "slider", "enum", "stepper"):
            continue
        if sid in ids_seen:
            rep.err(f"{where}: duplicate id {sid} (also {ids_seen[sid]})")
        ids_seen[sid] = where
        m = re.fullmatch(r"([bif])[A-Za-z0-9]+:([A-Za-z]+)", sid)
        if not m:
            rep.err(f"{where}: id {sid!r} is not 'key:Section' with a b/i/f prefix")
            continue
        prefix = m.group(1)
        src = c.get("valueOptions", {}).get("sourceType")
        if src != SOURCE_FOR_PREFIX[prefix]:
            rep.err(f"{where}: {sid} uses {src}, its prefix needs {SOURCE_FOR_PREFIX[prefix]}")
        want_type = {"b": ("toggle",), "i": ("slider", "enum", "stepper"), "f": ("slider",)}[prefix]
        if c["type"] not in want_type:
            rep.err(f"{where}: {sid} is a {c['type']} control; expected {want_type}")
        if sid not in ini_keys:
            rep.err(f"{where}: {sid} has no default in settings.ini")
        cdef = contract.get(sid)
        if cdef is None:
            rep.err(f"{where}: {sid} is not in CONTRACTS section 5")
            continue
        default = ini_keys.get(sid)
        if default is not None and float(default) != float(cdef["default"]):
            rep.err(f"{sid}: settings.ini default {default} != contract {cdef['default']}")
        vo = c.get("valueOptions", {})
        if c["type"] == "enum":
            n = len(vo.get("options", []))
            if cdef["values"] is not None and n != cdef["values"]:
                rep.err(f"{sid}: {n} options, contract lists {cdef['values']} values")
            if "shortNames" in vo and len(vo["shortNames"]) != n:
                rep.err(f"{sid}: shortNames length differs from options")
            if default is not None and not (0 <= int(default) < n):
                rep.err(f"{sid}: default {default} is not a valid option index")
        if c["type"] == "slider":
            lo, hi, step = vo.get("min"), vo.get("max"), vo.get("step")
            if None in (lo, hi, step) or "formatString" not in vo:
                rep.err(f"{sid}: slider needs min, max, step and formatString")
            elif default is not None:
                d = float(default)
                if not (lo <= d <= hi):
                    rep.err(f"{sid}: default {d} outside [{lo}, {hi}]")
                k = (d - lo) / step
                if abs(k - round(k)) > 1e-6:
                    rep.err(f"{sid}: default {d} is not on the step grid {lo} + n*{step}")
                if prefix == "i" and any(float(x) != int(x) for x in (lo, hi, step)):
                    rep.err(f"{sid}: integer setting with fractional slider bounds/step")
    for sid in contract:
        if sid not in ids_seen:
            rep.err(f"CONTRACTS section 5 setting {sid} has no MCM control")
    for sid in ini_keys:
        if sid not in contract:
            rep.err(f"settings.ini key {sid} is not in CONTRACTS section 5")
    if not rep.errors:
        rep.note(f"{len(ids_seen)} settings match CONTRACTS section 5 and settings.ini")

    # 4. script bindings
    psc = psc_path.read_text(encoding="utf-8")
    psc_funcs = {m.lower() for m in re.findall(r"(?im)^\s*Function\s+(\w+)\s*\(", psc)}
    psc_props = {m.lower() for m in re.findall(r"(?im)^\s*\w+(?:\[\])?\s+Property\s+(\w+)", psc)}
    for where, _page, c in controls:
        act = c.get("action")
        if act:
            if act.get("type") != "CallFunction":
                rep.err(f"{where}: only CallFunction actions are used by this mod")
            elif act.get("function", "").lower() not in psc_funcs:
                rep.err(f"{where}: action function {act.get('function')} not found in LostArt_MCM.psc")
        prop = c.get("valueOptions", {}).get("propertyName")
        if prop and prop.lower() not in psc_props:
            rep.err(f"{where}: propertyName {prop} not found in LostArt_MCM.psc")

    # 5. translations
    used: set[str] = set()

    def collect(obj):
        if isinstance(obj, str):
            for m in re.finditer(r"\$LA_MCM_[A-Za-z0-9_]+", obj):
                used.add(m.group(0))
        elif isinstance(obj, list):
            for x in obj:
                collect(x)
        elif isinstance(obj, dict):
            for x in obj.values():
                collect(x)

    collect(cfg)
    for psc_file in (repo / "scripts" / "Source").glob("*.psc"):
        collect(psc_file.read_text(encoding="utf-8"))
    raw = tsv_path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or raw.startswith(b"\xff\xfe"):
        rep.err("TSV fragment must be UTF-8 without BOM (the data agent converts to UTF-16 LE)")
    tsv: dict[str, str] = {}
    for n, line in enumerate(raw.decode("utf-8").splitlines(), 1):
        if not line.strip():
            continue
        if line.count("\t") != 1:
            rep.err(f"TSV line {n}: expected exactly one TAB")
            continue
        k, val = line.split("\t")
        if k in tsv:
            rep.err(f"TSV line {n}: duplicate key {k}")
        if not k.startswith("$LA_MCM_"):
            rep.err(f"TSV line {n}: key {k} is not a $LA_MCM_ key")
        if not val.strip():
            rep.err(f"TSV line {n}: empty value for {k}")
        tsv[k] = val
    for k in sorted(used - tsv.keys()):
        rep.err(f"translation missing for {k}")
    for k in sorted(tsv.keys() - used):
        rep.err(f"TSV key {k} is not used by config.json or the Papyrus sources")
    for k in ("$LA_MCM_Msg_Rebalanced", "$LA_MCM_Msg_Rebuilt"):
        if k in tsv and "{}" not in tsv[k]:
            rep.err(f"{k} must contain '{{}}' (SkyUI nested-translation argument)")
    if not (used - tsv.keys()):
        rep.note(f"{len(used)} $LA_MCM_ keys used, all translated")
    return rep


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args()
    rep = check(args.repo)
    for n in rep.notes:
        print(f"note: {n}")
    for e in rep.errors:
        print(f"ERROR: {e}")
    print("MCM validation", "FAILED" if rep.errors else "passed", f"({len(rep.errors)} errors)")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
