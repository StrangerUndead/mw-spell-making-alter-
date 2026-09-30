Scriptname SKI_ConfigBase extends SKI_QuestBase Hidden
{COMPILE STUB - NOT SHIPPED. Public interface of SkyUI's SKI_ConfigBase (API version 4),
reduced from the header MCM Helper publishes (github.com/Exit-9B/MCM-Helper,
scripts/public/SKI_ConfigBase.psc, MIT). The real script ships with SkyUI / MCM Helper.
Never install this file.}

string Property ModName Auto
string[] Property Pages Auto

string Property CurrentPage
	string Function Get()
		return ""
	EndFunction
EndProperty

Event OnConfigInit()
EndEvent

Event OnConfigOpen()
EndEvent

Event OnConfigClose()
EndEvent

Event OnConfigManagerReady(string a_eventName, string a_strArg, float a_numArg, Form a_sender)
EndEvent

Function ForcePageReset()
EndFunction

Function SetTitleText(string a_text)
EndFunction

Function SetInfoText(string a_text)
EndFunction

bool Function ShowMessage(string a_message, bool a_withCancel = true, string a_acceptLabel = "$Accept", string a_cancelLabel = "$Cancel")
	return false
EndFunction
