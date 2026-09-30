#!/usr/bin/env python3
"""Validate the FOMOD installer of Lost Art of Spellmaking.

  python3 tools/validate_fomod.py                   check fomod/ in the repository
  python3 tools/validate_fomod.py --staging DIR     also check that every file and folder
                                                    the installer references exists in an
                                                    assembled package (DIR contains fomod/)

Checks:
  - fomod/ModuleConfig.xml validates against the FOMOD 5.x schema
    (fomod/schema/ModuleConfig.xsd, vendored from github.com/GandaG/fomod-schema, now
    fomod-lang/fomod). The vendored XSD has one upstream typo (type=" xs:string" with a
    leading space on dependencyType/version) that libxml2 refuses to compile; it is
    corrected in memory before validation.
  - fomod/info.xml is well-formed and has Name, Author, Version and Description.
  - with --staging: every <file>/<folder> source exists (case-sensitively, as on Linux;
    Windows mod managers are case-insensitive, so this is the stricter check).
Requires lxml (pip install lxml).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from lxml import etree

ROOT = Path(__file__).resolve().parent.parent


def load_schema(xsd_path: Path) -> etree.XMLSchema:
    raw = xsd_path.read_bytes().replace(b'type=" xs:string"', b'type="xs:string"')
    return etree.XMLSchema(etree.fromstring(raw, base_url=str(xsd_path)))


def validate(fomod_dir: Path, staging: Path | None = None) -> list[str]:
    errors: list[str] = []
    schema = load_schema(ROOT / "fomod" / "schema" / "ModuleConfig.xsd")
    mc_path = fomod_dir / "ModuleConfig.xml"
    try:
        doc = etree.parse(str(mc_path))
    except etree.XMLSyntaxError as e:
        return [f"{mc_path}: not well-formed: {e}"]
    if not schema.validate(doc):
        for e in schema.error_log:
            errors.append(f"{mc_path}:{e.line}: {e.message}")

    info_path = fomod_dir / "info.xml"
    try:
        info = etree.parse(str(info_path)).getroot()
        for tag in ("Name", "Author", "Version", "Description"):
            el = info.find(tag)
            if el is None or not (el.text or "").strip():
                errors.append(f"{info_path}: missing or empty <{tag}>")
    except etree.XMLSyntaxError as e:
        errors.append(f"{info_path}: not well-formed: {e}")

    if staging is not None:
        for el in doc.iter("file", "folder"):
            src = el.get("source", "").replace("\\", "/")
            p = staging / src
            ok = p.is_file() if el.tag == "file" else p.is_dir()
            if not ok:
                errors.append(f"{mc_path}:{el.sourceline}: {el.tag} source '{src}' not in package")
            elif el.tag == "folder" and not any(p.rglob("*")):
                errors.append(f"{mc_path}:{el.sourceline}: folder '{src}' is empty")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fomod", type=Path, default=ROOT / "fomod", help="directory with ModuleConfig.xml and info.xml")
    ap.add_argument("--staging", type=Path, help="assembled package root to check file references against")
    args = ap.parse_args()
    fomod_dir = args.fomod
    if args.staging and args.fomod == ROOT / "fomod":
        fomod_dir = args.staging / "fomod"
    errors = validate(fomod_dir, args.staging)
    for e in errors:
        print(f"ERROR: {e}")
    print("FOMOD validation", "FAILED" if errors else "passed", f"({len(errors)} errors)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
