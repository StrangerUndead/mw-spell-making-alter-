#!/usr/bin/env python3
"""Validate everything under data/ for Lost Art of Spellmaking.

    python tools/validate_data.py [--data DIR] [--formkeys REPO] [--write-translations] [-q]

Checks (exit status 1 if any fails):
  * JSON Schema (draft 2020-12) validation of every data file against data/schema/*.schema.json
  * catalog counts: 119 Morrowind effects = 65 native + 44 custom + 10 stand-in; per Morrowind school
    Alteration 14, Conjuration 29, Destruction 21, Illusion 18, Mysticism 15, Restoration 22;
    13 Extended Morrowind effects; at least one Skyrim-only effect
  * unique ids and Morrowind indices; id <-> English name agreement; Skyrim school re-homing
  * ranges non-empty, Self-only effects have no area, flags consistent (no magnitude => unit none)
  * sub-target families declared (attributes.json / skills.json effect lists, $LA_EffectFmt_ keys)
  * every effect has >= 1 source; tome sources exist and teach the effect; spell sources are refs
  * every tome effect exists, uses an allowed range, a valid sub-index and consistent duration/area
  * riders, stand-ins, discovery maps/rules, spellmakers (15), altars (2), ranks reference valid data
  * stored skyrim.baseCost equals tools/calibrate.py's fit
  * the four Skyrim-balanced check numbers and the Classic golden vectors
  * every translation key used by data exists in every data/translations/LostArt_*.txt, and the
    UTF-16 LE English file is byte-identical to the one regenerated from LostArt_ENGLISH.src.tsv
    (--write-translations regenerates it)
  * every vanilla reference resolves in Mutagen.Bethesda.FormKeys (tools/check_formkeys.py), when the
    FormKeys clone is available ($LA_FORMKEYS or /opt/deps/formkeys); otherwise reported as skipped
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import calibrate as cal  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, ".."))
DATA = os.path.join(ROOT, "data")

try:
    import jsonschema
    from referencing import Registry, Resource
except ImportError:  # pragma: no cover
    sys.exit("validate_data: needs the 'jsonschema' package (pip install jsonschema)")

SCHEMA_FOR = {
    "effects/*.json": "effect.schema.json",
    "standins/stand-ins.json": "standins.schema.json",
    "discovery/vanilla.json": "discovery-vanilla.schema.json",
    "discovery/rules.json": "discovery-rules.schema.json",
    "content/attributes.json": "attributes.schema.json",
    "content/skills.json": "skills.schema.json",
    "content/tomes.json": "tomes.schema.json",
    "content/spellmakers.json": "spellmakers.schema.json",
    "content/altars.json": "altars.schema.json",
    "content/ranks.json": "ranks.schema.json",
    "content/riders.json": "riders.schema.json",
    "generated/formmap.json": "formmap.schema.json",
}
REQUIRED_FILES = [k for k in SCHEMA_FOR if k != "generated/formmap.json"]

MW_SCHOOL_COUNTS = {"Alteration": 14, "Conjuration": 29, "Destruction": 21, "Illusion": 18, "Mysticism": 15, "Restoration": 22}
MW_TIER_COUNTS = {"native": 65, "custom": 44, "standin": 10}
# Skyrim homes that differ from the Morrowind school (outline: Turn Undead, Light, Paralyze, every Mysticism effect)
REHOMED = {"mw.turn_undead": "Restoration", "mw.light": "Alteration", "mw.paralyze": "Alteration"}
MYSTICISM_HOMES = {
    "mw.absorb_attribute": "Destruction", "mw.absorb_fatigue": "Destruction", "mw.absorb_health": "Destruction",
    "mw.almsivi_intervention": "Conjuration", "mw.detect_animal": "Alteration", "mw.detect_enchantment": "Alteration",
    "mw.detect_key": "Alteration", "mw.dispel": "Restoration", "mw.divine_intervention": "Conjuration", "mw.mark": "Conjuration",
    "mw.recall": "Conjuration", "mw.reflect": "Alteration", "mw.soultrap": "Conjuration", "mw.spell_absorption": "Alteration",
    "mw.telekinesis": "Alteration",
}
REF_RX = re.compile(r"^(Skyrim|Dawnguard|Dragonborn|HearthFires|Update)\.esm\|0x[0-9A-F]{6}$")
KEY_RX = re.compile(r"\$LA_[A-Za-z0-9_]+")


def snake(name):
    return re.sub(r"[\s\-]+", "_", name.replace("'", "").replace("(", "").replace(")", "")).lower()


def pascal(name):
    return re.sub(r"[\s\-'()]", "", name)


# ----------------------------------------------------------------------------- translations
def read_src_tsv(path):
    entries = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#"):
                continue
            if "\t" not in line:
                raise ValueError(f"{path}:{n}: expected key<TAB>value")
            k, v = line.split("\t", 1)
            entries.append((k, v))
    return entries


def render_utf16(entries):
    text = "".join(f"{k}\t{v}\r\n" for k, v in entries)
    return b"\xff\xfe" + text.encode("utf-16-le")


def read_translation(path):
    raw = open(path, "rb").read()
    if not raw.startswith(b"\xff\xfe"):
        raise ValueError(f"{path}: missing UTF-16 LE BOM")
    out = {}
    for line in raw[2:].decode("utf-16-le").splitlines():
        if line and "\t" in line:
            k, v = line.split("\t", 1)
            out[k] = v
    return out


class Report:
    def __init__(self, quiet=False):
        self.fails, self.passes, self.notes, self.quiet = [], 0, [], quiet

    def ok(self, cond, msg):
        if cond:
            self.passes += 1
        else:
            self.fails.append(msg)
        return cond

    def fail(self, msg):
        self.fails.append(msg)

    def note(self, msg):
        self.notes.append(msg)


def load_schemas(schema_dir):
    reg = Registry()
    schemas = {}
    for p in glob.glob(os.path.join(schema_dir, "*.schema.json")):
        s = json.load(open(p, encoding="utf-8"))
        jsonschema.Draft202012Validator.check_schema(s)
        reg = reg.with_resource(s["$id"], Resource.from_contents(s))
        schemas[os.path.basename(p)] = s
    return schemas, reg


def validate(data_dir=DATA, formkeys=None, write_translations=False, quiet=False, out=print):
    R = Report(quiet)
    schemas, reg = load_schemas(os.path.join(data_dir, "schema"))

    # ------------------------------------------------------------------ schema validation
    docs = {}
    for pattern, sname in SCHEMA_FOR.items():
        paths = sorted(glob.glob(os.path.join(data_dir, pattern)))
        if not paths:
            if pattern in REQUIRED_FILES:
                R.fail(f"missing data file(s): {pattern}")
            continue
        validator = jsonschema.Draft202012Validator(schemas[sname], registry=reg)
        for p in paths:
            rel = os.path.relpath(p, data_dir).replace(os.sep, "/")
            try:
                doc = json.load(open(p, encoding="utf-8"))
            except json.JSONDecodeError as e:
                R.fail(f"{rel}: invalid JSON: {e}")
                continue
            docs[rel] = doc
            errs = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
            for e in errs[:20]:
                R.fail(f"{rel}: schema: {'/'.join(map(str, e.absolute_path))}: {e.message}")
            R.ok(not errs, f"{rel}: {len(errs)} schema errors")
    if R.fails:
        return R  # later checks assume schema-valid data

    effects = [e for rel, d in docs.items() if rel.startswith("effects/") for e in d]
    catalog = {}
    for e in effects:
        R.ok(e["id"] not in catalog, f"duplicate effect id {e['id']}")
        catalog[e["id"]] = e
    tomes = {t["id"]: t for t in docs["content/tomes.json"]["tomes"]}
    R.ok(len(tomes) == len(docs["content/tomes.json"]["tomes"]), "duplicate tome ids")
    riders = {r["id"]: r for r in docs["content/riders.json"]["riders"]}
    R.ok(len(riders) == len(docs["content/riders.json"]["riders"]), "duplicate rider ids")

    # ------------------------------------------------------------------ translations
    src_path = os.path.join(data_dir, "translations", "LostArt_ENGLISH.src.tsv")
    eng_path = os.path.join(data_dir, "translations", "LostArt_ENGLISH.txt")
    entries = read_src_tsv(src_path)
    keys = [k for k, _ in entries]
    dups = [k for k, c in Counter(keys).items() if c > 1]
    R.ok(not dups, f"translation source has duplicate keys: {dups[:10]}")
    for k, v in entries:
        R.ok(KEY_RX.fullmatch(k) is not None, f"translation key {k!r} does not match $LA_ key syntax")
        R.ok(v.strip() != "", f"translation {k} is empty")
    blob = render_utf16(entries)
    if write_translations:
        with open(eng_path, "wb") as f:
            f.write(blob)
        R.note(f"wrote {os.path.relpath(eng_path, data_dir)} ({len(entries)} strings)")
    if R.ok(os.path.exists(eng_path), "LostArt_ENGLISH.txt missing (run with --write-translations)"):
        R.ok(open(eng_path, "rb").read() == blob, "LostArt_ENGLISH.txt is out of date with LostArt_ENGLISH.src.tsv (run --write-translations)")
    tfiles = {os.path.basename(p): read_translation(p) for p in sorted(glob.glob(os.path.join(data_dir, "translations", "LostArt_*.txt")))}
    eng = tfiles.get("LostArt_ENGLISH.txt", {})

    used_keys = set()

    def collect_keys(node):
        if isinstance(node, dict):
            for v in node.values():
                collect_keys(v)
        elif isinstance(node, list):
            for v in node:
                collect_keys(v)
        elif isinstance(node, str) and KEY_RX.fullmatch(node):
            used_keys.add(node)

    for rel, d in docs.items():
        collect_keys(d)
    for e in effects:
        if e["target"] != "none":
            used_keys.add("$LA_EffectFmt_" + e["name"].replace("$LA_Effect_", ""))
    required_families = ["$LA_Msg_sNotifyMessage28", "$LA_Msg_sOnetypeEffectMessage", "$LA_Msg_sNotifyMessage30", "$LA_Msg_sNotifyMessage10",
                         "$LA_Msg_sEnchantmentMenu8", "$LA_Msg_sNotifyMessage18", "$LA_Msg_sQuestionDeleteSpell", "$LA_Msg_sDeleteSpellError",
                         "$LA_Msg_SpellbookFull", "$LA_Msg_AltarMembersOnly", "$LA_Msg_SpellmakerMembersOnly", "$LA_Msg_DuplicateName",
                         "$LA_Msg_TooComplex", "$LA_Fmt_To", "$LA_Fmt_For", "$LA_Fmt_In", "$LA_Fmt_On", "$LA_Unit_pt", "$LA_Unit_pts",
                         "$LA_Unit_percent", "$LA_Unit_ft", "$LA_Unit_Level", "$LA_Unit_Levels", "$LA_Unit_sec", "$LA_Unit_secs",
                         "$LA_Range_Self", "$LA_Range_Touch", "$LA_Range_Target", "$LA_UI_Create", "$LA_UI_Buy",
                         "$LA_Book_FragmentsOnTheLostArt", "$LA_Book_SpellmakersPrimer", "$LA_Book_OnTelvanniInscription"]
    required_families += [f"$LA_School_{s}" for s in MW_SCHOOL_COUNTS] + [f"$LA_Rank_{r}" for r in cal.RANKS]
    required_families += [f"$LA_UI_{k}" for k in ["Name", "EffectsKnown", "SpellEffects", "Magicka", "Price", "Gold", "Chance", "Rank", "Load",
                                                  "Clear", "Exit", "OK", "Cancel", "Delete", "Range", "Magnitude", "Duration", "Area", "To",
                                                  "SearchHint", "CostModel_SkyrimBalanced", "CostModel_Classic", "CostModel_EngineAutocalc",
                                                  "Hint_Keyboard", "Hint_Gamepad"]]
    used_keys |= set(required_families)
    for lang, table in tfiles.items():
        missing = sorted(k for k in used_keys if k not in table)
        R.ok(not missing, f"{lang}: {len(missing)} missing keys, e.g. {missing[:8]}")
    msgs = {"$LA_Msg_sNotifyMessage28": "You can only add eight effects to a spell.",
            "$LA_Msg_sOnetypeEffectMessage": "This effect has already been added.",
            "$LA_Msg_sNotifyMessage30": "You have to add at least one effect to a spell.",
            "$LA_Msg_sNotifyMessage10": "You have to name the spell before buying it.",
            "$LA_Msg_sEnchantmentMenu8": "You cannot buy a spell that has a zero point cost.",
            "$LA_Msg_sNotifyMessage18": "You don't have enough gold to buy this spell.",
            "$LA_Msg_sQuestionDeleteSpell": "Are you sure you wish to delete %s?",
            "$LA_Msg_sDeleteSpellError": "You cannot delete this item from the Magic Menu"}
    for k, v in msgs.items():
        R.ok(eng.get(k) == v, f"Morrowind message {k} must read exactly {v!r}, got {eng.get(k)!r}")
    for k, v in eng.items():
        if k.startswith("$LA_EffectFmt_"):
            R.ok("{0}" in v, f"{k} must contain {{0}}")

    # ------------------------------------------------------------------ counts
    mw = [e for e in effects if e["set"] == "mw"]
    R.ok(len(mw) == 119, f"expected 119 Morrowind effects, found {len(mw)}")
    tc = Counter(e["tier"] for e in mw)
    R.ok(dict(tc) == MW_TIER_COUNTS, f"Morrowind tiers {dict(tc)} != {MW_TIER_COUNTS}")
    sc = Counter(e["morrowind"]["school"] for e in mw)
    R.ok(dict(sc) == MW_SCHOOL_COUNTS, f"Morrowind schools {dict(sc)} != {MW_SCHOOL_COUNTS}")
    mwx = [e for e in effects if e["set"] == "mwx"]
    R.ok(len(mwx) == 13, f"expected 13 Extended Morrowind effects, found {len(mwx)}")
    sk = [e for e in effects if e["set"] == "sk"]
    R.ok(len(sk) >= 1, "no Skyrim-only effects")
    idx = Counter(e["morrowind"]["index"] for e in mw + mwx)
    R.ok(all(c == 1 for c in idx.values()), f"duplicate Morrowind indices: {[i for i, c in idx.items() if c > 1]}")

    # ------------------------------------------------------------------ per-effect checks
    attr_decl = set(docs["content/attributes.json"]["effects"])
    skill_decl = set(docs["content/skills.json"]["effects"])
    standins = docs["standins/stand-ins.json"]["standins"]
    standin_effects = Counter(s["effect"] for s in standins)
    for e in effects:
        eid = e["id"]
        pas = e["name"].replace("$LA_Effect_", "")
        R.ok(e["itemCard"] == f"$LA_Card_{pas}", f"{eid}: itemCard key should be $LA_Card_{pas}")
        if "nameSkyrim" in e:
            R.ok(e["nameSkyrim"] == f"$LA_EffectSk_{pas}", f"{eid}: nameSkyrim key should be $LA_EffectSk_{pas}")
        en = eng.get(e["name"])
        if en is not None:
            R.ok(f"{e['set']}.{snake(en)}" == eid, f"{eid}: id does not match English name {en!r}")
            R.ok(pascal(en) == pas, f"{eid}: name key {e['name']} is not the Pascal form of {en!r}")
        R.ok(e["id"].startswith(e["set"] + "."), f"{eid}: id prefix does not match set {e['set']}")
        R.ok(len(e["ranges"]) > 0, f"{eid}: no ranges")
        R.ok(e["ranges"] == [r for r in ("self", "touch", "target") if r in e["ranges"]], f"{eid}: ranges not in Morrowind order")
        if e["ranges"] == ["self"]:
            R.ok(e["area"] is False, f"{eid}: Self-only effect must not have area")
        else:
            R.ok(e["area"] is True, f"{eid}: Touch/Target effect should allow area (Morrowind rule)")
        if not e["magnitude"]["has"]:
            R.ok(e["magnitude"]["unit"] == "none" and all(v == 1 for v in e.get("rankLadder", {}).values()),
                 f"{eid}: magnitude-less effect must use unit 'none' and ladder thresholds of 1")
        if e["magnitude"]["ticking"]:
            R.ok(e["duration"], f"{eid}: ticking effect without duration")
        if e["set"] == "mw":
            ms = e["morrowind"]["school"]
            want = REHOMED.get(eid) or MYSTICISM_HOMES.get(eid) or ms
            R.ok(ms != "Mysticism" or eid in MYSTICISM_HOMES, f"{eid}: Mysticism effect without a declared Skyrim home")
            R.ok(e["skyrim"]["school"] == want, f"{eid}: Skyrim school {e['skyrim']['school']} != {want}")
        if e["tier"] == "custom":
            R.ok(e["skyrim"]["archetype"] == "Script", f"{eid}: custom effects use the Script archetype")
        if e["tier"] == "standin":
            R.ok(standin_effects.get(eid) == 1, f"{eid}: stand-in tier needs exactly one stand-ins.json entry")
        # targets
        if e["target"] == "attribute":
            R.ok(eid in attr_decl, f"{eid}: attribute family not declared in attributes.json effects")
        if e["target"] == "skill":
            R.ok(eid in skill_decl, f"{eid}: skill family not declared in skills.json effects")
        # riders
        for r in e["riders"]:
            R.ok(r in riders, f"{eid}: unknown rider {r}")
        # ladder monotonic
        lad = e.get("rankLadder")
        if lad:
            vals = [lad[r] for r in cal.RANKS if r in lad]
            R.ok(vals == sorted(vals) and len(set(vals)) == len(vals), f"{eid}: rankLadder not strictly increasing")
        # sources
        R.ok(len(e["sources"]) >= 1, f"{eid}: no source (vanilla/DLC spell or tome)")
        for s in e["sources"]:
            if s.startswith("tome:"):
                t = tomes.get(s[5:])
                if R.ok(t is not None, f"{eid}: source {s} does not exist"):
                    R.ok(any(te["id"] == eid for te in t["effects"]), f"{eid}: tome {s} does not teach it")
            else:
                R.ok(REF_RX.match(s) is not None, f"{eid}: bad source {s}")
        # vanilla fit sanity
        fit = e["skyrim"].get("fitFrom")
        if fit and fit.get("kind") == "morrowind":
            R.ok(fit["tome"] in tomes and any(te["id"] == eid for te in tomes[fit["tome"]]["effects"]),
                 f"{eid}: fitFrom tome {fit['tome']} does not teach it")
    R.ok(attr_decl == {e["id"] for e in effects if e["target"] == "attribute"}, "attributes.json effects list != attribute-target effects")
    R.ok(skill_decl == {e["id"] for e in effects if e["target"] == "skill"}, "skills.json effects list != skill-target effects")

    # ------------------------------------------------------------------ tomes
    nsky = len(docs["content/skills.json"]["skyrimSkills"])
    for t in tomes.values():
        for te in t["effects"]:
            e = catalog.get(te["id"])
            if not R.ok(e is not None, f"tome {t['id']}: unknown effect {te['id']}"):
                continue
            R.ok(te["range"] in e["ranges"], f"tome {t['id']}: {te['id']} cannot be cast on {te['range']}")
            R.ok(te["min"] <= te["max"], f"tome {t['id']}: {te['id']} min > max")
            R.ok(te["min"] >= 1, f"tome {t['id']}: {te['id']} magnitude < 1")
            if e["target"] == "attribute":
                R.ok(0 <= te["sub"] <= 7, f"tome {t['id']}: {te['id']} needs an attribute sub 0-7")
            elif e["target"] == "skill":
                R.ok(0 <= te["sub"] < nsky or 100 <= te["sub"] <= 126, f"tome {t['id']}: {te['id']} needs a skill sub")
            else:
                R.ok(te["sub"] == -1, f"tome {t['id']}: {te['id']} must have sub -1")
            if te["range"] == "self" or not e["area"]:
                R.ok(te["area"] == 0, f"tome {t['id']}: {te['id']} has area on Self")
            if not e["duration"]:
                R.ok(te["duration"] <= 1, f"tome {t['id']}: {te['id']} has no duration")
            else:
                R.ok(te["duration"] >= 1, f"tome {t['id']}: {te['id']} needs duration >= 1")
        dupl = Counter((te["id"], te["sub"]) for te in t["effects"])
        R.ok(all(c == 1 for c in dupl.values()), f"tome {t['id']}: repeats an effect (Morrowind forbids it)")
    R.ok(40 <= len([t for t in tomes.values() if not t.get("extended")]) <= 80, "expected about 50 Morrowind tomes")
    for e in mwx:
        R.ok(any(s.startswith("tome:") for s in e["sources"]), f"{e['id']}: Extended effects are taught by rare tomes")

    # ------------------------------------------------------------------ other files
    for s in standins:
        R.ok(s["effect"] in catalog and catalog[s["effect"]]["tier"] == "standin", f"stand-in {s['effect']}: not a stand-in-tier catalog effect")
        e = catalog.get(s["effect"])
        if e and "nameSkyrim" in e:
            R.ok(s["nameKey"] == e["nameSkyrim"], f"stand-in {s['effect']}: nameKey != effect nameSkyrim")
    R.ok(len([s for s in standins if catalog.get(s["effect"], {}).get("set") == "mw"]) == 10, "expected 10 Morrowind stand-ins")
    for m in docs["discovery/vanilla.json"]["map"]:
        R.ok(m["id"] in catalog, f"discovery/vanilla.json: unknown effect id {m['id']}")
    pairs = Counter((m["effect"], m["id"]) for m in docs["discovery/vanilla.json"]["map"])
    R.ok(all(c == 1 for c in pairs.values()), "discovery/vanilla.json has duplicate (effect, id) pairs")
    for rule in docs["discovery/rules.json"]["rules"]:
        for i in [rule["id"]] + rule.get("alsoIds", []):
            R.ok(i in catalog, f"discovery/rules.json: unknown effect id {i}")
    sm = docs["content/spellmakers.json"]["spellmakers"]
    R.ok(len(sm) == 15, f"expected 15 spellmakers, found {len(sm)}")
    R.ok(len({s["id"] for s in sm}) == len(sm), "duplicate spellmaker ids")
    R.ok(len(docs["content/altars.json"]["altars"]) == 2, "expected 2 altars")
    R.ok(docs["content/ranks.json"]["costThresholds"] == [60, 130, 250, 500], "rank cost thresholds must be 60/130/250/500")
    for sc_ in [t["mwSchool"] for t in tomes.values() if t.get("vendors")]:
        R.ok(any(sc_ in s["specialties"] for s in sm), f"no spellmaker sells {sc_} tomes")

    # ------------------------------------------------------------------ costs
    K, fits = cal.fit_all(catalog)
    for eid, (b, _) in fits.items():
        stored = catalog[eid]["skyrim"]["baseCost"]
        R.ok(math.isclose(stored, b, rel_tol=1e-9), f"{eid}: stored baseCost {stored} != calibrated {b} (run tools/calibrate.py --write)")
    for eid, e in catalog.items():
        R.ok(eid in fits, f"{eid}: no fitFrom (every effect needs a documented base-cost fit)")
    checks = cal.run_checks(catalog)
    for label, want, got, ok in checks:
        R.ok(ok, f"cost check {label}: expected {want}, got {got}")

    # ------------------------------------------------------------------ form keys
    repo = formkeys or os.environ.get("LA_FORMKEYS") or "/opt/deps/formkeys"
    import check_formkeys as ck
    fk_status = "skipped"
    try:
        n, errs = ck.check(data_dir, repo)
        for x in errs:
            R.fail("formkeys: " + x)
        fk_status = f"{n} references, {len(errs)} errors"
    except FileNotFoundError as ex:
        R.note(f"formkeys check skipped: {ex}")

    # ------------------------------------------------------------------ summary
    R.summary = {
        "effects": f"{len(effects)} (mw {len(mw)} = native {tc['native']} + custom {tc['custom']} + standin {tc['standin']}; mwx {len(mwx)}; sk {len(sk)})",
        "mw schools": ", ".join(f"{k} {sc[k]}" for k in MW_SCHOOL_COUNTS),
        "tomes": f"{len(tomes)} ({len([t for t in tomes.values() if t.get('extended')])} extended)",
        "stand-ins": str(len(standins)),
        "discovery": f"{len(docs['discovery/vanilla.json']['map'])} vanilla MGEF mappings, {len(docs['discovery/rules.json']['rules'])} rules",
        "spellmakers/altars": f"{len(sm)}/{len(docs['content/altars.json']['altars'])}",
        "translations": f"{len(entries)} English strings; languages: {', '.join(sorted(tfiles))}",
        "scale K": f"{K:.6f}",
        "formkeys": fk_status,
    }
    R.checks = checks
    return R


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DATA)
    ap.add_argument("--formkeys", default=None, help="Mutagen.Bethesda.FormKeys clone (default $LA_FORMKEYS or /opt/deps/formkeys)")
    ap.add_argument("--write-translations", action="store_true", help="regenerate LostArt_ENGLISH.txt from the UTF-8 source")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)
    R = validate(args.data, args.formkeys, args.write_translations, args.quiet)
    for n in R.notes:
        print("note: " + n)
    if not args.quiet:
        for label, want, got, ok in getattr(R, "checks", []):
            print(f"[{'PASS' if ok else 'FAIL'}] {label} = {got}")
        for k, v in getattr(R, "summary", {}).items():
            print(f"{k:>20}: {v}")
    for f in R.fails:
        print("FAIL " + f)
    print(f"validate_data: {R.passes} checks passed, {len(R.fails)} failed")
    return 1 if R.fails else 0


if __name__ == "__main__":
    sys.exit(main())
