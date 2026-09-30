#!/usr/bin/env python3
"""Minimal reader for Skyrim (SE/AE) compiled Papyrus .pex files.

Used by scripts/build.sh to check what the compiler produced, independent of the
compiler: header (magic 0xFA57C0DE, big-endian, format 3.x, game id 1 = Skyrim),
the object's name and parent, every function per state with its global/native
flags and parameter types, and every property. It walks the whole file, including
the bytecode, and fails if anything is left over or truncated.

Usage:
  pexinfo.py FILE.pex [...]            print a summary
  pexinfo.py --json FILE.pex [...]     print the parsed summary as JSON
  pexinfo.py --check-api FILE.pex      additionally verify LostArt.pex against the
                                       CONTRACTS section 8 API (exit 1 on mismatch)
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

MAGIC = 0xFA57C0DE

# Skyrim opcode table: opcode -> (fixed argument count, has variadic tail)
OPCODES = {
    0: (0, False), 1: (3, False), 2: (3, False), 3: (3, False), 4: (3, False),
    5: (3, False), 6: (3, False), 7: (3, False), 8: (3, False), 9: (3, False),
    10: (2, False), 11: (2, False), 12: (2, False), 13: (2, False), 14: (2, False),
    15: (3, False), 16: (3, False), 17: (3, False), 18: (3, False), 19: (3, False),
    20: (1, False), 21: (2, False), 22: (2, False),
    23: (3, True),   # callmethod name self dest [args]
    24: (2, True),   # callparent name dest [args]
    25: (3, True),   # callstatic class name dest [args]
    26: (1, False), 27: (3, False), 28: (3, False), 29: (3, False),
    30: (2, False), 31: (2, False), 32: (3, False), 33: (3, False),
    34: (4, False), 35: (4, False),
}

# CONTRACTS section 8: name -> (return type, [param types])
LOSTART_API = {
    "OpenSpellmaking": ("None", ["ObjectReference"]),
    "IsCustomSpell": ("bool", ["Spell"]),
    "GetCustomSpellCount": ("int", []),
    "GetFreeSlotCount": ("int", []),
    "GetFreeSubSlotCount": ("int", []),
    "DeleteCustomSpell": ("bool", ["Spell"]),
    "SetAltarActive": ("None", ["ObjectReference", "bool"]),
    "GetRefusalReason": ("int", ["Actor"]),
    "ReloadSettings": ("None", []),
    "RebalanceAll": ("int", []),
    "RebuildAllSlots": ("int", []),
    "PrepareForUninstall": ("bool", []),
    "GetVersion": ("string", []),
    "RunTests": ("int", ["string"]),
}


class PexError(Exception):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0
        self.strings: list[str] = []

    def take(self, n: int) -> bytes:
        if self.p + n > len(self.d):
            raise PexError(f"truncated at offset {self.p} (wanted {n} bytes)")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self) -> int: return self.take(1)[0]
    def u16(self) -> int: return struct.unpack(">H", self.take(2))[0]
    def u32(self) -> int: return struct.unpack(">I", self.take(4))[0]
    def i32(self) -> int: return struct.unpack(">i", self.take(4))[0]
    def u64(self) -> int: return struct.unpack(">Q", self.take(8))[0]
    def f32(self) -> float: return struct.unpack(">f", self.take(4))[0]

    def wstr(self) -> str:
        n = self.u16()
        return self.take(n).decode("utf-8", "replace")

    def sref(self) -> str:
        i = self.u16()
        if i >= len(self.strings):
            raise PexError(f"string index {i} out of range at offset {self.p - 2}")
        return self.strings[i]

    def var(self):
        t = self.u8()
        if t == 0: return None
        if t in (1, 2): return self.sref()
        if t == 3: return self.i32()
        if t == 4: return self.f32()
        if t == 5: return bool(self.u8())
        raise PexError(f"bad variable-data type {t} at offset {self.p - 1}")


def read_function(r: Reader) -> dict:
    fn = {
        "returnType": r.sref(),
        "doc": r.sref(),
        "userFlags": r.u32(),
    }
    flags = r.u8()
    fn["global"] = bool(flags & 1)
    fn["native"] = bool(flags & 2)
    fn["params"] = [(r.sref(), r.sref()) for _ in range(r.u16())]
    fn["locals"] = [(r.sref(), r.sref()) for _ in range(r.u16())]
    ninstr = r.u16()
    for _ in range(ninstr):
        op = r.u8()
        if op not in OPCODES:
            raise PexError(f"unknown opcode {op} at offset {r.p - 1}")
        fixed, variadic = OPCODES[op]
        for _ in range(fixed):
            r.var()
        if variadic:
            count = r.var()
            if not isinstance(count, int):
                raise PexError(f"bad variadic count at offset {r.p}")
            for _ in range(count):
                r.var()
    fn["instructions"] = ninstr
    return fn


def parse(path: Path) -> dict:
    r = Reader(path.read_bytes())
    if r.u32() != MAGIC:
        raise PexError("bad magic (not a big-endian Skyrim .pex)")
    major, minor, game = r.u8(), r.u8(), r.u16()
    if major != 3 or game != 1:
        raise PexError(f"unexpected format {major}.{minor} game id {game} (Skyrim is 3.x / 1)")
    info = {"file": str(path), "version": f"{major}.{minor}", "gameId": game}
    info["compiled"] = r.u64()
    info["source"] = r.wstr()
    info["user"] = r.wstr()
    info["machine"] = r.wstr()
    r.strings = [r.wstr() for _ in range(r.u16())]
    if r.u8():  # debug info
        r.u64()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u16(); r.u8()
            for _ in range(r.u16()):
                r.u16()
    info["userFlags"] = {}
    for _ in range(r.u16()):
        name = r.sref()
        info["userFlags"][name] = r.u8()
    objects = []
    for _ in range(r.u16()):
        obj = {"name": r.sref()}
        size = r.u32()
        start = r.p
        obj["parent"] = r.sref()
        obj["doc"] = r.sref()
        obj["userFlags"] = r.u32()
        obj["autoState"] = r.sref()
        obj["variables"] = []
        for _ in range(r.u16()):
            v = {"name": r.sref(), "type": r.sref(), "userFlags": r.u32()}
            v["initial"] = r.var()
            obj["variables"].append(v)
        obj["properties"] = []
        for _ in range(r.u16()):
            pr = {"name": r.sref(), "type": r.sref(), "doc": r.sref(), "userFlags": r.u32()}
            flags = r.u8()
            pr["flags"] = flags
            if flags & 4:
                pr["autoVar"] = r.sref()
            else:
                if flags & 1:
                    pr["get"] = read_function(r)
                if flags & 2:
                    pr["set"] = read_function(r)
            obj["properties"].append(pr)
        obj["states"] = {}
        for _ in range(r.u16()):
            sname = r.sref()
            funcs = {}
            for _ in range(r.u16()):
                fname = r.sref()
                funcs[fname] = read_function(r)
            obj["states"][sname] = funcs
        # The object size field is not used by the game's loader. Caprica (upstream
        # and this build) writes the body length in host (little-endian) byte order;
        # other compilers write it big-endian, some including the field itself.
        # Accept any of those, reject anything else.
        body = r.p - start
        le = struct.unpack("<I", struct.pack(">I", size))[0]
        if body not in (size, size - 4, le, le - 4):
            raise PexError(f"object {obj['name']}: size field {size:#x} does not match body length {body}")
        objects.append(obj)
    if r.p != len(r.d):
        raise PexError(f"{len(r.d) - r.p} trailing bytes after the last object")
    info["objects"] = objects
    return info


def summary(info: dict) -> str:
    out = [f"{info['file']}: pex {info['version']} game {info['gameId']} source {info['source']}"]
    for obj in info["objects"]:
        flags = [n for n, bit in info["userFlags"].items() if obj["userFlags"] & (1 << bit)]
        out.append(f"  Scriptname {obj['name']}" + (f" extends {obj['parent']}" if obj["parent"] else "")
                   + (" " + " ".join(flags) if flags else ""))
        for pr in obj["properties"]:
            kind = "Auto" if "autoVar" in pr else "Full"
            out.append(f"    {pr['type']} Property {pr['name']} ({kind})")
        for sname, funcs in obj["states"].items():
            label = f"state {sname}" if sname else "empty state"
            out.append(f"    [{label}]")
            for fname, fn in funcs.items():
                params = ", ".join(f"{t} {n}" for n, t in fn["params"])
                mods = " ".join(m for m, on in (("global", fn["global"]), ("native", fn["native"])) if on)
                out.append(f"      {fn['returnType']} {fname}({params}) {mods}".rstrip()
                           + ("" if fn["native"] else f"  [{fn['instructions']} instr]"))
    return "\n".join(out)


def check_api(info: dict) -> list[str]:
    errors = []
    obj = next((o for o in info["objects"] if o["name"].lower() == "lostart"), None)
    if obj is None:
        return ["no object named LostArt"]
    funcs = {k.lower(): (k, v) for k, v in obj["states"].get("", {}).items()}
    for name, (ret, params) in LOSTART_API.items():
        entry = funcs.pop(name.lower(), None)
        if entry is None:
            errors.append(f"missing {name}")
            continue
        _, fn = entry
        if not (fn["global"] and fn["native"]):
            errors.append(f"{name}: must be global native")
        if fn["returnType"].lower() != ret.lower():
            errors.append(f"{name}: returns {fn['returnType']}, contract says {ret}")
        got = [t for _, t in fn["params"]]
        if [t.lower() for t in got] != [t.lower() for t in params]:
            errors.append(f"{name}: params {got}, contract says {params}")
    for k, (orig, _) in funcs.items():
        if k not in ("getstate", "gotostate", "onbeginstate", "onendstate"):
            errors.append(f"unexpected function {orig} (not in CONTRACTS section 8)")
    return errors


def main(argv: list[str]) -> int:
    as_json = "--json" in argv
    do_check = "--check-api" in argv
    files = [a for a in argv if not a.startswith("--")]
    if not files:
        print(__doc__)
        return 2
    rc = 0
    for f in files:
        try:
            info = parse(Path(f))
        except (OSError, PexError) as e:
            print(f"{f}: ERROR: {e}", file=sys.stderr)
            rc = 1
            continue
        print(json.dumps(info, indent=1) if as_json else summary(info))
        if do_check:
            errs = check_api(info)
            for e in errs:
                print(f"{f}: API MISMATCH: {e}", file=sys.stderr)
            if errs:
                rc = 1
            else:
                print(f"{f}: API matches CONTRACTS section 8 ({len(LOSTART_API)} global natives)")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
