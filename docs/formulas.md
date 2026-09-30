# Formulas

Every number the spellmaker shows comes from `skse/core` (`lostart/CostEngine.h`,
`lostart/Mechanics.h`). The unit tests in `skse/tests/TestCost.cpp` pin each formula to the
worked examples below. B is an effect's base cost, M its magnitude, D its duration in seconds,
A its area in feet.

## Morrowind Classic cost

Per effect (magnitude and duration count as 1 when the effect has none):

    C_i = B × [(Mmin + Mmax) × (D + 1) + max(1, A)] / 40

The spell total runs in list order, and each Target effect multiplies the running total:

    y_i = (y_{i-1} + max(1, C_i)) × (1.5 if effect i is on Target, else 1)
    Cost = floor(y_n × global cost multiplier)

With *Target ×1.5 on the running total* off, each Target effect is multiplied on its own and order
stops mattering. The global multiplier (0.25–4) scales magicka only, not the price.

| Spell | Cost | Price |
| --- | --- | --- |
| Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target | 13 | 91 |
| Fire Damage 10 pts on Touch, then Frost Damage 10 pts on Target | 15 | 107 |
| The same two effects in reverse order | 12 | 89 |
| Fire Damage 10 to 20 pts on Target, then Fortify Health 20 pts for 30 secs on Self | 42 | 297 |
| The same two effects, Fortify Health first | 57 | 405 |
| Levitate 10 pts for 30 secs on Self | 46 | 326 |
| Absorb Health 10 to 20 pts for 5 secs on Touch | 36 | 253 |
| Mark or Recall | 43 | 306 |
| Divine Intervention | 18 | 131 |

## Skyrim-balanced cost (default)

    c_i = B_sk × T^1.1 × (1 + A / 16)   [× 1.5 on Target when vanilla has no aimed version]
    T   = M̄ × max(1, D)            for ticking effects (damage, restore, absorb, poison, disintegrate)
    T   = M̄ × max(D, 10) / 10      for held effects
    M̄   = max(1, (Mmin + Mmax) / 2)
    Cost = floor(Σ c_i × global cost multiplier)

B_sk comes from the catalog. At load the plugin refits it from the vanilla spell named in
`skyrim.fitFrom`, so an overhaul that rebalances vanilla costs carries into crafted spells. Order
never changes this cost, which is a property test.

With the fire base cost fitted to Firebolt (25 pts, instant, 41 magicka), a rebuilt Firebolt costs
41, a rebuilt Fireball (40 pts in 15 ft) 133, *Fire Damage 5 to 10 pts for 3 secs in 10 ft on
Target* 59, and *Fire Damage 10 pts for 10 secs on Target* 188.

After this, the usual Skyrim reductions apply to the cast: skill (× (1 − (skill/400)^0.65)),
half-cost perks (each crafted spell carries its school's rank perk), Fortify school enchantments,
and the 2.8× dual-cast cost.

## Price

    Price = max(1, floor(k × y))

y is the unrounded total. k is 7 in Classic (Morrowind's fSpellMakingValueMult) and 3 in
Skyrim-balanced; both are MCM settings (1–20). Then, in order:

1. **Altars.** Full, half (never below 1), free, or one filled soul gem. The Arch-Mage uses the
   College altar free.
2. **Spellmakers.** The per-NPC price modifier, when that setting is on.
3. **Skyrim haggling.** When on: Skyrim's buy-price formula plus the Modify Buy Prices perk entry
   point.
4. **Load pricing.** With *Difference only*, you pay `max(0, new price − price paid)` for the
   loaded spell.

## Casting chance (optional module)

Morrowind's formula, used for the parity test:

    Chance = (2S + W/5 + L/10 − Cost − Sound) × (0.75 + 0.5 × F / Fmax)

With Destruction 30, Willpower 40 and Luck 40, the first worked example (cost 13) shows 59% in the
menu, which previews at half fatigue, and 73% at full fatigue.

In Skyrim, stamina stands in for fatigue, and the MCM *casting bonus* (default 15) stands in for
W/5 + L/10:

    Chance = (2S + bonus − D_mw − Sound) × (0.75 + 0.5 × Stamina / StaminaMax)

D_mw is the spell's Classic cost, whichever magicka model is active. S is the skill of the hardest
effect's school: the effect with the lowest `2 × skill − x`, where
`x = B × [(Mmin + Mmax) × max(1, D) + A] / 40`, times 1.5 on Target.

## Rank

Each effect has a magnitude ladder taken from vanilla spells: Shield is 40 Novice, 60 Apprentice,
80 Adept, 100 Expert; Fire Damage is 25 Apprentice, 40 Adept, 60 Expert. Effects without a ladder
use their Skyrim-balanced cost: under 60 Novice, under 130 Apprentice, under 250 Adept, under 500
Expert, then Master. A spell's rank is the highest rank among its effects.

## Casting

| Rule | Value |
| --- | --- |
| Area radius | A × 22 game units around the impact point; the caster is excluded; line of sight required by default |
| Touch reach | 192 units by default (MCM: 128–320) |
| Magnitude roll | One-time effects roll a whole number in [min, max] once per target. Effects ticking for 2 s or more apply the average. A 1-second damage effect is one instant hit and rolls |
| Burden on NPCs | −M/2 % speed, capped at 75% |
| Slowfall | Fall speed × (1 − M/200); any active Slowfall cancels fall damage |
| Jump | +3% jump height per point (MCM 1–10%); fall height counts M ft shorter |
| Lock / Open | Skyrim lock tiers 1, 25, 50, 75, 100. Open works when the lock level is ≤ M. Lock raises the lock to the tier at or below M. Requires-key locks (255) are never touched |
| Elemental shields | Attacker takes 0.1 × M × (1 − x/100), with x = min(100, max(0, save − d100) + resistance) and save = (skill + bonus) × 1.25 × fatigue ratio |
| Miss chance | Blind(attacker) + min(Sanctuary, 75) + 0.2 × Chameleon + evasion − Fortify Attack, clamped to 0–100% |
| Charm | One relationship step per 25 M; prices improve by M/2 % |
| Disintegrate | The item's condition pool (0–100) loses M per second; armor rating and damage scale with condition; at 0 the item is unequipped |
