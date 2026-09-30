/*
 * Visual constants. The menu is laid out in a virtual 1080-unit-high design space and scaled
 * to the visible rect (see LostArtSpellmakingMenu.relayout), so every size here is "pixels at
 * 1080p".
 */
class lostart.Theme
{
	/* Fonts from Skyrim's Interface/fontconfig.txt: nothing is embedded in the SWF. */
	public static var FONT_REGULAR: String = "$EverywhereFont";
	public static var FONT_MEDIUM: String = "$EverywhereMediumFont";
	public static var FONT_BOLD: String = "$EverywhereBoldFont";

	/* true in-game (fontconfig fonts are "embedded" glyph libraries); the offline harness turns
	   it off and falls back to a device font. */
	public static var embedFonts: Boolean = true;
	public static var FALLBACK_FONT: String = "_sans";

	public static var DESIGN_HEIGHT: Number = 1080;
	public static var MAX_CONTENT_WIDTH: Number = 1800;   // keeps 21:9 / 32:9 readable
	public static var MIN_MARGIN_X: Number = 48;
	public static var MIN_MARGIN_Y: Number = 36;
	public static var GAP: Number = 16;

	/* type sizes */
	public static var FS_TITLE: Number = 30;
	public static var FS_HEADER: Number = 26;
	public static var FS_BODY: Number = 23;
	public static var FS_SMALL: Number = 20;
	public static var FS_HINT: Number = 17;

	public static var ROW_H: Number = 36;          // Effects Known / popups
	public static var ROW_H_EFFECT: Number = 58;   // Spell Effects (two lines)

	/* colours */
	public static var TEXT: Number = 0xFFFFFF;
	public static var TEXT_SOFT: Number = 0xD8D2C4;
	public static var TEXT_DIM: Number = 0x7D7A74;
	public static var TEXT_HINT: Number = 0x9C978C;
	public static var TEXT_RED: Number = 0xE5483B;
	public static var TEXT_GOOD: Number = 0x9FD27A;

	public static var OVERLAY: Number = 0x000000;
	public static var OVERLAY_ALPHA: Number = 45;
	public static var PANEL: Number = 0x000000;
	public static var PANEL_ALPHA: Number = 68;
	public static var MODAL_ALPHA: Number = 88;
	public static var BORDER: Number = 0xFFFFFF;
	public static var BORDER_ALPHA: Number = 22;
	public static var BORDER_FOCUS_ALPHA: Number = 55;
	public static var ROW_SELECT: Number = 0xFFFFFF;
	public static var ROW_SELECT_ALPHA: Number = 14;
	public static var ROW_SELECT_FOCUS_ALPHA: Number = 24;
	public static var ACCENT: Number = 0xE9DDB9;   // parchment highlight used for the focus bar

	/* schools, contract order 0..4 */
	public static var SCHOOL_COLORS: Array = [
		0x6FA9E2,   // Alteration
		0xA88CEB,   // Conjuration
		0xE8683A,   // Destruction
		0xE38FD1,   // Illusion
		0xE9D46B    // Restoration
	];
	public static var NEUTRAL: Number = 0xA8A39A;

	public static function schoolColor(a_school: Number): Number
	{
		if (a_school == undefined || a_school < 0 || a_school >= SCHOOL_COLORS.length)
			return NEUTRAL;
		return SCHOOL_COLORS[a_school];
	}

	/* Gamepad face-button colours for the procedural button art. */
	public static var PAD_A: Number = 0x5AA532;
	public static var PAD_B: Number = 0xD2402F;
	public static var PAD_X: Number = 0x2F7FD2;
	public static var PAD_Y: Number = 0xE3B32B;
}
