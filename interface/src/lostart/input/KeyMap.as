/*
 * The menu's complete key/button binding table (docs/OUTLINE.md "Controls", plus the few
 * bindings the outline leaves open: Load, Clear, list paging). Pure data + lookup, no UI, so it
 * can be exercised offline (interface/test).
 *
 * Every row matches either on the keyboard (Flash key code OR DirectInput scan code as
 * reported by skse.GetLastKeycode) or on the gamepad (SKSE gamepad code 266+ OR the GFx pad
 * key code 96..107 Skyrim's menus receive). Rows are tried in order; the first match wins, so
 * Shift variants come before the plain ones.
 */
import gfx.ui.NavigationCode;

class lostart.input.KeyMap
{
	/* ---- actions ---- */
	public static var UP: String = "up";
	public static var DOWN: String = "down";
	public static var LEFT: String = "left";
	public static var RIGHT: String = "right";
	public static var PAGE_UP: String = "pageUp";
	public static var PAGE_DOWN: String = "pageDown";
	public static var HOME: String = "home";
	public static var END: String = "end";
	public static var ACCEPT: String = "accept";
	public static var CANCEL: String = "cancel";
	public static var TAB_PREV: String = "tabPrev";
	public static var TAB_NEXT: String = "tabNext";
	public static var MOVE_UP: String = "moveUp";
	public static var MOVE_DOWN: String = "moveDown";
	public static var REMOVE: String = "remove";
	public static var CYCLE_RANGE: String = "cycleRange";
	public static var STEP_DEC: String = "stepDec";
	public static var STEP_INC: String = "stepInc";
	public static var BIG_DEC: String = "bigDec";
	public static var BIG_INC: String = "bigInc";
	public static var RENAME: String = "rename";
	public static var CREATE: String = "create";
	public static var COST_MATH: String = "costMath";
	public static var SEARCH: String = "search";
	public static var LOAD: String = "load";
	public static var CLEAR: String = "clear";

	/* ---- contexts ---- */
	public static var CTX_KNOWN: String = "known";       // Effects Known pane focused
	public static var CTX_EFFECTS: String = "effects";   // Spell Effects pane focused
	public static var CTX_EDITOR: String = "editor";     // effect editor modal
	public static var CTX_POPUP: String = "popup";       // picker / load list
	public static var CTX_MESSAGE: String = "message";   // message box
	public static var CTX_TEXT: String = "text";         // typing in name or search field

	/* ---- SKSE gamepad codes ---- */
	public static var PAD_DPAD_UP: Number = 266;
	public static var PAD_DPAD_DOWN: Number = 267;
	public static var PAD_DPAD_LEFT: Number = 268;
	public static var PAD_DPAD_RIGHT: Number = 269;
	public static var PAD_START: Number = 270;
	public static var PAD_BACK: Number = 271;
	public static var PAD_LS: Number = 272;
	public static var PAD_RS: Number = 273;
	public static var PAD_LB: Number = 274;
	public static var PAD_RB: Number = 275;
	public static var PAD_A: Number = 276;
	public static var PAD_B: Number = 277;
	public static var PAD_X: Number = 278;
	public static var PAD_Y: Number = 279;
	public static var PAD_LT: Number = 280;
	public static var PAD_RT: Number = 281;

	/* GFx key codes Skyrim sends for pad buttons (same table vanilla InputDelegate uses). */
	public static var GFX_PAD_FIRST: Number = 96;
	public static var GFX_PAD_LAST: Number = 107;

	/*
	 * ctx : comma list of contexts, "*" = all except text
	 * kb/dx: Flash codes / DirectInput scan codes    pad/gp: SKSE pad codes / GFx pad codes
	 * shift: true = Shift required, false = Shift must be up, undefined = either
	 * any  : true = the kb code matches whatever device reported it (arrow keys come from the
	 *        D-pad and left stick too)
	 */
	public static var BINDINGS: Array = [
		// text entry: only the keys that end editing
		{ctx: "text", action: "accept", kb: [13], dx: [28, 156], pad: [276], gp: [96]},
		{ctx: "text", action: "cancel", kb: [27, 9], dx: [1, 15], pad: [277], gp: [97]},

		// reorder (Spell Effects)
		{ctx: "effects", action: "moveUp", shift: true, kb: [38], dx: [200]},
		{ctx: "effects", action: "moveDown", shift: true, kb: [40], dx: [208]},
		{ctx: "effects", action: "moveUp", pad: [274], gp: [100]},
		{ctx: "effects", action: "moveDown", pad: [275], gp: [103]},

		// school tabs
		{ctx: "known,effects", action: "tabPrev", kb: [65], dx: [30]},
		{ctx: "known,effects", action: "tabNext", kb: [68], dx: [32]},
		{ctx: "known", action: "tabPrev", pad: [274], gp: [100]},
		{ctx: "known", action: "tabNext", pad: [275], gp: [103]},

		// editor sliders: Shift+Left/Right and LT/RT are big steps, Left/Right/D-pad single steps
		{ctx: "editor", action: "bigDec", shift: true, kb: [37], dx: [203]},
		{ctx: "editor", action: "bigInc", shift: true, kb: [39], dx: [205]},
		{ctx: "editor", action: "bigDec", pad: [280], gp: [101]},
		{ctx: "editor", action: "bigInc", pad: [281], gp: [104]},
		{ctx: "editor", action: "stepDec", any: true, kb: [37], dx: [203], pad: [268]},
		{ctx: "editor", action: "stepInc", any: true, kb: [39], dx: [205], pad: [269]},
		{ctx: "editor", action: "cycleRange", kb: [70], dx: [33], pad: [279], gp: [99]},
		{ctx: "editor", action: "remove", kb: [46], dx: [211], pad: [278], gp: [98]},

		// list navigation (every device)
		{ctx: "known,effects,editor,popup", action: "up", any: true, kb: [38], dx: [200], pad: [266]},
		{ctx: "known,effects,editor,popup", action: "down", any: true, kb: [40], dx: [208], pad: [267]},
		{ctx: "known,effects,popup", action: "left", any: true, kb: [37], dx: [203], pad: [268]},
		{ctx: "known,effects,popup", action: "right", any: true, kb: [39], dx: [205], pad: [269]},
		{ctx: "known,effects,popup", action: "pageUp", kb: [33], dx: [201]},
		{ctx: "known,effects,popup", action: "pageDown", kb: [34], dx: [209]},
		{ctx: "known,effects,popup", action: "home", kb: [36], dx: [199]},
		{ctx: "known,effects,popup", action: "end", kb: [35], dx: [207]},

		// main-view commands
		{ctx: "effects", action: "remove", kb: [46], dx: [211]},
		{ctx: "known,effects", action: "rename", kb: [84], dx: [20], pad: [279], gp: [99]},
		{ctx: "known,effects", action: "create", kb: [82], dx: [19], pad: [278], gp: [98]},
		{ctx: "known,effects,editor", action: "costMath", kb: [112], dx: [59], pad: [272], gp: [102]},
		{ctx: "known,effects", action: "search", kb: [191, 111], dx: [53, 181], pad: [273], gp: [105]},
		{ctx: "known,effects", action: "load", kb: [76], dx: [38], pad: [280], gp: [101]},
		{ctx: "known,effects", action: "clear", kb: [67], dx: [46], pad: [271], gp: [107]},

		// accept / back
		{ctx: "known,effects,editor,popup,message", action: "accept", kb: [13, 69], dx: [28, 156, 18], pad: [276], gp: [96]},
		{ctx: "known,effects,editor,popup,message", action: "cancel", kb: [9, 27], dx: [15, 1], pad: [277], gp: [97]}
	];

	/* Key captions for the procedural button art. */
	private static var KB_NAMES: Object = {
		k13: "Enter", k69: "E", k9: "Tab", k27: "Esc", k46: "Del", k70: "F", k84: "T", k82: "R",
		k112: "F1", k191: "/", k76: "L", k67: "C", k65: "A", k68: "D", k37: "Left", k39: "Right",
		k38: "Up", k40: "Down", k33: "PgUp", k34: "PgDn", k36: "Home", k35: "End", k111: "/"
	};
	private static var PAD_NAMES: Object = {
		p266: "D-Up", p267: "D-Down", p268: "D-Left", p269: "D-Right", p270: "Start", p271: "Back",
		p272: "LS", p273: "RS", p274: "LB", p275: "RB", p276: "A", p277: "B", p278: "X", p279: "Y",
		p280: "LT", p281: "RT"
	};

	/* ---------------------------------------------------------------------------------- */

	public static function hasSkse(): Boolean
	{
		return _global.skse != undefined;
	}

	/*
	 * Which device produced this event. SKSE's last keycode is authoritative in game; without
	 * SKSE (offline harness) the GFx pad range 96..107 is treated as gamepad, which lets the
	 * harness drive "gamepad" input from the numeric keypad.
	 */
	public static function deviceFor(a_code: Number, a_skseCode: Number): String
	{
		if (a_skseCode != undefined && a_skseCode >= 0) {
			if (a_skseCode >= PAD_DPAD_UP)
				return "pad";
			return "kbm";
		}
		if (a_code >= GFX_PAD_FIRST && a_code <= GFX_PAD_LAST)
			return "pad";
		return "kbm";
	}

	public static function navFor(a_code: Number, a_shift: Boolean): String
	{
		switch (a_code) {
			case 38: return NavigationCode.UP;
			case 40: return NavigationCode.DOWN;
			case 37: return NavigationCode.LEFT;
			case 39: return NavigationCode.RIGHT;
			case 13: return NavigationCode.ENTER;
			case 27: return NavigationCode.ESCAPE;
			case 9: return a_shift ? NavigationCode.SHIFT_TAB : NavigationCode.TAB;
			case 36: return NavigationCode.HOME;
			case 35: return NavigationCode.END;
			case 33: return NavigationCode.PAGE_UP;
			case 34: return NavigationCode.PAGE_DOWN;
			case 96: return NavigationCode.GAMEPAD_A;
			case 97: return NavigationCode.GAMEPAD_B;
			case 98: return NavigationCode.GAMEPAD_X;
			case 99: return NavigationCode.GAMEPAD_Y;
			case 100: return NavigationCode.GAMEPAD_L1;
			case 101: return NavigationCode.GAMEPAD_L2;
			case 102: return NavigationCode.GAMEPAD_L3;
			case 103: return NavigationCode.GAMEPAD_R1;
			case 104: return NavigationCode.GAMEPAD_R2;
			case 105: return NavigationCode.GAMEPAD_R3;
			case 106: return NavigationCode.GAMEPAD_START;
			case 107: return NavigationCode.GAMEPAD_BACK;
		}
		return undefined;
	}

	private static function has(a_list: Array, a_value: Number): Boolean
	{
		if (a_list == undefined || a_value == undefined)
			return false;
		for (var i: Number = 0; i < a_list.length; i++) {
			if (a_list[i] == a_value)
				return true;
		}
		return false;
	}

	private static function ctxMatches(a_rowCtx: String, a_ctx: String): Boolean
	{
		if (a_rowCtx == "*")
			return a_ctx != CTX_TEXT;
		var parts: Array = a_rowCtx.split(",");
		for (var i: Number = 0; i < parts.length; i++) {
			if (parts[i] == a_ctx)
				return true;
		}
		return false;
	}

	/*
	 * Resolves an event to an action in a context; undefined when unbound.
	 * a_code: Flash code, a_skse: SKSE keycode (may be undefined), a_device: "kbm"/"pad".
	 */
	public static function resolve(a_ctx: String, a_code: Number, a_skse: Number, a_device: String, a_shift: Boolean): String
	{
		for (var i: Number = 0; i < BINDINGS.length; i++) {
			var b: Object = BINDINGS[i];
			if (!ctxMatches(b.ctx, a_ctx))
				continue;
			if (b.shift == true && !a_shift)
				continue;
			if (b.shift == false && a_shift)
				continue;

			if (a_device == "pad") {
				if (has(b.pad, a_skse))
					return b.action;
				if ((a_skse == undefined || a_skse < 0) && has(b.gp, a_code))
					return b.action;
				if (b.any == true && has(b.kb, a_code))
					return b.action;
			} else {
				if (a_skse != undefined && a_skse >= 0) {
					if (has(b.dx, a_skse))
						return b.action;
				} else if (has(b.kb, a_code)) {
					return b.action;
				}
			}
		}
		return undefined;
	}

	/* Caption for the first binding of an action in a context on a device ("R", "X", ...). */
	public static function caption(a_action: String, a_ctx: String, a_device: String): String
	{
		for (var i: Number = 0; i < BINDINGS.length; i++) {
			var b: Object = BINDINGS[i];
			if (b.action != a_action || !ctxMatches(b.ctx, a_ctx))
				continue;
			if (a_device == "pad") {
				if (b.pad != undefined && b.pad.length > 0 && PAD_NAMES["p" + b.pad[0]] != undefined)
					return PAD_NAMES["p" + b.pad[0]];
			} else {
				if (b.kb != undefined && b.kb.length > 0 && KB_NAMES["k" + b.kb[0]] != undefined)
					return (b.shift == true ? "Shift+" : "") + KB_NAMES["k" + b.kb[0]];
			}
		}
		return undefined;
	}
}
