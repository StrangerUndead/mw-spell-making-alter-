#!/usr/bin/env node
/*
 * Offline harness driver: plays the built SWF in Ruffle (headless Chromium via Playwright),
 * with mock_dll.js standing in for the SKSE plugin. It
 *   1. drives the menu with keyboard / mouse / "gamepad" (numpad) input through a scripted
 *      scenario and asserts the exact GameDelegate intents the SWF sends (CONTRACTS.md 7),
 *   2. screenshots the main view, editor, picker, load list, cost math and message box,
 *   3. screenshots the layout at 1280x720, 1920x1080, 2560x1440, 3840x2160, 2560x1080 and
 *      3440x1440 (21:9),
 *   4. times LA_SetKnown + LA_SetState with 200 known effects (indicative only: Ruffle, not GFx).
 * Output: interface/test/out/*.png and report.json. Exit status 1 on any failed check.
 *
 * Usage: node run.mjs [--serve] [--only=scenario|layout|perf]
 * Needs: Playwright + Chromium, and @ruffle-rs/ruffle in interface/.tools/node_modules
 *        (build.sh --harness installs it).
 */
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..", "..");            // interface/
const outDir = path.join(root, "test", "out");
const args = process.argv.slice(2);
const only = (args.find((a) => a.startsWith("--only=")) || "").slice(7);
const PORT = Number(process.env.LA_HARNESS_PORT || 8765);

function loadPlaywright() {
  const candidates = [
    path.join(root, ".tools", "node_modules"),
    process.env.NODE_PATH || "",
    "/opt/node22/lib/node_modules",
    "/usr/lib/node_modules",
    "/usr/local/lib/node_modules",
  ].filter(Boolean);
  for (const c of candidates) {
    try { return createRequire(path.join(c, "noop.js"))("playwright"); } catch (e) { /* next */ }
  }
  throw new Error("playwright not found (npm i -g playwright && npx playwright install chromium)");
}

const MIME = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".wasm": "application/wasm",
  ".swf": "application/x-shockwave-flash", ".txt": "text/plain; charset=utf-8", ".map": "application/json" };

function serve() {
  return new Promise((resolve) => {
    const srv = http.createServer((req, res) => {
      let p = decodeURIComponent(req.url.split("?")[0]);
      if (p.endsWith("/")) p += "index.html";
      const f = path.join(root, p);
      if (!f.startsWith(root) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end("404"); }
      res.writeHead(200, { "Content-Type": MIME[path.extname(f)] || "application/octet-stream", "Cache-Control": "no-store" });
      fs.createReadStream(f).pipe(res);
    });
    srv.listen(PORT, "127.0.0.1", () => resolve(srv));
  });
}

const checks = [];
function check(name, ok, detail) {
  checks.push({ name, ok: !!ok, detail: detail === undefined ? "" : detail });
  console.log(`${ok ? "PASS" : "FAIL"} ${name}${detail !== undefined ? " - " + (typeof detail === "string" ? detail : JSON.stringify(detail)) : ""}`);
}

async function openPage(browser, w, h, query = "") {
  const page = await browser.newPage({ locale: "en-US", viewport: { width: w, height: h } });
  const errors = [];
  page.on("pageerror", (e) => errors.push(String(e.message)));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  await page.goto(`http://127.0.0.1:${PORT}/test/harness/index.html${query}`);
  await page.waitForFunction(() => window.LA_HARNESS_READY === true, null, { timeout: 30000 });
  await page.waitForFunction(() => window.LA_LAST_STATE !== undefined, null, { timeout: 10000 });
  await page.waitForTimeout(400);
  // focus the player so keyboard events reach the movie
  await page.mouse.click(w - 5, 5);
  await page.waitForTimeout(100);
  return { page, errors };
}

const calls = (page) => page.evaluate(() => window.LA_LOG.filter((c) => c[0] !== "LA_PlaySound"));
const sounds = (page) => page.evaluate(() => window.LA_LOG.filter((c) => c[0] === "LA_PlaySound").map((c) => c[1]));
const clearLog = (page) => page.evaluate(() => { window.LA_LOG.length = 0; });
const state = (page) => page.evaluate(() => window.LA_LAST_STATE);
const shot = (page, name) => page.screenshot({ path: path.join(outDir, name + ".png") });

async function press(page, key, times = 1, delay = 60) {
  for (let i = 0; i < times; i++) { await page.keyboard.press(key); await page.waitForTimeout(delay); }
}
async function settle(page, ms = 250) { await page.waitForTimeout(ms); }
function hasCall(log, name, ...args) {
  return log.some((c) => c[0] === name && args.every((a, i) => JSON.stringify(c[i + 1]) === JSON.stringify(a)));
}

async function scenario(browser) {
  const { page, errors } = await openPage(browser, 1920, 1080);
  let log = await calls(page);
  check("LA_Ready sent on load", hasCall(log, "LA_Ready"));
  await shot(page, "01_main_1080p");

  // --- list navigation + focus sound
  await clearLog(page);
  await press(page, "ArrowDown", 3);
  await settle(page);
  check("Down arrows play UIMenuFocus", (await sounds(page)).includes("UIMenuFocus"));

  // --- search with "/" (case-insensitive), Enter keeps the filter
  await press(page, "/");
  await settle(page, 150);
  await page.keyboard.type("FiRe", { delay: 40 });
  await settle(page);
  await shot(page, "02_search_fire");
  await press(page, "Enter");
  await settle(page);

  // --- add Fire Damage (first match) with E -> editor opens
  await clearLog(page);
  await press(page, "e");
  await settle(page, 400);
  log = await calls(page);
  check("E adds the focused effect", hasCall(log, "LA_AddEffect", "mw.fire_damage"), log);
  let st = await state(page);
  check("editor open for new effect", st.editor && st.editor.index === -1 && st.editor.id === "mw.fire_damage");
  await shot(page, "03_editor_new");

  // --- editor: Right = +1 on min, Shift+Right = big step, Down to max, F cycles range, Enter = OK
  await clearLog(page);
  await press(page, "ArrowRight", 4);
  await page.keyboard.down("Shift"); await press(page, "ArrowRight"); await page.keyboard.up("Shift");
  await press(page, "ArrowDown");
  await press(page, "ArrowRight", 2);
  await press(page, "f");
  await settle(page, 300);
  log = await calls(page);
  check("Right -> LA_EditorStep(min,+1,false)", hasCall(log, "LA_EditorStep", "min", 1, false), log.slice(0, 3));
  check("Shift+Right -> LA_EditorStep(min,+1,true)", hasCall(log, "LA_EditorStep", "min", 1, true));
  check("Down then Right -> LA_EditorStep(max,+1,false)", hasCall(log, "LA_EditorStep", "max", 1, false));
  check("F -> LA_EditorRange", hasCall(log, "LA_EditorRange"));
  st = await state(page);
  check("editor shows DLL values (min 15, max 17)", st.editor.min === 15 && st.editor.max === 17, [st.editor.min, st.editor.max]);
  await shot(page, "04_editor_values");
  await clearLog(page);
  await press(page, "Enter");
  await settle(page, 300);
  log = await calls(page);
  check("Enter in editor -> LA_EditorOk", hasCall(log, "LA_EditorOk"));
  st = await state(page);
  check("effect row added", st.effects.length === 1);
  check("add plays the add sound", (await sounds(page)).includes("UIEnchantingLearnEffect"));

  // --- clear search with "/" then Escape; tab switching with D
  await press(page, "/");
  await settle(page, 100);
  await press(page, "Escape");
  await settle(page, 150);
  await clearLog(page);
  await press(page, "d", 5);   // All -> ... -> Restoration
  await settle(page);
  check("D switches school tabs (UIMenuPrevNext)", (await sounds(page)).filter((s) => s === "UIMenuPrevNext").length >= 5);
  await shot(page, "05_tab_restoration");

  // --- attribute picker: Fortify Attribute (Restoration tab)
  await press(page, "/");
  await settle(page, 100);
  await page.keyboard.type("fortify attr", { delay: 30 });
  await press(page, "Enter");
  await settle(page, 150);
  await clearLog(page);
  await press(page, "Enter");
  await settle(page, 400);
  log = await calls(page);
  check("Enter adds Fortify Attribute", hasCall(log, "LA_AddEffect", "mw.fortify_attribute"), log);
  st = await state(page);
  check("picker open with 8 attributes", st.picker && st.picker.options.length === 8);
  await shot(page, "06_picker");
  await clearLog(page);
  await press(page, "ArrowDown");
  await press(page, "Enter");
  await settle(page, 400);
  log = await calls(page);
  check("picker Enter -> LA_PickTarget(1)", hasCall(log, "LA_PickTarget", 1), log);
  st = await state(page);
  check("editor title after pick", st.editor && /Intelligence/.test(st.editor.title), st.editor && st.editor.title);
  await press(page, "Enter");
  await settle(page, 400);

  // --- Spell Effects pane: Right switches pane, Shift+Up reorders, Delete removes
  await press(page, "/");
  await settle(page, 100);
  await press(page, "Escape");
  await press(page, "a", 5);   // back to All
  await clearLog(page);
  await press(page, "ArrowRight");
  await press(page, "End");
  await page.keyboard.down("Shift"); await press(page, "ArrowUp"); await page.keyboard.up("Shift");
  await settle(page, 300);
  log = await calls(page);
  check("Shift+Up -> LA_MoveEffect(1,-1)", hasCall(log, "LA_MoveEffect", 1, -1), log);
  st = await state(page);
  check("moved row now first", st.effects[0].id === "mw.fortify_attribute");
  await shot(page, "07_effects_pane");

  // --- edit existing row with Enter, Delete in editor
  await clearLog(page);
  await press(page, "Enter");
  await settle(page, 300);
  log = await calls(page);
  check("Enter on a spell effect -> LA_EditEffect(0)", hasCall(log, "LA_EditEffect", 0), log);
  st = await state(page);
  check("editor in edit mode shows Delete", st.editor && st.editor.index === 0);
  await shot(page, "08_editor_edit");
  await press(page, "Escape");
  await settle(page, 300);
  log = await calls(page);
  check("Esc in editor -> LA_EditorCancel", hasCall(log, "LA_EditorCancel"));

  // --- F1 cost math
  await clearLog(page);
  await press(page, "F1");
  await settle(page, 300);
  log = await calls(page);
  check("F1 -> LA_ToggleCostMath", hasCall(log, "LA_ToggleCostMath"));
  await shot(page, "09_cost_math");
  await press(page, "F1");

  // --- name: T, type, Enter creates (mock gold too low -> message)
  await page.evaluate(() => { window.LA_LOG.length = 0; });
  await press(page, "t");
  await settle(page, 150);
  await page.keyboard.type("Stormcrow", { delay: 30 });
  await settle(page, 150);
  await shot(page, "10_name_typing");
  await press(page, "Enter");
  await settle(page, 400);
  log = await calls(page);
  const names = log.filter((c) => c[0] === "LA_SetName").map((c) => c[1]);
  check("typing sends LA_SetName (no swallowed 't' hotkey char)", names.includes("Stormcrow"), names.slice(-3));
  check("Enter in name field -> LA_Create", hasCall(log, "LA_Create"));
  st = await state(page);
  await shot(page, "11_after_create");

  // --- Delete on a spell-effect row; Create button by mouse; message box
  await press(page, "ArrowRight");
  await clearLog(page);
  await press(page, "Delete");
  await settle(page, 300);
  log = await calls(page);
  check("Delete -> LA_RemoveEffect", log.some((c) => c[0] === "LA_RemoveEffect"), log);
  await press(page, "c");     // Clear
  await settle(page, 300);
  await clearLog(page);
  await press(page, "r");     // Create with no effects -> message
  await settle(page, 400);
  log = await calls(page);
  check("R -> LA_Create", hasCall(log, "LA_Create"));
  check("message box plays UIMenuCancel", (await sounds(page)).includes("UIMenuCancel"));
  await shot(page, "12_message");
  await press(page, "Enter");
  await settle(page, 200);

  // --- load list
  await clearLog(page);
  await press(page, "l");
  await settle(page, 400);
  log = await calls(page);
  check("L -> LA_LoadList", hasCall(log, "LA_LoadList"));
  await shot(page, "13_load_list");
  await press(page, "ArrowDown");
  await press(page, "Enter");
  await settle(page, 400);
  log = await calls(page);
  check("load pick -> LA_Load(3)", hasCall(log, "LA_Load", 3), log);
  st = await state(page);
  check("loaded spell rendered", st.effects.length === 2 && st.name === "Stormcrow's Kiss");
  await shot(page, "14_loaded");

  // --- mouse: click the first known row adds it; wheel scrolls
  await clearLog(page);
  const box = await page.evaluate(() => {
    const r = document.querySelector("ruffle-player").getBoundingClientRect();
    return { w: r.width, h: r.height };
  });
  await page.mouse.move(box.w * 0.2, box.h * 0.5);
  await page.mouse.wheel(0, 300);
  await settle(page, 200);
  await page.mouse.click(box.w * 0.2, box.h * 0.5);
  await settle(page, 400);
  log = await calls(page);
  check("mouse click on a known row -> LA_AddEffect", log.some((c) => c[0] === "LA_AddEffect"), log);
  await press(page, "Escape");
  await settle(page, 300);

  // --- "gamepad" through the numpad (GFx pad codes 96..107): Y rename -> keyboard request
  await clearLog(page);
  await press(page, "Numpad3");   // Y
  await settle(page, 400);
  log = await calls(page);
  check("pad Y -> LA_RequestKeyboard(name)", hasCall(log, "LA_RequestKeyboard", "name", "Stormcrow's Kiss", 40), log);
  st = await state(page);
  await settle(page, 300);
  log = await calls(page);
  check("keyboard result -> LA_SetName", hasCall(log, "LA_SetName", "Pad Named Spell"), log);
  await shot(page, "15_gamepad_glyphs");
  await clearLog(page);
  await press(page, "Numpad4");   // LB on Effects Known -> previous tab
  await press(page, "Numpad2");   // X -> create
  await settle(page, 300);
  log = await calls(page);
  check("pad X -> LA_Create", hasCall(log, "LA_Create"));
  await press(page, "Numpad0");   // A dismisses the message
  await settle(page, 200);
  await clearLog(page);
  await press(page, "Numpad1");   // B -> exit
  await settle(page, 500);
  log = await calls(page);
  check("pad B -> LA_Exit", hasCall(log, "LA_Exit"));
  const alpha = await page.evaluate(() => 0);
  await shot(page, "16_closed");

  check("no page errors", errors.length === 0, errors.slice(0, 5));
  await page.close();
}

async function layout(browser) {
  const sizes = [[1280, 720, "720p"], [1920, 1080, "1080p"], [2560, 1440, "1440p"], [3840, 2160, "4k"],
    [2560, 1080, "21x9_1080"], [3440, 1440, "21x9_1440"]];
  for (const [w, h, tag] of sizes) {
    const { page, errors } = await openPage(browser, w, h);
    await page.evaluate(() => window.LA_Load(0, 3));
    await settle(page, 400);
    await page.evaluate(() => { window.LA_EditEffect(0, 1); });
    await settle(page, 400);
    await shot(page, `layout_${tag}_editor`);
    await page.evaluate(() => { window.LA_EditorCancel(0); });
    await settle(page, 300);
    await shot(page, `layout_${tag}`);
    check(`layout ${tag} renders without errors`, errors.length === 0, errors.slice(0, 3));
    await page.close();
  }
}

async function perf(browser) {
  const { page } = await openPage(browser, 1920, 1080);
  const ms = await page.evaluate(() => {
    const base = window.LA_DATA.known;
    const big = [];
    for (let i = 0; i < 200; i++) {
      const k = Object.assign({}, base[i % base.length]);
      k.id = k.id + "_" + i;
      k.text = k.text + " " + String(i).padStart(3, "0");
      big.push(k);
    }
    big.sort((a, b) => (a.text < b.text ? -1 : 1));
    const p = window.LA_PLAYER;
    const t0 = performance.now();
    p.LA_Invoke("LA_SetKnown", window.LA_CLEAN(big));
    p.LA_Invoke("LA_SetState", window.LA_CLEAN(window.LA_LAST_STATE));
    return performance.now() - t0;
  });
  await settle(page, 300);
  await shot(page, "perf_200_known");
  check("200 known effects: LA_SetKnown+LA_SetState (Ruffle, indicative)", true, `${ms.toFixed(1)} ms`);
  await page.close();
  return ms;
}

(async () => {
  fs.mkdirSync(outDir, { recursive: true });
  const srv = await serve();
  if (args.includes("--serve")) {
    console.log(`Serving ${root} at http://127.0.0.1:${PORT}/test/harness/  (Ctrl+C to stop)`);
    return;
  }
  const { chromium } = loadPlaywright();
  const browser = await chromium.launch({ args: ["--enable-unsafe-swiftshader"] });
  let perfMs;
  try {
    if (!only || only === "scenario") await scenario(browser);
    if (!only || only === "layout") await layout(browser);
    if (!only || only === "perf") perfMs = await perf(browser);
  } catch (e) {
    check("harness ran to completion", false, String(e && e.stack || e));
  } finally {
    await browser.close();
    srv.close();
  }
  const failed = checks.filter((c) => !c.ok);
  fs.writeFileSync(path.join(outDir, "report.json"), JSON.stringify({ checks, perfMs, failed: failed.length }, null, 2));
  console.log(`\n${checks.length - failed.length}/${checks.length} checks passed; screenshots in ${outDir}`);
  process.exit(failed.length ? 1 : 0);
})();
