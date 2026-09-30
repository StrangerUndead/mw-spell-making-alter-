# Spellmaking menu (Scaleform SWF)

Owner: `interface/**` and this file. The build spec is `docs/OUTLINE.md` → "Spellmaking menu".
The wire protocol is `docs/dev/CONTRACTS.md` §6–§7; this file describes how the movie implements
it and lists the few places where it needs something the contract does not yet say.

**Rule of the house:** the DLL (core `MenuSession`) owns every rule, limit, cost, price, chance and
message. The SWF renders the state object it is given and sends intents. It never checks whether
an action is legal: adding a ninth or duplicate effect, creating without a name, and so on are all
sent to the DLL, which answers with `LA_ShowMessage` and/or a new `LA_SetState`.

## Files

```
interface/
  build.sh                     headless build + verification (see "Building")
  .gitignore                   .tools/ build/ test/out/
  src/                         ActionScript 2 sources (MTASC -strict clean)
    LostArtMain.as             entry point (MTASC -main): builds _root.Menu_mc
    LostArtSpellmakingMenu.as  root clip class: DLL API, layout, state application, input routing
    skse.as                    intrinsic declarations of _global.skse (AllowTextInput, GetLastKeycode, ...)
    gfx/io/GameDelegate.as     clean-room implementation of the GameDelegate wire protocol
    gfx/ui/NavigationCode.as   navEquivalent string constants (same values as Skyrim's menus)
    lostart/Theme.as           sizes, colours (school colours), font names
    lostart/Sounds.as          UI sound -> vanilla SoundDescriptor EditorID table
    lostart/util/              Translator ($key -> text), Text (TextField factory), Draw (procedural art),
                               Layout (visibleRect/safeRect -> design frame), Tween
    lostart/input/             KeyMap (the whole binding table, pure data), InputDetails, InputRouter
    lostart/model/             EffectFilter (school tab + case-insensitive search), StateUtil (diff helpers)
    lostart/components/        VirtualList, TabBar, TextBox, Button, KeyGlyph (button art), Slider,
                               ItemCard, ReadoutBar, EffectEditor, ListPopup (picker + load list),
                               MessageBox, CostMathPanel
  translations/LostArt_UI_ENGLISH.txt   every $LA_ key the SWF uses (UTF-8 list; see "Strings")
  test/swfcheck.py             dependency-free SWF structural verifier
  test/harness/                offline harness: Ruffle + Playwright + mock DLL
    index.html, mock_dll.js, mock_data.json, run.mjs
  build/LostArt_Spellmaking.swf         build output (not committed)
```

The movie has **no library symbols, bitmaps or embedded fonts**. Everything is drawn at runtime
with the MovieClip drawing API and `createTextField`, using the fontconfig names
`$EverywhereFont`, `$EverywhereMediumFont`, `$EverywhereBoldFont` (Skyrim's
`Interface/fontconfig.txt` maps them per language, so localised glyphs come for free).
Button art (keycaps and Xbox-layout face buttons) is drawn by `KeyGlyph`.

### Relation to SkyUI

The structure follows SkyUI's AS2 menus: a virtualized list with recycled row clips
(`skyui.components.list.BasicList`/`ScrollingList` → `VirtualList`), category tabs
(`TabBar`), the search widget's text-input handling (`SearchWidget` → `TextBox`),
`skse.GetLastKeycode`/`GetLastControl` for device detection (`InputDelegate` → `InputRouter`),
hidden-TextField translation (`skyui.util.Translator` → `lostart.util.Translator`), and
`Stage.visibleRect`/`Stage.safeRect` layout (`GlobalFunc.Lock` → `Layout`).

**No SkyUI code is vendored.** The SkyUI repository (github.com/schlangster/skyui, checked at
commit `8354287`) has no licence file; its README carries only a warranty disclaimer, which is not
a grant. The CLIK classes in that repo (`gfx.*`, `Shared.*`) are decompiled Scaleform/Bethesda
code. So every class here is an independent implementation; `gfx.io.GameDelegate` shares only the
wire protocol (`ExternalInterface.call(name, responseId, args...)`, callbacks `call`/`respond`),
which the DLL's `RE::FxDelegate` registration depends on.

## Protocol as implemented

Root clip `_root.Menu_mc` is an instance of `LostArtSpellmakingMenu` (the empty clip's
`__proto__` is bound to the class, since there is no library symbol to `registerClass`).

### DLL → SWF (Invoke on `_root.Menu_mc`)

| Function | SWF behaviour |
| --- | --- |
| `LA_SetKnown(arr)` | Stores the list (kept in the DLL's order), rebuilds tab counts, re-filters, keeps the focused effect by `id`. |
| `LA_SetState(obj)` | Applies the whole snapshot (see below). Safe to call synchronously from inside any intent handler. |
| `LA_SetLoadList(arr)` | Opens the Load popup (`[{slot, name, text}]`, two-line rows). Picking sends `LA_Load(slot)`; cancelling just closes it. |
| `LA_ShowMessage(text)` | Queues a modal message box; plays the error sound. Enter/A/Esc/B/OK dismiss. |
| `LA_Close()` | Ends any text entry (releases `AllowTextInput`), fades the menu out in 150 ms. The DLL may close the menu immediately or after ~150 ms. |
| `LA_KeyboardResult(purpose, text, accepted)` | **Proposed** — see "Contract gaps". |

`LA_SetState` handling, field by field:

- `name` → name field, unless the player is typing in it.
- `dim` → Effects Known rows with those ids are drawn dimmed (still selectable and still sent
  with `LA_AddEffect`, so the DLL can answer with Morrowind's message).
- `effects` → Spell Effects rows (`text` is the finished Morrowind line). When the row count grows
  the add sound plays and the new row is selected; when it shrinks the remove sound plays; after
  `LA_MoveEffect` the selection follows the moved row.
- `count`/`maxEffects` → the `n/8` counter (highlighted when full).
- `cost`/`costText` → Magicka readout; the number counts up/down to `cost` over ≤ 400 ms, then
  shows `costText`. `rank`/`rankName` → badge; `chance` shown only when `>= 0`; `price` turns red
  when `canAfford == false`; `gold`; `modelName` → "(… cost model)".
- `showCostMath`/`costMath` → cost-math panel (replaces the item card; while the editor is open it
  sits over the lower part of Effects Known instead).
- `picker` → attribute/skill popup (`options[{sub,text}]`); choosing sends `LA_PickTarget(sub)`.
- `editor` → editor modal over the right column; `null` closes it.
- `buttons.createLabel` → Create/Buy label.
- Any string field starting with `$` is translated by the SWF; anything else is shown verbatim.
  So the DLL may send either keys (`"$LA_Rank_Adept"`) or finished text.

### SWF → DLL (`gfx.io.GameDelegate.call(name, args)`)

| Call | Sent when |
| --- | --- |
| `LA_Ready []` | once, after the movie has built itself |
| `LA_SetName [text]` | on every keystroke in the name field and when editing ends |
| `LA_AddEffect [id]` | Enter/E/A or click on an Effects Known row |
| `LA_PickTarget [sub]` | choice in the picker |
| `LA_EditEffect [index]` | Enter/E/A or click on a Spell Effects row |
| `LA_RemoveEffect [index]` | Delete on a Spell Effects row, or its ✕ mouse tool |
| `LA_MoveEffect [index, ±1]` | Shift+Up/Down, LB/RB in Spell Effects, or the ▲/▼ mouse tools |
| `LA_EditorRange []` | F / Y / click on the range box / Left-Right on the range row |
| `LA_EditorSet [field, value]` | slider drag or track click (at most once per frame while dragging, then on release) |
| `LA_EditorStep [field, ±1, big]` | Left/Right (`big=false`), Shift+Left/Right or LT/RT (`big=true`) on a slider row |
| `LA_EditorOk []` | Enter/E/A in the editor, or OK |
| `LA_EditorCancel []` | Tab/Esc/B or Cancel in the editor; **also** Cancel in the picker |
| `LA_EditorDelete []` | Delete/X in the editor, or Delete (only shown when `editor.index >= 0`) |
| `LA_Clear []` | C / Back(View) / Clear button |
| `LA_LoadList []` | L / LT / Load button |
| `LA_Load [slot]` | choice in the Load popup |
| `LA_Create []` | R / X / Create button, and Enter in the name field |
| `LA_ToggleCostMath []` | F1 / left-stick click |
| `LA_Exit []` | Tab/Esc/B or Exit in the main view |
| `LA_PlaySound [soundKey]` | UI feedback, see "Sounds" |
| `LA_RequestKeyboard [purpose, text, maxChars]` | **Proposed** — see "Contract gaps" |

Field names for the editor calls are `"min"`, `"max"`, `"duration"`, `"area"`.

### Contract gaps (proposed additions — need a CONTRACTS.md owner decision)

1. **Gamepad text entry.** The outline wants gamepad renaming to open the on-screen keyboard;
   an AS2 movie cannot open it. On gamepad (Y for rename, right-stick click for search) the SWF
   starts normal text entry (so a physical keyboard still works) and sends
   `LA_RequestKeyboard [purpose("name"|"search"), currentText, maxChars]`. The DLL should open
   Skyrim's virtual keyboard (`RE::BSVirtualKeyboardDevice` / the Steam gamepad text input, as the
   vanilla crafting rename does) and answer with Invoke
   `LA_KeyboardResult(purpose, text, accepted)`. The SWF then ends text entry and, for the name,
   sends `LA_SetName`. If the DLL ignores the call nothing breaks (unregistered FxDelegate calls
   are no-ops); the player can still press B to leave the field.
2. **Cancelling the picker.** No call exists for it; the SWF sends `LA_EditorCancel`, treating the
   picker as the first step of the pending add. The DLL must clear `state.picker` on it.
3. **Rank badge colour.** The outline wants the badge in the spell's school colour, but the state
   has no spell school. The SWF uses optional `state.school` (0–4) when present, otherwise the
   school of `effects[0]`. The DLL should add `school` (it already decides the spell's school).
4. **Editor school name.** `editor` has `school` but no `schoolName`; the SWF looks it up in the
   known list by `editor.id`, and uses `editor.schoolName` if the DLL adds it.

## Controls (as bound in `lostart/input/KeyMap.as`)

Keyboard matching uses the DirectInput scan code from `skse.GetLastKeycode` in game (and the Flash
key code offline), so it is layout-independent like SkyUI. Gamepad matching uses SKSE codes
266–281, or the GFx pad codes 96–107 Skyrim's menus receive.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Move in a list | Up/Down, wheel, PgUp/PgDn/Home/End | D-pad / left stick |
| Switch pane | Left/Right | D-pad left/right |
| School tab | A/D, click a tab | LB/RB (Effects Known) |
| Reorder effect | Shift+Up/Down, ▲/▼ on the selected row | LB/RB (Spell Effects) |
| Add / edit | Click, E, Enter | A |
| Remove | Delete, ✕ on the row; Delete in editor | X in editor |
| Cycle range (editor) | F, click Range, Left/Right on the Range row | Y |
| Slider (editor) | drag / click track; Left/Right = 1; Shift+Left/Right = big | D-pad = 1; LT/RT = big |
| Editor rows | Up/Down (last row = OK/Cancel/Delete, Left/Right between them) | D-pad |
| Rename | Click the name, T | Y (+ on-screen keyboard request) |
| Create / Buy | R, Create button, Enter in the name field | X |
| Cost math | F1 | Left stick click |
| Search | /, click the box | Right stick click |
| Load | L, Load button | LT |
| Clear | C, Clear button | Back (View) |
| Back / Exit | Tab, Esc | B |

Load/Clear bindings and list paging are additions: the outline's table leaves them open.
Mouse hover selects rows (and moves pane focus), as in SkyUI.
Auto-repeat (`keyHold`) is honoured only for navigation and slider steps.

## Text input

`TextBox` follows Skyrim's rename/search behaviour: on start the field becomes `type="input"`,
takes focus, and `skse.AllowTextInput(true)` stops typed letters acting as controls; on end
(Enter accepts, Esc/Tab/B cancels, clicking away keeps the text) it restores everything and calls
`AllowTextInput(false)`. Calls are kept balanced across both fields (SKSE keeps a counter), and
`LA_Close`/`onUnload` release it. The hotkey that opened a field ("t", "/") is removed if its
character lands in the field. The name is capped at 40 characters (`maxChars`); the DLL should
still truncate.

## Sounds

`LA_PlaySound [editorId]` with vanilla Skyrim.esm SoundDescriptor EditorIDs (checked against
Mutagen.Bethesda.FormKeys.SkyrimSE), ready for `RE::PlaySound(id)`:

| When | EditorID |
| --- | --- |
| selection moved (list, editor row, popup) | `UIMenuFocus` |
| editor / picker / load list / text entry opened, message dismissed | `UIMenuOK` |
| back out of editor, picker or load list; exit | `UIMenuCancel` |
| message box opened (Morrowind error) | `UIMenuCancel` |
| school tab, pane switch, range cycle, row removed | `UIMenuPrevNext` |
| effect row added (seen in `LA_SetState`) | `UIEnchantingLearnEffect` |
| slider step | `UIMenuFocus` (throttled, 45 ms per sound) |

Create's success feedback (`UIEnchantingItemCreate` / `UISpellLearned`, flash, altar flare) is
the DLL's, since only it knows the spell was made.

## Layout and scaling

`LostArtMain` sets `Stage.scaleMode = "noScale"`, `Stage.align = "TL"`, so the movie works the same
whatever scale mode the DLL loads it with. `Layout.frame()` reads `Stage.visibleRect` (GFx
extension; falls back to `Stage.width/height`) and scales the content clip so the visible height
is 1080 design units: 16:9 gives 1920 units of width, 21:9 gives 2520, 32:9 3840. Content is
capped at 1800 units wide and centred, so ultrawide screens keep the panes readable; margins
honour `Stage.safeRect` (TV safe area). Relayout happens on `Stage.onResize`.
The SWF header is 1280×720 @ 30 fps (only tools see it).

## Strings

All text is `$LA_` keys (CONTRACTS.md §6). The canonical file is
`data/translations/LostArt_ENGLISH.txt`; `interface/translations/LostArt_UI_ENGLISH.txt` lists
every key the SWF uses with the canonical English value. `build.sh` fails if the SWF uses a key
missing from that list, and warns about keys not yet merged into the canonical file. Currently
**four keys are new** and need merging into every language:
`$LA_UI_CostShare`, `$LA_UI_NoEffects`, `$LA_UI_NoKnown`, `$LA_UI_NoMatches`.
The only format placeholder used is `{0}` in `$LA_UI_CostModelFmt`, substituted by the SWF.

## Building

No Flash IDE is needed. From the repository root:

```sh
interface/build.sh            # compile + structural check + translation-key check
interface/build.sh --ffdec    # + JPEXS FFDec dump and full AS2 decompile
interface/build.sh --harness  # + offline Ruffle/Playwright harness
interface/build.sh --all      # everything
```

Tools are downloaded once into `interface/.tools/` and pinned by SHA-256:

| Tool | Version / source | Used for |
| --- | --- | --- |
| MTASC | 1.14 (`mtasc_1.14-3build5_amd64.deb`, Ubuntu archive) | compiling; `-strict` type checking of every class |
| JPEXS FFDec | 26.3.0 (GitHub release zip) | `-dumpSWF` and `-export script` (independent parser/decompiler) |
| Ruffle | `@ruffle-rs/ruffle` 0.6.0 (npm) | running the SWF offline |
| Playwright + Chromium | preinstalled (or `npm i -g playwright`) | driving Ruffle headlessly |

The compile step is:

```sh
cd interface/src
mtasc -version 8 -strict -wimp -cp $STD/std -cp $STD/std8 -cp . \
      -swf ../build/LostArt_Spellmaking.swf -header 1280:720:30:000000 \
      -main LostArtMain.as <every other .as file>
```

MTASC writes a new compressed SWF 8 (`CWS`) with one DefineSprite/ExportAssets/DoInitAction per
class (`__Packages.<class>`) and a frame-1 DoAction calling `LostArtMain.main(_root)`.
`test/swfcheck.py` then re-parses the file with no dependencies and checks the SWF version (8–10),
tag stream, all 28 classes, and that every contract function name and the font names are in the
bytecode's string pools.

## Offline testing

`interface/build.sh --harness` (or `node interface/test/harness/run.mjs`) serves `interface/`,
opens the SWF in Ruffle in headless Chromium with FlashVar `la_dev=1`, and lets
`test/harness/mock_dll.js` play the DLL. Dev mode only switches to a device font and registers
the `LA_Invoke`/`LA_DevStart` ExternalInterface bridge; in game there are no FlashVars.

- **Scenario** (42 checks): drives keyboard, mouse and "gamepad" input (synthetic numpad events
  carrying the GFx pad codes: 0=A 1=B 2=X 3=Y 4=LB 5=LT 6=LS 7=RB 8=RT 9=RS) and asserts the exact
  intents and arguments the SWF sends, plus sounds: search, tabs, add, picker, editor steps/range/
  OK/cancel/delete, reorder, remove, cost math (also inside the editor), rename + Enter-creates,
  message box, load list, mouse wheel/click, every gamepad binding, exit.
- **Layout** (6 checks): screenshots at 1280×720, 1920×1080, 2560×1440, 3840×2160, 2560×1080 and
  3440×1440, main view and editor.
- **Perf** (1 check): pushes 200 known effects and times `LA_SetKnown` + `LA_SetState`
  (30–45 ms in Ruffle/WASM under software GL; indicative only, GFx is native code).

Screenshots, `trace.log` (the SWF's `trace` output) and `report.json` land in
`interface/test/out/`. Harness-only quirks, none of which apply in game: Ruffle 0.6 turns a JS
`""` into the AS string `"null"`, so the mock drops empty strings before invoking; Playwright's
numpad keys arrive as navigation keys, so "pad" presses are synthetic events that also carry a
digit (typed if a text field has focus); headless Chromium renders frames only on demand and
Ruffle ticks on animation frames, so the mouse step forces frames (1×1 screenshots) around the
press and waits for the call instead of sleeping. Run one harness at a time (it owns port 8765;
set `LA_HARNESS_PORT` to change it). For manual poking, run
`node interface/test/harness/run.mjs --serve` and open `http://127.0.0.1:8765/test/harness/`
(`?npc=1` for the Buy label, `?close=1` to close after Create, `?log=info` for traces).
`mock_data.json` doubles as a sample of every payload shape (known list, load list, state).
The mock's numbers are a toy, not the mod's rules.

## Testing in game

What the DLL needs to do for this movie (for the SKSE plugin owner):

1. Register a menu `LostArt_SpellmakingMenu` that loads `Interface/LostArt_Spellmaking.swf`
   (any scale mode; background alpha 0), with flags: pauses game, uses cursor, uses menu context
   (so key events reach the movie), modal. Install an `RE::FxDelegate` and register every
   `LA_*` call in the table above (plus `LA_RequestKeyboard` if adopted).
2. Answer `LA_Ready` with `Invoke("_root.Menu_mc.LA_SetKnown", arr)` then
   `Invoke("_root.Menu_mc.LA_SetState", obj)`. Every later intent should end in a fresh
   `LA_SetState` (the SWF diffs it; the whole snapshot is cheap).
3. Play `LA_PlaySound` ids with `RE::PlaySound`.
4. On close: `Invoke LA_Close`, then hide the menu (≥ 150 ms later for the fade, or at once).

Then in game: open the console and run `la menu` (the DLL's debug command) to open the menu
without an altar. Check, at 720p, 1080p, 1440p, 4K and a 21:9 mode (`iSize W`/`iSize H` in
`SkyrimPrefs.ini`, windowed is fine):

- fonts render (fontconfig names), the layout fits inside the safe area, nothing overlaps;
- keyboard: every row of the Controls table; typing a name with letters that are also hotkeys
  (T, R, E, F, A, D, C, L) does not trigger them; Enter in the name creates;
- mouse: hover selects, wheel scrolls, clicks add/edit, ▲▼✕ tools, slider drag, tabs, buttons;
- gamepad: glyphs switch to Xbox buttons after a pad press; every pad binding; on-screen keyboard;
- messages: ninth effect, duplicate, no name, no effects, zero cost, not enough gold (price red);
- 200 known effects: open time under 150 ms (`la menu` with the Sandbox availability option).
