#!/usr/bin/env python3
"""Cost formulas and Skyrim-balanced base-cost calibration for Lost Art of Spellmaking.

This module is both a library (imported by tools/validate_data.py) and a CLI:

    python tools/calibrate.py            # print every fit, the scale K and the check numbers
    python tools/calibrate.py --write    # also write the fitted skyrim.baseCost into data/effects/*.json

Formulas (docs/OUTLINE.md "Formulas" and "Skyrim-balanced formula"):

  Classic (Morrowind):  C_i = B [(Mmin+Mmax)(D+1) + max(1,A)] / 40
                        y_i = (y_{i-1} + max(1, C_i)) * (1.5 if Target)      cost = floor(y_n)
                        price = max(1, floor(7 y_n))
      (magnitude and duration count as 1 when an effect has none)

  Skyrim-balanced:      c_i = B_sk * T^1.1 * (1 + A/16)
                        T = Mbar * max(1, D)            ticking effects
                        T = Mbar * max(D, 10) / 10      held effects
                        Mbar = max(1, (Mmin+Mmax)/2)
                        x1.5 on Target unless vanillaTargetPriced
                        cost = floor(sum c_i)  (1e-9 epsilon), price = max(1, floor(3 * sum))

Calibration of B_sk (documented in docs/data-notes.md):
  * kind "vanilla":   B_sk = vanillaCost / (T^1.1 (1 + A/16)) from the vanilla spell in fitFrom
                      (skill 0, no perks).
  * kind "morrowind": no vanilla analogue. K = median over all vanilla fits of
                      vanillaCost / ClassicSelfCost(same M, D, A); then
                      B_sk = K * ClassicSelfCost(reference spell) / (T^1.1 (1 + A/16)),
                      where the reference spell is the Morrowind spell of the tome named in fitFrom.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import statistics
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, "data")
EPS = 1e-9
RANKS = ["Novice", "Apprentice", "Adept", "Expert", "Master"]


# ----------------------------------------------------------------------------- loading
def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_catalog(data_dir=DATA):
    """Return (effects_by_id, {path: [effects]})."""
    by_id, files = {}, {}
    for path in sorted(glob.glob(os.path.join(data_dir, "effects", "*.json"))):
        arr = load_json(path)
        files[path] = arr
        for e in arr:
            by_id[e["id"]] = e
    return by_id, files


# ----------------------------------------------------------------------------- Classic
def _mag_dur(eff, mmin, mmax, dur):
    """Apply 'counts as 1 when the effect has none'."""
    if not eff["magnitude"]["has"]:
        mmin = mmax = 1
    if not eff["duration"]:
        dur = 1
    return mmin, mmax, max(1, dur)


def classic_effect_cost(eff, mmin, mmax, dur, area):
    """C_i without the Target multiplier (Morrowind spellmaking formula)."""
    mmin, mmax, dur = _mag_dur(eff, mmin, mmax, dur)
    b = eff["morrowind"]["baseCost"]
    return b * ((mmin + mmax) * (dur + 1) + max(1, area)) / 40.0


def classic_spell(effects, catalog):
    """effects: list of dicts {id, range, min, max, duration, area}. Returns (cost, price, y)."""
    y = 0.0
    for se in effects:
        eff = catalog[se["id"]]
        c = classic_effect_cost(eff, se["min"], se["max"], se.get("duration", 1), se.get("area", 0))
        y = (y + max(1.0, c)) * (1.5 if se["range"] == "target" else 1.0)
    return int(math.floor(y + EPS)), max(1, int(math.floor(7 * y + EPS))), y


# ----------------------------------------------------------------------------- Skyrim-balanced
def skyrim_T(ticking, mmin, mmax, dur):
    mbar = max(1.0, (mmin + mmax) / 2.0)
    if ticking:
        return mbar * max(1.0, float(dur))
    return mbar * max(float(dur), 10.0) / 10.0


def skyrim_effect_cost(eff, mmin, mmax, dur, area, rng, base=None):
    sk = eff["skyrim"]
    b = sk["baseCost"] if base is None else base
    if not eff["magnitude"]["has"]:
        mmin = mmax = 0
    if not eff["duration"]:
        dur = 0
    t = skyrim_T(eff["magnitude"].get("ticking", False), mmin, mmax, dur)
    c = b * t ** 1.1 * (1.0 + area / 16.0)
    if rng == "target" and not sk.get("vanillaTargetPriced", False):
        c *= 1.5
    return c


def skyrim_spell(effects, catalog):
    total = 0.0
    for se in effects:
        eff = catalog[se["id"]]
        total += skyrim_effect_cost(eff, se["min"], se["max"], se.get("duration", 0), se.get("area", 0), se["range"])
    return int(math.floor(total + EPS)), max(1, int(math.floor(3 * total + EPS))), total


def effect_rank(eff, se, cost_share, thresholds):
    """Rank index 0..4 of one effect: magnitude ladder if present, else cost thresholds."""
    ladder = eff.get("rankLadder")
    if ladder:
        mag = se["max"] if eff["magnitude"]["has"] else 1
        r = 0
        for i, name in enumerate(RANKS):
            if name in ladder and mag >= ladder[name]:
                r = max(r, i)
        return r
    for i, t in enumerate(thresholds):
        if cost_share < t:
            return i
    return 4


def spell_rank(effects, catalog, thresholds=(60, 130, 250, 500)):
    r = 0
    for se in effects:
        eff = catalog[se["id"]]
        c = skyrim_effect_cost(eff, se["min"], se["max"], se.get("duration", 0), se.get("area", 0), se["range"])
        r = max(r, effect_rank(eff, se, c, thresholds))
    return r


# ----------------------------------------------------------------------------- fitting
def _fit_T(eff, fit):
    mag = fit.get("magnitude", 0)
    mmin, mmax = (mag if isinstance(mag, list) else [mag, mag])
    if not eff["magnitude"]["has"]:
        mmin = mmax = 0
    dur = fit.get("duration", 0) if eff["duration"] else 0
    return skyrim_T(eff["magnitude"].get("ticking", False), mmin, mmax, dur), mmin, mmax, dur


def classic_self_of_fit(eff, fit):
    mag = fit.get("magnitude", 0)
    mmin, mmax = (mag if isinstance(mag, list) else [mag, mag])
    return classic_effect_cost(eff, mmin, mmax, fit.get("duration", 0), fit.get("area", 0))


def vanilla_B(eff, fit):
    t, *_ = _fit_T(eff, fit)
    return fit["cost"] / (t ** 1.1 * (1.0 + fit.get("area", 0) / 16.0))


def scale_K(catalog):
    """Median of vanilla cost / Classic self cost over every vanilla fit of a Morrowind effect."""
    ratios = []
    for e in catalog.values():
        fit = e["skyrim"].get("fitFrom")
        if e["set"] in ("mw", "mwx") and fit and fit.get("kind", "vanilla") == "vanilla":
            ratios.append(fit["cost"] / classic_self_of_fit(e, fit))
    return statistics.median(ratios), ratios


def morrowind_B(eff, fit, K):
    t, *_ = _fit_T(eff, fit)
    target_cost = K * classic_self_of_fit(eff, fit)
    return target_cost / (t ** 1.1 * (1.0 + fit.get("area", 0) / 16.0)), target_cost


def fit_all(catalog):
    """Return {id: (B, detail)} for every effect with fitFrom; morrowind fits use the median K."""
    K, _ = scale_K(catalog)
    out = {}
    for eid, e in catalog.items():
        fit = e["skyrim"].get("fitFrom")
        if not fit:
            continue
        if fit.get("kind", "vanilla") == "vanilla":
            out[eid] = (vanilla_B(e, fit), fit)
        else:
            b, tc = morrowind_B(e, fit, K)
            out[eid] = (b, dict(fit, targetCost=tc))
    return K, out


# ----------------------------------------------------------------------------- check numbers
SKYRIM_CHECKS = [
    ("Firebolt: Fire Damage 25 pts on Target", [dict(id="mw.fire_damage", range="target", min=25, max=25, duration=0, area=0)], 41),
    ("Fireball: Fire Damage 40 pts in 15 ft on Target", [dict(id="mw.fire_damage", range="target", min=40, max=40, duration=0, area=15)], 133),
    ("Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target", [dict(id="mw.fire_damage", range="target", min=5, max=10, duration=3, area=10)], 59),
    ("Fire Damage 10 pts for 10 secs on Target", [dict(id="mw.fire_damage", range="target", min=10, max=10, duration=10, area=0)], 188),
]

FD, FR, FH, LV, AH = "mw.fire_damage", "mw.frost_damage", "mw.fortify_health", "mw.levitate", "mw.absorb_health"
CLASSIC_GOLDEN = [
    ("Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target", [dict(id=FD, range="target", min=5, max=10, duration=3, area=10)], 13, 91),
    ("Fire Damage 10 pts on Touch, then Frost Damage 10 pts on Target",
     [dict(id=FD, range="touch", min=10, max=10, duration=1, area=0), dict(id=FR, range="target", min=10, max=10, duration=1, area=0)], 15, 107),
    ("The same two effects in reverse order",
     [dict(id=FR, range="target", min=10, max=10, duration=1, area=0), dict(id=FD, range="touch", min=10, max=10, duration=1, area=0)], 12, 89),
    ("Fire Damage 10 to 20 pts on Target, then Fortify Health 20 pts for 30 secs on Self",
     [dict(id=FD, range="target", min=10, max=20, duration=1, area=0), dict(id=FH, range="self", min=20, max=20, duration=30, area=0)], 42, 297),
    ("The same two effects, Fortify Health first",
     [dict(id=FH, range="self", min=20, max=20, duration=30, area=0), dict(id=FD, range="target", min=10, max=20, duration=1, area=0)], 57, 405),
    ("Levitate 10 pts for 30 secs on Self", [dict(id=LV, range="self", min=10, max=10, duration=30, area=0)], 46, 326),
    ("Absorb Health 10 to 20 pts for 5 secs on Touch", [dict(id=AH, range="touch", min=10, max=20, duration=5, area=0)], 36, 253),
    ("Mark", [dict(id="mw.mark", range="self", min=1, max=1, duration=1, area=0)], 43, 306),
    ("Recall", [dict(id="mw.recall", range="self", min=1, max=1, duration=1, area=0)], 43, 306),
    ("Divine Intervention", [dict(id="mw.divine_intervention", range="self", min=1, max=1, duration=1, area=0)], 18, 131),
]


def run_checks(catalog):
    """Return list of (label, expected, got, ok)."""
    rows = []
    for label, effs, want in SKYRIM_CHECKS:
        got, _, _ = skyrim_spell(effs, catalog)
        rows.append(("Skyrim-balanced: " + label, want, got, got == want))
    for label, effs, want_cost, want_price in CLASSIC_GOLDEN:
        cost, price, _ = classic_spell(effs, catalog)
        rows.append(("Classic: " + label, (want_cost, want_price), (cost, price), (cost, price) == (want_cost, want_price)))
    return rows


# ----------------------------------------------------------------------------- CLI
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DATA)
    ap.add_argument("--write", action="store_true", help="write fitted baseCost values into the effect files")
    args = ap.parse_args(argv)

    catalog, files = load_catalog(args.data)
    K, fits = fit_all(catalog)
    _, ratios = scale_K(catalog)
    print(f"Scale K (median vanilla/Classic-self over {len(ratios)} vanilla fits) = {K:.6f}")
    print(f"{'effect':40} {'kind':9} {'B_mw':>8} {'B_sk':>12} {'stored':>12}  fit")
    changed = 0
    for eid in sorted(fits):
        b, fit = fits[eid]
        e = catalog[eid]
        stored = e["skyrim"].get("baseCost")
        kind = fit.get("kind", "vanilla")
        mw = e.get("morrowind") or {}
        desc = f"{fit.get('label')} M={fit.get('magnitude')} D={fit.get('duration')} A={fit.get('area')}"
        desc += f" cost={fit['cost']}" if kind == "vanilla" else f" -> {fit['targetCost']:.2f}"
        print(f"{eid:40} {kind:9} {mw.get('baseCost', 0):8g} {b:12.6f} {stored if stored is not None else float('nan'):12.6f}  {desc}")
        if stored is None or abs(stored - b) > 1e-12 * max(1.0, abs(b)):
            changed += 1
            e["skyrim"]["baseCost"] = b
    print()
    for label, want, got, ok in run_checks(catalog):
        print(f"[{'PASS' if ok else 'FAIL'}] {label}: expected {want}, got {got}")
    if args.write:
        for path, arr in files.items():
            with open(path, "w", encoding="utf-8") as f:
                json.dump(arr, f, indent=2, ensure_ascii=False)
                f.write("\n")
        print(f"wrote {len(files)} effect files ({changed} baseCost values changed)")
    elif changed:
        print(f"{changed} stored baseCost values differ from the fit (run with --write)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
