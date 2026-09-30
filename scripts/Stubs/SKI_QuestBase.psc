Scriptname SKI_QuestBase extends Quest Hidden
{COMPILE STUB - NOT SHIPPED. Public interface of SkyUI's SKI_QuestBase, reduced from
the header MCM Helper publishes (github.com/Exit-9B/MCM-Helper,
scripts/public/SKI_QuestBase.psc, MIT). The real script ships with SkyUI.
Never install this file.}

int Property CurrentVersion Auto Hidden

Event OnInit()
EndEvent

Function CheckVersion()
EndFunction

int Function GetVersion()
	return 0
EndFunction

Event OnVersionUpdate(int a_version)
EndEvent

Event OnGameReload()
EndEvent
