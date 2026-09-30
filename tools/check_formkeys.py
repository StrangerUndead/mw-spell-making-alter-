#!/usr/bin/env python3
"""Verify that every vanilla form reference in data/ resolves in Mutagen.Bethesda.FormKeys.

    python tools/check_formkeys.py [FORMKEYS_REPO] [--data DIR] [--list]

FORMKEYS_REPO is a clone of https://github.com/Mutagen-Modding/Mutagen.Bethesda.FormKeys (the repo root
or its Mutagen.Bethesda.FormKeys.SkyrimSE folder). Default: $LA_FORMKEYS, then /opt/deps/formkeys.

A reference is any string (or substring, e.g. "quest:Dragonborn.esm|0x01DB3A:500") of the form
"<Plugin>.esm|0x<6 hex digits>". Checks:
  * the (plugin, local id) pair exists in the FormKeys tables;
  * where the JSON key implies a record type (spell, vanillaEffect, npc, perk, ...), the form has that type;
  * keyword EditorIDs in "keywords"/"keywordsAny"/"keywordsAll"/"keywordsNone" arrays exist as KYWD records;
  * editor-ID labels stored next to a reference (e.g. discovery "label", "npcLabel", "cellLabel",
    riders "labels", ranks "perkLabels") match the FormKeys EditorID.
Exit status 0 when everything resolves, 1 otherwise.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_REPO = "/opt/deps/formkeys"
PLUGIN_DIRS = {"Skyrim": "Skyrim.esm", "Dawnguard": "Dawnguard.esm", "Dragonborn": "Dragonborn.esm",
               "HearthFires": "HearthFires.esm", "Update": "Update.esm"}
REF_RX = re.compile(r"(Skyrim|Dawnguard|Dragonborn|HearthFires|Update)\.esm\|0x([0-9A-Fa-f]{6})")
CS_RX = re.compile(r"public static FormLink<\w+> (\w+) => Construct\((0x[0-9a-fA-F]+)\)")

# JSON key (nearest ancestor that appears here) -> accepted record types
KEY_TYPES = {
    "spell": {"Spell"}, "sources": {"Spell"}, "vanillaEffect": {"MagicEffect"}, "projectile": {"Projectile"},
    "creature": {"Npc"}, "weapon": {"Weapon"}, "armor": {"Armor"}, "perk": {"Perk"}, "perks": {"Perk"},
    "npc": {"Npc"}, "merchantChest": {"Container"}, "crimeFaction": {"Faction"}, "collegeFaction": {"Faction"},
    "locationRef": {"Location"}, "cell": {"Cell"}, "owner": {"Faction", "Npc"}, "freeForFaction": {"Faction"},
    "lootLists": {"LeveledItem"}, "variants": {"MagicEffect"}, "refusal": {"Quest"},
}
# per-file override for the generic key "effect"
FILE_KEY_TYPES = {"vanilla.json": {"effect": {"MagicEffect"}}, "riders.json": {"effect": {"MagicEffect"}}}


def find_repo(path):
    cands = [path, os.path.join(path, "Mutagen.Bethesda.FormKeys.SkyrimSE")]
    for c in cands:
        if os.path.isdir(os.path.join(c, "Skyrim")) and os.path.isfile(os.path.join(c, "Skyrim", "Spell.cs")):
            return c
    return None


def load_index(repo):
    """Return {(plugin, id): [(type, editorId), ...]} and {(plugin, type, editorId): id}."""
    sk = find_repo(repo)
    if not sk:
        raise FileNotFoundError(f"no Mutagen.Bethesda.FormKeys SkyrimSE tables under {repo!r}")
    by_id, by_name = {}, {}
    for d, plugin in PLUGIN_DIRS.items():
        for cs in glob.glob(os.path.join(sk, d, "*.cs")):
            typ = os.path.basename(cs)[:-3]
            with open(cs, encoding="utf-8") as f:
                for m in CS_RX.finditer(f.read()):
                    fid = int(m.group(2), 16)
                    by_id.setdefault((plugin, fid), []).append((typ, m.group(1)))
                    by_name[(plugin, typ, m.group(1))] = fid
    return by_id, by_name


def iter_refs(node, path=()):
    """Yield (path, dict_parent, key, string) for every string in the JSON tree."""
    if isinstance(node, dict):
        for k, v in node.items():
            if isinstance(v, str):
                yield path + (k,), node, k, v
            else:
                yield from iter_refs(v, path + (k,))
    elif isinstance(node, list):
        for i, v in enumerate(node):
            if isinstance(v, str):
                yield path + (i,), None, i, v
            else:
                yield from iter_refs(v, path + (i,))


def expected_types(path, fname):
    over = FILE_KEY_TYPES.get(fname, {})
    for k in reversed(path):
        if isinstance(k, str):
            if k in over:
                return over[k]
            if k in KEY_TYPES:
                return KEY_TYPES[k]
    return None


def check(data_dir, repo, verbose=False, out=print):
    by_id, by_name = load_index(repo)
    keywords = {e for (_, t, e) in by_name if t == "Keyword"}
    errors, count = [], 0
    files = [p for p in sorted(glob.glob(os.path.join(data_dir, "**", "*.json"), recursive=True))
             if os.sep + "schema" + os.sep not in p and os.sep + "generated" + os.sep not in p]
    for path in files:
        rel = os.path.relpath(path, data_dir)
        fname = os.path.basename(path)
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        for jpath, parent, key, s in iter_refs(doc):
            if len(jpath) >= 2 and jpath[-2] in ("keywords", "keywordsAny", "keywordsAll", "keywordsNone"):
                count += 1
                if s not in keywords:
                    errors.append(f"{rel}:{'/'.join(map(str, jpath))}: keyword {s!r} is not a vanilla KYWD EditorID")
                continue
            for m in REF_RX.finditer(s):
                count += 1
                plugin, fid = m.group(1) + ".esm", int(m.group(2), 16)
                hits = by_id.get((plugin, fid))
                where = f"{rel}:{'/'.join(map(str, jpath))}"
                if not hits:
                    errors.append(f"{where}: {m.group(0)} does not exist in FormKeys")
                    continue
                want = expected_types(jpath, fname)
                if want and not any(t in want for t, _ in hits):
                    errors.append(f"{where}: {m.group(0)} is {hits}, expected a {'/'.join(sorted(want))}")
                    continue
                edids = {e for _, e in hits}
                # sibling label checks
                if isinstance(parent, dict):
                    lab = parent.get(f"{key}Label") if isinstance(key, str) else None
                    if fname == "vanilla.json" and key == "effect":
                        lab = parent.get("label")
                    if isinstance(lab, str) and lab not in edids:
                        errors.append(f"{where}: label {lab!r} != FormKeys EditorID {sorted(edids)}")
                if verbose:
                    out(f"  {where}: {m.group(0)} = {', '.join(f'{t}:{e}' for t, e in hits)}")
        # label arrays (riders "labels" follow "variants"/"effect"; ranks "perkLabels" mirror "perks")
        if fname == "ranks.json":
            for school, ranks in doc.get("perkLabels", {}).items():
                for rank, lab in ranks.items():
                    r = doc["perks"][school][rank]
                    m = REF_RX.fullmatch(r)
                    hits = by_id.get((m.group(1) + ".esm", int(m.group(2), 16)), [])
                    if lab not in {e for _, e in hits}:
                        errors.append(f"{rel}:perkLabels/{school}/{rank}: {lab!r} != {hits}")
        if fname == "riders.json":
            for rd in doc.get("riders", []):
                refs = list(rd.get("variants", {}).values()) or [rd["effect"]]
                names = set()
                for r in refs + [rd["effect"]]:
                    m = REF_RX.fullmatch(r)
                    names |= {e for _, e in by_id.get((m.group(1) + ".esm", int(m.group(2), 16)), [])}
                for lab in rd.get("labels", []):
                    if lab not in names:
                        errors.append(f"{rel}:riders/{rd['id']}/labels: {lab!r} is not one of its effects {sorted(names)}")
    return count, errors


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo", nargs="?", default=os.environ.get("LA_FORMKEYS", DEFAULT_REPO))
    ap.add_argument("--data", default=os.path.join(ROOT, "data"))
    ap.add_argument("--list", action="store_true", help="print every reference with its record type and EditorID")
    args = ap.parse_args(argv)
    try:
        count, errors = check(args.data, args.repo, verbose=args.list)
    except FileNotFoundError as e:
        print(f"check_formkeys: {e}", file=sys.stderr)
        return 2
    for e in errors:
        print("FAIL " + e)
    print(f"check_formkeys: {count} references, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
