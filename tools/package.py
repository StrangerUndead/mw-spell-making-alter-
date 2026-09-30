#!/usr/bin/env python3
"""Assemble the Lost Art of Spellmaking release archive (FOMOD layout) from build outputs.

  python3 tools/package.py [--version 1.0.0] [--out build/package] [--check]

Inputs (relative to the repository root):
  build/plugin/LostArt.dll                        SKSE plugin
  build/plugins/LostArt.esp, LostArt_Slots.esp    generated plugins
  interface/build/LostArt_Spellmaking.swf        spellmaking menu
  build/papyrus/<name>.pex                        one per scripts/Source/<name>.psc
  scripts/Source/*.psc                            script sources (shipped for modders)
  data/**                                         JSON data + translations (data/schema excluded)
  mcm/config.json, mcm/settings.ini               MCM Helper config
  fomod/info.xml, fomod/ModuleConfig.xml          installer

Archive layout (see fomod/ModuleConfig.xml):
  fomod/info.xml, fomod/ModuleConfig.xml
  Core/                          installed to Data/ as-is (CONTRACTS section 1):
    SKSE/Plugins/LostArt.dll
    SKSE/Plugins/LostArt/...     data/**/*.json except schema/ and translations/, keeping
                                 sub-folders (effects/, content/, ...); data/generated/*
                                 lands at the folder root (formmap.json)
    LostArt.esp, LostArt_Slots.esp
    Interface/LostArt_Spellmaking.swf
    Interface/Translations/LostArt_ENGLISH.txt
    Scripts/*.pex, Scripts/Source/*.psc
    MCM/Config/LostArt/config.json, settings.ini
  Translations/LostArt_<LANG>.txt  one per non-English language in ModuleConfig.xml; the
                                   real translation from data/translations when it exists,
                                   otherwise a copy of the English text so the language
                                   still shows words instead of $keys

The script lists every missing input and exits 1 before writing anything. --check only
verifies the inputs. The finished staging tree is validated with tools/validate_fomod.py
(schema + every referenced file exists) before it is zipped.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

# Data files CONTRACTS section 1 names explicitly; every other *.json under data/ is
# shipped too, but these must exist.
REQUIRED_DATA = [
    "content/attributes.json", "content/skills.json", "content/tomes.json",
    "content/spellmakers.json", "content/altars.json", "content/ranks.json",
    "content/riders.json", "standins/stand-ins.json", "discovery/vanilla.json",
    "discovery/rules.json", "generated/formmap.json",
]
DATA_EXCLUDE_DIRS = {"schema", "translations"}
DATA_FLATTEN_DIRS = {"generated"}   # data/generated/x.json -> SKSE/Plugins/LostArt/x.json
UTF16LE_BOM = b"\xff\xfe"
FIXED_TIME = (2026, 1, 1, 0, 0, 0)  # deterministic zip timestamps


def plan(version: str) -> tuple[list[tuple[Path, str]], list[str], list[str]]:
    """Return (copies [(source, archive path)], missing inputs, warnings)."""
    copies: list[tuple[Path, str]] = []
    missing: list[str] = []
    warnings: list[str] = []

    def need(rel: str, dest: str):
        src = ROOT / rel
        if src.is_file():
            copies.append((src, dest))
        else:
            missing.append(rel)

    need("build/plugin/LostArt.dll", "Core/SKSE/Plugins/LostArt.dll")
    need("build/plugins/LostArt.esp", "Core/LostArt.esp")
    need("build/plugins/LostArt_Slots.esp", "Core/LostArt_Slots.esp")
    need("interface/build/LostArt_Spellmaking.swf", "Core/Interface/LostArt_Spellmaking.swf")

    # Papyrus: every source needs its compiled script.
    sources = sorted((ROOT / "scripts" / "Source").glob("*.psc"))
    if not sources:
        missing.append("scripts/Source/*.psc")
    for psc in sources:
        pex_rel = f"build/papyrus/{psc.stem}.pex"
        need(pex_rel, f"Core/Scripts/{psc.stem}.pex")
        pex = ROOT / pex_rel
        if pex.is_file() and pex.stat().st_mtime < psc.stat().st_mtime:
            warnings.append(f"{pex_rel} is older than {psc.relative_to(ROOT)}; run scripts/build.sh")
        copies.append((psc, f"Core/Scripts/Source/{psc.name}"))
    stray = sorted({p.stem for p in (ROOT / "build" / "papyrus").glob("*.pex")} - {p.stem for p in sources})
    for s in stray:
        warnings.append(f"build/papyrus/{s}.pex has no source in scripts/Source; not shipped")

    # JSON data
    data = ROOT / "data"
    for rel in REQUIRED_DATA:
        if not (data / rel).is_file():
            missing.append(f"data/{rel}")
    if not list((data / "effects").glob("*.json")):
        missing.append("data/effects/*.json")
    for f in sorted(data.rglob("*.json")):
        rel = f.relative_to(data)
        top = rel.parts[0]
        if top in DATA_EXCLUDE_DIRS:
            continue
        dest_rel = Path(*rel.parts[1:]) if top in DATA_FLATTEN_DIRS else rel
        copies.append((f, f"Core/SKSE/Plugins/LostArt/{dest_rel.as_posix()}"))

    # Translations
    tr_dir = data / "translations"
    english = tr_dir / "LostArt_ENGLISH.txt"
    if english.is_file():
        if not english.read_bytes().startswith(UTF16LE_BOM):
            missing.append("data/translations/LostArt_ENGLISH.txt (present, but not UTF-16 LE with BOM)")
        copies.append((english, "Core/Interface/Translations/LostArt_ENGLISH.txt"))
    else:
        missing.append("data/translations/LostArt_ENGLISH.txt")
    module_config = (ROOT / "fomod" / "ModuleConfig.xml").read_text(encoding="utf-8") \
        if (ROOT / "fomod" / "ModuleConfig.xml").is_file() else ""
    for lang in sorted(set(re.findall(r"Translations\\LostArt_([A-Z]+)\.txt\"", module_config))):
        real = tr_dir / f"LostArt_{lang}.txt"
        if real.is_file():
            if not real.read_bytes().startswith(UTF16LE_BOM):
                missing.append(f"data/translations/LostArt_{lang}.txt (present, but not UTF-16 LE with BOM)")
            copies.append((real, f"Translations/LostArt_{lang}.txt"))
        elif english.is_file():
            copies.append((english, f"Translations/LostArt_{lang}.txt"))
            warnings.append(f"no {lang} translation yet; the {lang} option installs the English text")
    for extra in sorted(tr_dir.glob("LostArt_*.txt")):
        lang = extra.stem.split("_", 1)[1]
        if lang != "ENGLISH" and f"LostArt_{lang}.txt" not in module_config:
            warnings.append(f"{extra.relative_to(ROOT)} exists but fomod/ModuleConfig.xml offers no {lang} option")

    # MCM Helper
    need("mcm/config.json", "Core/MCM/Config/LostArt/config.json")
    need("mcm/settings.ini", "Core/MCM/Config/LostArt/settings.ini")

    # Installer
    need("fomod/ModuleConfig.xml", "fomod/ModuleConfig.xml")
    need("fomod/info.xml", "fomod/info.xml")
    return copies, missing, warnings


def stage(copies: list[tuple[Path, str]], staging: Path, version: str):
    if staging.exists():
        shutil.rmtree(staging)
    for src, dest in copies:
        out = staging / dest
        out.parent.mkdir(parents=True, exist_ok=True)
        if dest == "fomod/info.xml":
            text = src.read_text(encoding="utf-8")
            text = re.sub(r'<Version MachineVersion="[^"]*">[^<]*</Version>',
                          f'<Version MachineVersion="{re.split(r"[-+]", version)[0]}">{version}</Version>', text)
            out.write_text(text, encoding="utf-8")
        else:
            shutil.copyfile(src, out)


def make_zip(staging: Path, zip_path: Path):
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(p for p in staging.rglob("*") if p.is_file()):
            info = zipfile.ZipInfo(f.relative_to(staging).as_posix(), FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, f.read_bytes())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="1.0.0", help="release version (written into fomod/info.xml)")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "package", help="output directory")
    ap.add_argument("--check", action="store_true", help="only verify that every input exists")
    args = ap.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+([-.][0-9A-Za-z.]+)?", args.version):
        print(f"ERROR: version {args.version!r} is not x.y.z")
        return 2

    copies, missing, warnings = plan(args.version)
    for w in warnings:
        print(f"warning: {w}")
    if missing:
        print(f"ERROR: {len(missing)} missing input(s):")
        for m in missing:
            print(f"  - {m}")
        return 1
    if args.check:
        print(f"all inputs present ({len(copies)} files)")
        return 0

    staging = args.out / "staging"
    stage(copies, staging, args.version)

    import validate_fomod
    errors = validate_fomod.validate(staging / "fomod", staging)
    for e in errors:
        print(f"ERROR: {e}")
    if errors:
        return 1

    zip_path = args.out / f"LostArtOfSpellmaking-{args.version}.zip"
    make_zip(staging, zip_path)
    print(f"wrote {zip_path.relative_to(ROOT) if zip_path.is_relative_to(ROOT) else zip_path} "
          f"({len(copies)} files, {zip_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
