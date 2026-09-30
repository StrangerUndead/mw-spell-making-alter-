/*
 * Mock DLL for the offline harness (browser side).
 *
 * A TOY stand-in for the SKSE plugin's MenuSession: just enough behaviour to drive every UI
 * path of LostArt_Spellmaking.swf (picker, editor, reorder, messages, load list, cost math).
 * Its numbers are NOT the mod's rules - the real limits, costs and messages live in the DLL
 * (skse/core). Protocol shapes follow docs/dev/CONTRACTS.md section 7.
 *
 * The SWF calls window.<LA_Name>(responseId, ...args) through ExternalInterface (that is what
 * gfx.io.GameDelegate.call does); we answer by calling player.LA_Invoke(fn, arg), the harness
 * stand-in for the DLL's GFxMovieView::Invoke("_root.Menu_mc.<fn>").
 */
(function () {
  "use strict";
  const log = (window.LA_LOG = []);
  let D, byId, S, backup;
  const cfg = { closeAfterCreate: false, chanceModule: true };

  const player = () => window.LA_PLAYER;
  // Ruffle 0.6 hands JS "" to ActionScript as the string "null"; the real DLL (GFx values)
  // has no such problem. Drop empty strings so the SWF sees undefined (it treats both as "").
  const clean = (v) => {
    if (v === "") return undefined;
    if (Array.isArray(v)) return v.map(clean);
    if (v && typeof v === "object") {
      const o = {};
      for (const k of Object.keys(v)) { const c = clean(v[k]); if (c !== undefined) o[k] = c; }
      return o;
    }
    return v;
  };
  window.LA_CLEAN = clean;
  const invoke = (fn, a, b, c) => setTimeout(() => player().LA_Invoke(fn, clean(a), clean(b), clean(c)), 0);

  function lineText(fx) {
    const k = byId[fx.id];
    let s = fx.title;
    if (k._mock.mag) s += " " + fx.min + (fx.max !== fx.min ? " to " + fx.max : "") + (k.unit ? " " + k.unit : "");
    if (k._mock.dur) s += " for " + fx.duration + (fx.duration === 1 ? " sec" : " secs");
    if (fx.area > 0) s += " in " + fx.area + " ft";
    return s + " on " + fx.range;
  }

  // Morrowind-flavoured toy cost (Classic): ((min+max)*(dur+1)+area)*base/40, x1.5 on Target.
  function effectCost(fx) {
    const k = byId[fx.id];
    const mn = k._mock.mag ? fx.min : 1, mx = k._mock.mag ? fx.max : 1;
    const du = k._mock.dur ? fx.duration : 1;
    let c = ((mn + mx) * (du + 1) + fx.area) * k.baseCost / 40;
    if (fx.range === "Target") c *= 1.5;
    return Math.max(1, Math.round(c));
  }

  function titleFor(id, sub) {
    const k = byId[id];
    if (sub < 0 || sub === undefined) return k.text;
    const list = k.target === 1 ? D.attributes : D.skills;
    return k.text.replace("Attribute", "").replace("Skill", "").trim() + " " + list[sub];
  }

  function newEffect(id, sub) {
    const k = byId[id];
    const r = k._mock.ranges;
    const range = r.includes("Target") && r.length > 1 ? "Target" : r[0];
    return { id, sub, title: titleFor(id, sub), range, min: 1, max: 1, duration: k._mock.dur ? 1 : 0, area: 0 };
  }

  function editorState() {
    const e = S.editing;
    if (!e) return null;
    const k = byId[e.fx.id];
    return {
      index: e.index, id: e.fx.id, title: e.fx.title, school: k.school, icon: "",
      range: ["Self", "Touch", "Target"].indexOf(e.fx.range), rangeText: e.fx.range,
      canCycleRange: k._mock.ranges.length > 1,
      hasMagnitude: k._mock.mag, min: e.fx.min, max: e.fx.max, magCap: 100,
      hasDuration: k._mock.dur, duration: e.fx.duration, durCap: 1440,
      hasArea: k._mock.area && e.fx.range !== "Self", area: e.fx.area, areaCap: 50,
      unit: k.unit, lineText: lineText(e.fx), effectCost: effectCost(e.fx),
    };
  }

  function push() {
    let running = 0;
    const math = S.effects.map((fx) => {
      const c = effectCost(fx);
      running += c;
      return { text: lineText(fx), share: c, runningTotal: running };
    });
    const cost = running;
    const price = Math.max(1, cost * 3);
    const rank = cost < 25 ? 0 : cost < 50 ? 1 : cost < 100 ? 2 : cost < 150 ? 3 : 4;
    const full = S.effects.length >= 8;
    const dim = full ? D.known.map((k) => k.id) : S.effects.map((f) => f.id);
    const state = {
      mode: S.mode, providerName: S.providerName, name: S.name, maxEffects: 8, count: S.effects.length,
      effects: S.effects.map((fx, i) => ({ index: i, id: fx.id, text: lineText(fx), school: byId[fx.id].school, canEdit: true })),
      dim, cost, costText: String(cost), price, gold: S.gold, canAfford: price <= S.gold,
      chance: cfg.chanceModule ? Math.max(0, Math.min(100, 95 - cost)) : -1,
      rank, rankName: ["Novice", "Apprentice", "Adept", "Expert", "Master"][rank],
      modelName: "Classic", showCostMath: S.showCostMath, costMath: math,
      picker: S.picker, editor: editorState(),
      buttons: { createLabel: S.mode === "npc" ? "$LA_UI_Buy" : "$LA_UI_Create" },
    };
    window.LA_LAST_STATE = state;
    invoke("LA_SetState", state);
  }

  function msg(t) { invoke("LA_ShowMessage", t); }

  const H = {
    LA_Ready() { invoke("LA_SetKnown", D.known); push(); },
    LA_SetName(n) { S.name = String(n).slice(0, 40); },
    LA_AddEffect(id) {
      if (S.effects.length >= 8) return msg("You can only add eight effects to a spell.");
      if (S.effects.some((f) => f.id === id)) return msg("This effect has already been added.");
      const k = byId[id];
      if (k.target) {
        const list = k.target === 1 ? D.attributes : D.skills;
        S.picker = { effectId: id, title: k.text, options: list.map((t, i) => ({ sub: i, text: t })) };
      } else {
        S.editing = { index: -1, fx: newEffect(id, -1) };
      }
      push();
    },
    LA_PickTarget(sub) {
      const id = S.picker.effectId;
      S.picker = null;
      S.editing = { index: -1, fx: newEffect(id, sub) };
      push();
    },
    LA_EditEffect(i) { backup = JSON.stringify(S.effects[i]); S.editing = { index: i, fx: JSON.parse(backup) }; push(); },
    LA_RemoveEffect(i) { S.effects.splice(i, 1); push(); },
    LA_MoveEffect(i, d) {
      const j = i + d;
      if (j < 0 || j >= S.effects.length) return;
      const t = S.effects[i]; S.effects[i] = S.effects[j]; S.effects[j] = t; push();
    },
    LA_EditorRange() {
      const fx = S.editing.fx, r = byId[fx.id]._mock.ranges;
      fx.range = r[(r.indexOf(fx.range) + 1) % r.length];
      if (fx.range === "Self") fx.area = 0;
      push();
    },
    LA_EditorSet(field, v) { setField(field, v); push(); },
    LA_EditorStep(field, steps, big) { setField(field, S.editing.fx[field] + steps * (big ? 10 : 1)); push(); },
    LA_EditorOk() {
      const e = S.editing;
      if (e.index < 0) S.effects.push(e.fx); else S.effects[e.index] = e.fx;
      S.editing = null; push();
    },
    LA_EditorCancel() { S.editing = null; S.picker = null; push(); },
    LA_EditorDelete() { S.effects.splice(S.editing.index, 1); S.editing = null; push(); },
    LA_Clear() { S.effects = []; S.name = ""; S.editing = null; push(); },
    LA_LoadList() { invoke("LA_SetLoadList", D.loadList.map((l) => ({ slot: l.slot, name: l.name, text: l.text }))); },
    LA_Load(slot) {
      const l = D.loadList.find((x) => x.slot === slot);
      S.name = l.name;
      S.effects = l.effects.map(([id, sub, range, min, max, duration, area]) =>
        ({ id, sub, title: titleFor(id, sub), range, min, max, duration, area }));
      push();
    },
    LA_Create() {
      if (!S.effects.length) return msg("You have to add at least one effect to a spell.");
      if (!S.name) return msg("You have to name the spell before buying it.");
      const price = Math.max(1, S.effects.reduce((a, f) => a + effectCost(f), 0) * 3);
      if (price > S.gold) return msg("You don't have enough gold to buy this spell.");
      S.gold -= price;
      if (cfg.closeAfterCreate) return invoke("LA_Close");
      S.effects = []; S.name = ""; push();
    },
    LA_ToggleCostMath() { S.showCostMath = !S.showCostMath; push(); },
    LA_Exit() { invoke("LA_Close"); },
    LA_PlaySound() {},
    LA_RequestKeyboard(purpose, text) {
      // what the DLL would do after the on-screen keyboard returns
      setTimeout(() => player().LA_Invoke("LA_KeyboardResult", purpose, purpose === "name" ? "Pad Named Spell" : "shock", true), 50);
    },
  };

  function setField(field, v) {
    const fx = S.editing.fx;
    const caps = { min: [1, 100], max: [1, 100], duration: [1, 1440], area: [0, 50] }[field];
    fx[field] = Math.max(caps[0], Math.min(caps[1], Math.round(v)));
    if (field === "min" && fx.max < fx.min) fx.max = fx.min;
    if (field === "max" && fx.max < fx.min) fx.max = fx.min;
  }

  for (const name of Object.keys(H)) {
    window[name] = function (responseId, ...args) {
      log.push([name, ...args]);
      try { H[name](...args); } catch (e) { log.push(["MOCK_ERROR", name, String(e)]); }
    };
  }

  window.LA_MOCK_START = function (data, dict, options) {
    D = data;
    Object.assign(cfg, options || {});
    byId = {};
    D.known.forEach((k) => (byId[k.id] = k));
    S = Object.assign({ name: "", effects: [], editing: null, picker: null, showCostMath: false, gold: 1250 }, D.state);
    player().LA_DevStart(dict);
  };
  window.LA_MOCK_CFG = cfg;
})();
