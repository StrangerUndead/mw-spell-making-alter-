/*
 * One key/button event, normalised. Mirrors the fields of CLIK's InputDetails that Skyrim and
 * SkyUI menus rely on (code, value, navEquivalent, control, skseKeycode) plus the resolved
 * device and the Shift state.
 */
class lostart.input.InputDetails
{
	public var code: Number;           // Flash/GFx key code (Key.getCode())
	public var value: String;          // "keyDown" | "keyHold" | "keyUp"
	public var navEquivalent: String;  // gfx.ui.NavigationCode value or undefined
	public var skseKeycode: Number;    // SKSE key code: DX scan code, 256+ mouse, 266+ gamepad
	public var control: String;        // SKSE control name ("Accept", "Cancel", ...) if known
	public var device: String;         // "kbm" | "pad"
	public var shift: Boolean;         // Shift held (keyboard)

	public function InputDetails(a_code: Number, a_value: String, a_nav: String, a_skse: Number,
		a_control: String, a_device: String, a_shift: Boolean)
	{
		code = a_code;
		value = a_value;
		navEquivalent = a_nav;
		skseKeycode = a_skse;
		control = a_control;
		device = a_device;
		shift = a_shift;
	}

	public function isPress(a_allowRepeat: Boolean): Boolean
	{
		return value == "keyDown" || (a_allowRepeat && value == "keyHold");
	}

	public function toString(): String
	{
		return "[Input " + value + " code=" + code + " skse=" + skseKeycode + " dev=" + device +
			(shift ? " shift" : "") + " nav=" + navEquivalent + " ctl=" + control + "]";
	}
}
