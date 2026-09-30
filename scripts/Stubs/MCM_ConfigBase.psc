Scriptname MCM_ConfigBase extends SKI_ConfigBase
{COMPILE STUB - NOT SHIPPED. Public interface of MCM Helper's MCM_ConfigBase, copied
from github.com/Exit-9B/MCM-Helper scripts/public/MCM_ConfigBase.psc (MIT,
Copyright (c) 2021-2022 Parapets). The real script ships with MCM Helper.
Never install this file.}

Event OnSettingChange(string a_ID)
EndEvent

Event OnPageSelect(string a_page)
EndEvent

Event OnConfigInit()
EndEvent

Event OnConfigOpen()
EndEvent

Event OnConfigClose()
EndEvent

Function RefreshMenu() native
Function SetMenuOptions(string a_ID, string[] a_options, string[] a_shortNames = None) native

int Function GetModSettingInt(string a_settingName) native
bool Function GetModSettingBool(string a_settingName) native
float Function GetModSettingFloat(string a_settingName) native
string Function GetModSettingString(string a_settingName) native

Function SetModSettingInt(string a_settingName, int a_value) native
Function SetModSettingBool(string a_settingName, bool a_value) native
Function SetModSettingFloat(string a_settingName, float a_value) native
Function SetModSettingString(string a_settingName, string a_value) native
