#!/usr/bin/env python3
"""Structural check of LostArt_Spellmaking.swf (no third-party dependencies).

Parses the SWF container (FWS/CWS), walks every tag, and verifies what the game needs:
  * SWF version 8..10 (Skyrim's Scaleform plays AS2 movies of these versions)
  * the tag stream is well formed and ends with End
  * every AS2 class the menu needs is present as a DoInitAction/ExportAssets pair
    (MTASC packs each class as a "__Packages.<name>" sprite)
  * a frame-1 DoAction exists (the MTASC -main call that builds _root.Menu_mc)
  * the DLL -> SWF entry points appear in the constant pools

Usage: swfcheck.py <file.swf> [--json]
Exit status 0 when every check passes.
"""
import json
import struct
import sys
import zlib

TAG_NAMES = {0: "End", 1: "ShowFrame", 9: "SetBackgroundColor", 12: "DoAction", 39: "DefineSprite",
             43: "FrameLabel", 56: "ExportAssets", 59: "DoInitAction", 69: "FileAttributes", 77: "Metadata"}

REQUIRED_CLASSES = [
    "LostArtMain", "LostArtSpellmakingMenu",
    "gfx.io.GameDelegate", "gfx.ui.NavigationCode",
    "lostart.Theme", "lostart.Sounds",
    "lostart.util.Translator", "lostart.util.Draw", "lostart.util.Text", "lostart.util.Layout", "lostart.util.Tween",
    "lostart.input.InputDetails", "lostart.input.KeyMap", "lostart.input.InputRouter",
    "lostart.model.EffectFilter", "lostart.model.StateUtil",
    "lostart.components.VirtualList", "lostart.components.TabBar", "lostart.components.TextBox",
    "lostart.components.Button", "lostart.components.KeyGlyph", "lostart.components.Slider",
    "lostart.components.ItemCard", "lostart.components.ReadoutBar", "lostart.components.EffectEditor",
    "lostart.components.ListPopup", "lostart.components.MessageBox", "lostart.components.CostMathPanel",
]

REQUIRED_STRINGS = [
    # DLL -> SWF entry points (CONTRACTS.md 7)
    "LA_SetKnown", "LA_SetState", "LA_SetLoadList", "LA_ShowMessage", "LA_Close",
    # SWF -> DLL intents
    "LA_Ready", "LA_SetName", "LA_AddEffect", "LA_PickTarget", "LA_EditEffect", "LA_RemoveEffect",
    "LA_MoveEffect", "LA_EditorRange", "LA_EditorSet", "LA_EditorStep", "LA_EditorOk",
    "LA_EditorCancel", "LA_EditorDelete", "LA_Clear", "LA_LoadList", "LA_Load", "LA_Create",
    "LA_ToggleCostMath", "LA_Exit", "LA_PlaySound",
    # root clip + fonts
    "Menu_mc", "$EverywhereFont", "$EverywhereMediumFont",
]


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
        self.bitbuf = 0
        self.bitcnt = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def bits(self, n):
        v = 0
        for _ in range(n):
            if self.bitcnt == 0:
                self.bitbuf = self.u8()
                self.bitcnt = 8
            self.bitcnt -= 1
            v = (v << 1) | ((self.bitbuf >> self.bitcnt) & 1)
        return v

    def sbits(self, n):
        v = self.bits(n)
        if n and v & (1 << (n - 1)):
            v -= 1 << n
        return v

    def align(self):
        self.bitcnt = 0

    def cstr(self):
        e = self.d.index(b"\0", self.p)
        s = self.d[self.p:e].decode("utf-8", "replace")
        self.p = e + 1
        return s


def parse(path):
    raw = open(path, "rb").read()
    sig = raw[:3]
    if sig not in (b"FWS", b"CWS"):
        raise ValueError("not an SWF (signature %r)" % sig)
    version = raw[3]
    length = struct.unpack_from("<I", raw, 4)[0]
    body = zlib.decompress(raw[8:]) if sig == b"CWS" else raw[8:]
    if len(body) + 8 != length:
        raise ValueError("header length %d != actual %d" % (length, len(body) + 8))
    r = Reader(body)
    nbits = r.bits(5)
    xmin, xmax, ymin, ymax = (r.sbits(nbits) for _ in range(4))
    r.align()
    rate = r.u16() / 256.0
    frames = r.u16()
    tags = []
    exports = {}
    strings = set()
    while r.p < len(body):
        hdr = r.u16()
        code = hdr >> 6
        ln = hdr & 0x3F
        if ln == 0x3F:
            ln = r.u32()
        start = r.p
        payload = body[start:start + ln]
        if len(payload) != ln:
            raise ValueError("truncated tag %d at %d" % (code, start))
        tags.append((code, ln, start))
        if code == 56:  # ExportAssets
            t = Reader(payload)
            for _ in range(t.u16()):
                cid = t.u16()
                exports[t.cstr()] = cid
        if code in (12, 59):  # DoAction / DoInitAction: harvest ConstantPool strings
            t = Reader(payload, 2 if code == 59 else 0)
            while t.p < len(payload):
                op = t.u8()
                if op == 0:
                    break
                if op >= 0x80:
                    alen = t.u16()
                    if op == 0x88:  # ConstantPool
                        pool = Reader(payload[t.p:t.p + alen])
                        for _ in range(pool.u16()):
                            strings.add(pool.cstr())
                    elif op == 0x96:  # Push: string literals (type 0)
                        pr = Reader(payload[t.p:t.p + alen])
                        while pr.p < alen:
                            ty = pr.u8()
                            if ty == 0:
                                strings.add(pr.cstr())
                            elif ty in (1,):
                                pr.p += 4
                            elif ty in (4, 5, 8):
                                pr.p += 1
                            elif ty in (6,):
                                pr.p += 8
                            elif ty in (7,):
                                pr.p += 4
                            elif ty in (9,):
                                pr.p += 2
                    t.p += alen
        r.p = start + ln
        if code == 0:
            break
    return {
        "path": path, "signature": sig.decode(), "version": version, "fileLength": len(raw),
        "uncompressedLength": length, "stage": [(xmax - xmin) / 20.0, (ymax - ymin) / 20.0],
        "frameRate": rate, "frameCount": frames, "tagCount": len(tags),
        "tagHistogram": _histogram(tags), "classes": sorted(k[len("__Packages."):] for k in exports if k.startswith("__Packages.")),
        "lastTag": TAG_NAMES.get(tags[-1][0], tags[-1][0]) if tags else None, "strings": strings,
    }


def _histogram(tags):
    h = {}
    for code, _, _ in tags:
        name = TAG_NAMES.get(code, str(code))
        h[name] = h.get(name, 0) + 1
    return h


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    info = parse(argv[1])
    problems = []
    if not 8 <= info["version"] <= 10:
        problems.append("SWF version %d outside 8..10" % info["version"])
    if info["lastTag"] != "End":
        problems.append("tag stream does not end with End")
    if info["tagHistogram"].get("DoAction", 0) < 1:
        problems.append("no frame DoAction (MTASC -main call missing)")
    missing = [c for c in REQUIRED_CLASSES if c not in info["classes"]]
    if missing:
        problems.append("missing classes: " + ", ".join(missing))
    missing_s = [s for s in REQUIRED_STRINGS if s not in info["strings"]]
    if missing_s:
        problems.append("missing strings: " + ", ".join(missing_s))

    if "--json" in argv:
        out = dict(info)
        out["strings"] = len(info["strings"])
        out["problems"] = problems
        print(json.dumps(out, indent=2))
    else:
        print("%s: %s v%d, %d bytes (%d uncompressed), stage %gx%g @ %g fps, %d frame(s)" % (
            info["path"], info["signature"], info["version"], info["fileLength"], info["uncompressedLength"],
            info["stage"][0], info["stage"][1], info["frameRate"], info["frameCount"]))
        print("  tags: %d  %s" % (info["tagCount"], ", ".join("%s=%d" % kv for kv in sorted(info["tagHistogram"].items()))))
        print("  AS2 classes: %d (all %d required present: %s)" % (
            len(info["classes"]), len(REQUIRED_CLASSES), "yes" if not missing else "NO"))
        print("  protocol strings: %d/%d present" % (len(REQUIRED_STRINGS) - len(missing_s), len(REQUIRED_STRINGS)))
        for p in problems:
            print("  FAIL: " + p)
        print("  result: " + ("OK" if not problems else "FAILED"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
