/*
 * Declarations of the natives SKSE64 installs as _global.skse in every menu movie.
 * Intrinsic: nothing is compiled, calls resolve at runtime. Outside the game (Ruffle harness,
 * standalone players) _global.skse is undefined and every call is a silent no-op.
 * Only the functions this menu uses are declared.
 */
intrinsic class skse
{
	static function Log(a_string: String): Void;
	static function AllowTextInput(a_flag: Boolean): Void;
	static function GetMappedKey(a_name: String, a_deviceType: Number, a_context: Number): Number;
	static function GetLastControl(a_bKeyDown: Boolean): String;
	static function GetLastKeycode(a_bKeyDown: Boolean): Number;
}
