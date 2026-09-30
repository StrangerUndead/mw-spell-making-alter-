"""pytest: data/ must validate; the validator must catch representative breakage.

Run:  python -m pytest tools/tests -q
"""
import json
import os
import shutil
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, TOOLS)

import calibrate as cal  # noqa: E402
import check_formkeys as ck  # noqa: E402
import validate_data as vd  # noqa: E402

DATA = os.path.join(os.path.dirname(TOOLS), "data")
FORMKEYS = os.environ.get("LA_FORMKEYS", "/opt/deps/formkeys")
HAVE_FK = ck.find_repo(FORMKEYS) is not None


def test_validator_passes():
    assert vd.main(["-q"]) == 0


def test_check_numbers():
    catalog, _ = cal.load_catalog(DATA)
    rows = cal.run_checks(catalog)
    assert len(rows) == 14
    assert all(ok for *_, ok in rows), [r for r in rows if not r[3]]


def test_fire_base_cost_matches_firebolt():
    catalog, _ = cal.load_catalog(DATA)
    b = catalog["mw.fire_damage"]["skyrim"]["baseCost"]
    assert abs(b - 41 / 25 ** 1.1) < 1e-12


@pytest.mark.skipif(not HAVE_FK, reason="Mutagen.Bethesda.FormKeys clone not available")
def test_formkeys_resolve():
    n, errors = ck.check(DATA, FORMKEYS)
    assert n > 100 and errors == []


def test_firebolt_ref():
    # the fire fit spell must be Firebolt (Skyrim.esm SPEL 0x012FD0)
    catalog, _ = cal.load_catalog(DATA)
    assert catalog["mw.fire_damage"]["skyrim"]["fitFrom"]["spell"] == "Skyrim.esm|0x012FD0"


# ----------------------------------------------------------------------------- mutation tests
@pytest.fixture
def data_copy(tmp_path):
    dst = tmp_path / "data"
    shutil.copytree(DATA, dst)
    return dst


def _edit(path, fn):
    doc = json.load(open(path, encoding="utf-8"))
    fn(doc)
    json.dump(doc, open(path, "w", encoding="utf-8"), indent=2)


def _fails(data_dir):
    R = vd.validate(str(data_dir), quiet=True)
    return R.fails


def _effect(doc, eid):
    return next(e for e in doc if e["id"] == eid)


def test_detects_self_area(data_copy):
    _edit(data_copy / "effects" / "mysticism.json", lambda d: _effect(d, "mw.mark").__setitem__("area", True))
    assert any("Self-only" in f for f in _fails(data_copy))


def test_detects_missing_source(data_copy):
    _edit(data_copy / "effects" / "alteration.json", lambda d: _effect(d, "mw.levitate").__setitem__("sources", []))
    assert _fails(data_copy)


def test_detects_bad_tome_effect(data_copy):
    def f(doc):
        doc["tomes"][0]["effects"][0]["id"] = "mw.no_such_effect"
    _edit(data_copy / "content" / "tomes.json", f)
    assert any("unknown effect" in x for x in _fails(data_copy))


def test_detects_missing_translation(data_copy):
    src = data_copy / "translations" / "LostArt_ENGLISH.src.tsv"
    lines = [l for l in open(src, encoding="utf-8") if not l.startswith("$LA_Card_Levitate\t")]
    open(src, "w", encoding="utf-8").writelines(lines)
    fails = _fails(data_copy)
    assert any("out of date" in x for x in fails)
    vd.validate(str(data_copy), write_translations=True, quiet=True)
    assert any("$LA_Card_Levitate" in x for x in _fails(data_copy))


def test_detects_wrong_base_cost(data_copy):
    _edit(data_copy / "effects" / "destruction.json", lambda d: _effect(d, "mw.fire_damage")["skyrim"].__setitem__("baseCost", 1.2))
    assert any("check" in x or "calibrated" in x for x in _fails(data_copy))


def test_detects_count_change(data_copy):
    _edit(data_copy / "effects" / "illusion.json", lambda d: _effect(d, "mw.blind").__setitem__("tier", "native"))
    assert any("tiers" in x for x in _fails(data_copy))


@pytest.mark.skipif(not HAVE_FK, reason="Mutagen.Bethesda.FormKeys clone not available")
def test_detects_unknown_formkey(data_copy):
    _edit(data_copy / "effects" / "destruction.json",
          lambda d: _effect(d, "mw.fire_damage")["skyrim"].__setitem__("vanillaEffect", "Skyrim.esm|0xFFFFFE"))
    n, errors = ck.check(str(data_copy), FORMKEYS)
    assert any("does not exist" in e for e in errors)


@pytest.mark.skipif(not HAVE_FK, reason="Mutagen.Bethesda.FormKeys clone not available")
def test_detects_wrong_record_type(data_copy):
    # Firebolt is a SPEL, not an MGEF
    _edit(data_copy / "effects" / "destruction.json",
          lambda d: _effect(d, "mw.fire_damage")["skyrim"].__setitem__("vanillaEffect", "Skyrim.esm|0x012FD0"))
    n, errors = ck.check(str(data_copy), FORMKEYS)
    assert any("expected a MagicEffect" in e for e in errors)
