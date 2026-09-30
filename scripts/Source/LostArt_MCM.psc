Scriptname LostArt_MCM extends MCM_ConfigBase
{Lost Art of Spellmaking - MCM Helper config script.

Attached to the MCM quest in LostArt.esp (see docs/dev/PAPYRUS.md). The menu layout
is Data/MCM/Config/LostArt/config.json; every setting is a ModSetting stored by MCM
Helper in Data/MCM/Settings/LostArt.ini, which the DLL reads. This script only:
  - tells the DLL when a setting changed (mod event + LostArt.ReloadSettings),
  - fills the read-only slot/version readouts,
  - runs the Maintenance buttons and reports their results.
No polling, no update registrations, no persistent state beyond the readout strings.}

; -----------------------------------------------------------------------------
; Readouts (bound in config.json through valueOptions.propertyName)
; -----------------------------------------------------------------------------

string Property SpellSlotsText Auto Hidden
{"used / total" for the 500 primary spell slots.}

string Property SubSlotsText Auto Hidden
{"used / total" for the 500 shared sub-spell slots.}

string Property VersionText Auto Hidden
{Version of the loaded LostArt.dll, or "$LA_MCM_DllMissing".}

int Property SubSlotTotal = 500 AutoReadOnly Hidden
{Number of LA_Sub_* records in LostArt_Slots.esp (CONTRACTS section 2).}

; -----------------------------------------------------------------------------
; MCM Helper events
; -----------------------------------------------------------------------------

Event OnConfigOpen()
	UpdateReadouts()
EndEvent

Event OnPageSelect(string a_page)
	; MCM Helper draws the page before this event, so refresh the drawn values.
	UpdateReadouts()
	RefreshMenu()
EndEvent

Event OnSettingChange(string a_ID)
	; MCM Helper has already written the new value to Data/MCM/Settings/LostArt.ini.
	SendModEvent("LostArt_SettingsChanged", a_ID)
	LostArt.ReloadSettings()
EndEvent

; -----------------------------------------------------------------------------
; Readouts
; -----------------------------------------------------------------------------

Function UpdateReadouts()
	string version = LostArt.GetVersion()
	if version == ""
		VersionText = "$LA_MCM_DllMissing"
		SpellSlotsText = "-"
		SubSlotsText = "-"
		return
	endif
	VersionText = version

	int used = LostArt.GetCustomSpellCount()
	int total = used + LostArt.GetFreeSlotCount()
	SpellSlotsText = used + " / " + total

	int subUsed = SubSlotTotal - LostArt.GetFreeSubSlotCount()
	if subUsed < 0
		subUsed = 0
	endif
	SubSlotsText = subUsed + " / " + SubSlotTotal
EndFunction

; -----------------------------------------------------------------------------
; Maintenance buttons (config.json: "action": {"type": "CallFunction", ...})
; -----------------------------------------------------------------------------

Function DoRebalanceAll()
	if !ShowMessage("$LA_MCM_Msg_RebalanceConfirm", true)
		return
	endif
	int n = LostArt.RebalanceAll()
	if n < 0
		ShowMessage("$LA_MCM_Msg_Failed", false)
	else
		ShowMessage("$LA_MCM_Msg_Rebalanced{" + n + "}", false)
	endif
	UpdateReadouts()
	RefreshMenu()
EndFunction

Function DoRebuildAllSlots()
	int n = LostArt.RebuildAllSlots()
	if n < 0
		ShowMessage("$LA_MCM_Msg_Failed", false)
	else
		ShowMessage("$LA_MCM_Msg_Rebuilt{" + n + "}", false)
	endif
	UpdateReadouts()
	RefreshMenu()
EndFunction

Function DoPrepareForUninstall()
	if !ShowMessage("$LA_MCM_Msg_UninstallConfirm", true)
		return
	endif
	if LostArt.PrepareForUninstall()
		ShowMessage("$LA_MCM_Msg_UninstallDone", false)
	else
		ShowMessage("$LA_MCM_Msg_UninstallFailed", false)
	endif
	UpdateReadouts()
	RefreshMenu()
EndFunction
