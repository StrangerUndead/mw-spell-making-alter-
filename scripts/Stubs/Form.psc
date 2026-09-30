Scriptname Form Hidden
{COMPILE STUB - NOT SHIPPED. Minimal subset of the vanilla/SKSE Form script
(signatures from the Creation Kit wiki, https://ck.uesp.net/wiki/Form_Script, and
SKSE64's Form.psc). Only for compile-checking Lost Art of Spellmaking; the real script
comes with the game and SKSE. Never install this file.}

int Function GetFormID() native
string Function GetName() native

; SKSE
Function RegisterForModEvent(string eventName, string callbackName) native
Function UnregisterForModEvent(string eventName) native
Function SendModEvent(string eventName, string strArg = "", float numArg = 0.0) native
