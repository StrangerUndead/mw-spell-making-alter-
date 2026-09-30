Scriptname LostArt Hidden
{Lost Art of Spellmaking - public Papyrus API.

Every function here is a global native implemented by SKSE/Plugins/LostArt.dll
(docs/dev/CONTRACTS.md section 8). The script holds no state, registers for no
events and runs no update loops: all behaviour lives in the DLL.

Calling any of these without the DLL loaded logs "Unbound native function" in
Papyrus.0.log and returns the type default (None / false / 0 / ""), so callers from
other mods should check GetVersion() != "" before relying on the API.

Mod events (SKSE ModEvent, register with RegisterForModEvent):
  LostArt_SpellCreated(Form akSpell)  - a custom spell was written into a slot
  LostArt_SpellDeleted(Form akSpell)  - a custom spell was deleted and its slot freed
  LostArt_SettingsChanged             - sent by LostArt_MCM when an MCM setting changes
                                        (strArg = the "key:Section" id that changed)}

; -----------------------------------------------------------------------------
; Services
; -----------------------------------------------------------------------------

Function OpenSpellmaking(ObjectReference akProvider) global native
{Opens the spellmaking menu. akProvider is the altar reference (altar mode) or the
spellmaker NPC (spellmaker mode: price paid to that NPC). The DLL checks access
itself (GetRefusalReason) and shows the refusal message instead of the menu when the
provider refuses. Safe to call from a dialogue fragment: the menu opens once the
Dialogue Menu has closed.}

bool Function IsCustomSpell(Spell akSpell) global native
{True when akSpell is one of the 500 primary spell slots (LA_Slot_000..499) and
currently holds a custom spell definition.}

int Function GetCustomSpellCount() global native
{Number of primary slots in use (custom spells that exist in this save), 0-500.}

int Function GetFreeSlotCount() global native
{Number of free primary slots, 0-500.}

int Function GetFreeSubSlotCount() global native
{Number of free shared sub-spell slots (LA_Sub_000..499) used by mixed-range spells, 0-500.}

bool Function DeleteCustomSpell(Spell akSpell) global native
{Deletes a custom spell: removes it from the player, blanks and frees its slot(s),
sends LostArt_SpellDeleted. Returns false if akSpell is not a custom spell.}

Function SetAltarActive(ObjectReference akAltar, bool abActive) global native
{Enables or disables one Altar of Spellmaking reference (for a future questline or
other mods gating access). A disabled altar behaves as ordinary furniture.}

int Function GetRefusalReason(Actor akSpellmaker) global native
{Why akSpellmaker would refuse to make a spell for the player right now:
0 serves, 1 not a College member, 2 wanted in the hold, 3 quest not done
(Neloth/Talvas: "Reluctant Steward"; Othreloth: "Clean Sweep"), 4 stage-4 vampire,
5 relationship rank below Acquaintance, 6 service disabled in the MCM.}

; -----------------------------------------------------------------------------
; Settings and maintenance (called by LostArt_MCM)
; -----------------------------------------------------------------------------

Function ReloadSettings() global native
{Re-reads Data/MCM/Config/LostArt/settings.ini (defaults) and
Data/MCM/Settings/LostArt.ini (user values, which win) and applies them.}

int Function RebalanceAll() global native
{Recomputes the cost of every custom spell under the current cost model and
settings and rewrites the slots. Returns the number of spells rebalanced, or a
negative number on failure.}

int Function RebuildAllSlots() global native
{Re-applies every stored spell definition to its slot records (the same pass that
runs on every save load). Returns the number of spells rebuilt, or a negative number
on failure.}

bool Function PrepareForUninstall() global native
{Removes custom spells from the player and followers, dispels their effects, clears
marks and ledgers and blanks all slots. Returns true when it is safe to save and
remove the mod.}

string Function GetVersion() global native
{Version of the loaded LostArt.dll, e.g. "1.0.0". Empty string if the DLL is missing.}

int Function RunTests(string asSuite) global native
{Runs the in-game scripted checks for asSuite ("parity", "effects", "save",
"compiler" or "all") and writes Documents/My Games/Skyrim Special Edition/SKSE/
LostArt_Tests.log. Returns the number of failed checks (0 = all passed), or a
negative number if the suite name is unknown.}

; Mod events: LostArt_SpellCreated(Form akSpell), LostArt_SpellDeleted(Form akSpell)
