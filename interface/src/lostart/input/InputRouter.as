/*
 * Turns GFx Key/Mouse listener callbacks into InputDetails and hands them to the menu.
 * Same approach as Skyrim's CLIK InputDelegate (Key.addListener + skse.GetLastKeycode /
 * GetLastControl) without the CLIK focus manager: the menu is the single input consumer and
 * routes by its own modal state.
 */
import lostart.input.InputDetails;
import lostart.input.KeyMap;

class lostart.input.InputRouter
{
	private var _target: Object;       // has handleInput(InputDetails): Boolean, handleWheel(delta)
	private var _held: Object;
	private var _shiftKeys: Object;
	private var _lastMouseX: Number;
	private var _lastMouseY: Number;

	public var lastDevice: String = "kbm";
	public var onDeviceChange: Function;   // fn(device)

	public function InputRouter(a_target: Object)
	{
		_target = a_target;
		_held = {};
		_shiftKeys = {};
		Key.addListener(this);
		Mouse.addListener(this);
	}

	public function isShiftDown(): Boolean
	{
		for (var k: String in _shiftKeys) {
			if (_shiftKeys[k])
				return true;
		}
		return Key.isDown(16);
	}

	private static function isShiftCode(a_code: Number, a_skse: Number): Boolean
	{
		return a_code == 16 || a_skse == 42 || a_skse == 54;
	}

	private function readCode(a_controllerIdx: Number): Number
	{
		var getCode: Function = Key["getCode"];
		return Number(getCode.call(Key, a_controllerIdx));
	}

	/* GFx passes the controller index to Key listeners. */
	public function onKeyDown(a_controllerIdx: Number): Void
	{
		var code: Number = readCode(a_controllerIdx);
		var skseCode: Number = KeyMap.hasSkse() ? skse.GetLastKeycode(true) : undefined;
		var control: String = KeyMap.hasSkse() ? skse.GetLastControl(true) : undefined;
		if (isShiftCode(code, skseCode)) {
			_shiftKeys["k" + code] = true;
			return;
		}
		var value: String = _held["k" + code] ? "keyHold" : "keyDown";
		_held["k" + code] = true;
		dispatch(code, value, skseCode, control);
	}

	public function onKeyUp(a_controllerIdx: Number): Void
	{
		var code: Number = readCode(a_controllerIdx);
		var skseCode: Number = KeyMap.hasSkse() ? skse.GetLastKeycode(false) : undefined;
		var control: String = KeyMap.hasSkse() ? skse.GetLastControl(false) : undefined;
		if (isShiftCode(code, skseCode)) {
			_shiftKeys["k" + code] = false;
			return;
		}
		_held["k" + code] = false;
		dispatch(code, "keyUp", skseCode, control);
	}

	private function dispatch(a_code: Number, a_value: String, a_skse: Number, a_control: String): Void
	{
		if (a_skse == 0)
			a_skse = undefined;
		var device: String = KeyMap.deviceFor(a_code, a_skse);
		setDevice(device);
		var shift: Boolean = device == "kbm" && isShiftDown();
		var d: InputDetails = new InputDetails(a_code, a_value, KeyMap.navFor(a_code, shift), a_skse, a_control, device, shift);
		_target.handleInput(d);
	}

	public function setDevice(a_device: String): Void
	{
		if (a_device == lastDevice)
			return;
		lastDevice = a_device;
		if (onDeviceChange != undefined)
			onDeviceChange(a_device);
	}

	/* Mouse listener */
	public function onMouseMove(): Void
	{
		var x: Number = _root._xmouse;
		var y: Number = _root._ymouse;
		if (_lastMouseX != undefined && (Math.abs(x - _lastMouseX) > 3 || Math.abs(y - _lastMouseY) > 3))
			setDevice("kbm");
		_lastMouseX = x;
		_lastMouseY = y;
	}

	public function onMouseWheel(a_delta: Number): Void
	{
		setDevice("kbm");
		_target.handleWheel(a_delta);
	}

	/* Forget held keys (e.g. after a modal closes on keyDown, so the keyUp is not misread). */
	public function reset(): Void
	{
		_held = {};
	}
}
