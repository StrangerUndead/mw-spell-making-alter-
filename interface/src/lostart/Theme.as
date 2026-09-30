/*
 * Visual constants: Skyrim's own menu language as SkyUI draws it (Interface/skyui/config.txt
 * [Appearance]/[ListLayout], the vanilla crafting and magic menus): white text on a darkened
 * scene, grey upper-case labels, hairline dividers that fade out at both ends, no boxed panels
 * and no colour accents except the elemental icon tints.
 *
 * The menu is laid out in a virtual 1080-unit-high design space and scaled to the visible rect
 * (see LostArtSpellmakingMenu.relayout), so every size here is "pixels at 1080p".
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
	public static var MAX_CONTENT_WIDTH: Number = 1760;   // keeps 21:9 / 32:9 readable
	public static var MIN_MARGIN_X: Number = 64;
	public static var MIN_MARGIN_Y: Number = 40;
	public static var GAP: Number = 24;

	/* type sizes (SkyUI list entries are 14 at its 720-high stage -> 21 at 1080) */
	public static var FS_MENU_TITLE: Number = 34;   // "ALTAR OF SPELLMAKING"
	public static var FS_TITLE: Number = 30;        // item card / editor title
	public static var FS_HEADER: Number = 24;
	public static var FS_BODY: Number = 21;
	public static var FS_SMALL: Number = 19;
	public static var FS_LABEL: Number = 16;        // grey upper-case column labels
	public static var FS_HINT: Number = 18;         // bottom-bar button hints
	public static var LETTER_SPACING: Number = 1.2; // SkyUI letterSpacing 0.8 at 720p

	public static var ROW_H: Number = 34;           // Effects Known / popups
	public static var ROW_H_EFFECT: Number = 56;    // Spell Effects (two lines)
	public static var ICON_SIZE: Number = 24;       // list icons (SkyUI n_iconSize 18 at 720p)
	public static var TAB_ICON_SIZE: Number = 38;   // category bar icons

	/* colours (SkyUI colors.text.enabled / disabled / negative) */
	public static var TEXT: Number = 0xFFFFFF;
	public static var TEXT_SOFT: Number = 0xC8C8C8;    // unselected list entries
	public static var TEXT_DIM: Number = 0x4C4C4C;     // disabled entries
	public static var TEXT_HINT: Number = 0x999999;    // labels, placeholders
	public static var TEXT_RED: Number = 0xE23A3A;     // cannot afford
	public static var TEXT_GOOD: Number = 0x9FD27A;

	public static var OVERLAY: Number = 0x000000;
	public static var OVERLAY_ALPHA: Number = 38;      // whole-scene darkening
	public static var BACKDROP_ALPHA: Number = 82;     // list-side gradient, opaque end
	public static var PANEL: Number = 0x000000;
	public static var PANEL_ALPHA: Number = 72;
	public static var MODAL_ALPHA: Number = 94;
	public static var BORDER: Number = 0xFFFFFF;
	public static var BORDER_ALPHA: Number = 32;       // fading dividers
	public static var BORDER_FOCUS_ALPHA: Number = 60;
	public static var ROW_SELECT: Number = 0xFFFFFF;
	public static var ROW_SELECT_ALPHA: Number = 9;
	public static var ROW_SELECT_FOCUS_ALPHA: Number = 18;
	public static var ACCENT: Number = 0xFFFFFF;       // Skyrim highlights in white, not colour

	/* schools, contract order 0..4: only used to tint the drawn fallback icons */
	public static var SCHOOL_COLORS: Array = [
		0xFFFFFF,   // Alteration
		0xFFFFFF,   // Conjuration
		0xFFFFFF,   // Destruction
		0xFFFFFF,   // Illusion
		0xFFFFFF    // Restoration
	];
	public static var NEUTRAL: Number = 0xFFFFFF;

	/* SkyUI's elemental icon tints (MagicIconSetter.processResist) */
	public static var ICON_FIRE: Number = 0xC73636;
	public static var ICON_SHOCK: Number = 0xEAAB00;
	public static var ICON_FROST: Number = 0x1FFBFF;

	/* SkyUI icon libraries, loaded at runtime from the player's SkyUI install (never shipped). */
	public static var ICONS_CATEGORY: String = "skyui/icons_category_psychosteve.swf";
	public static var ICONS_ITEM: String = "skyui/icons_item_psychosteve.swf";
	/* SkyUI's magic category frame labels, tab order All, A, C, D, I, R. */
	public static var TAB_ICON_LABELS: Array = ["mag_all", "mag_alteration", "mag_conjuration",
		"mag_destruction", "mag_illusion", "mag_restoration"];
	public static var SCHOOL_ICON_LABELS: Array = ["default_alteration", "default_conjuration",
		"default_destruction", "default_illusion", "default_restoration"];

	public static function schoolColor(a_school: Number): Number
	{
		if (a_school == undefined || a_school < 0 || a_school >= SCHOOL_COLORS.length)
			return NEUTRAL;
		return SCHOOL_COLORS[a_school];
	}

	public static function schoolIcon(a_school: Number): String
	{
		if (a_school == undefined || a_school < 0 || a_school >= SCHOOL_ICON_LABELS.length)
			return "default_effect";
		return SCHOOL_ICON_LABELS[a_school];
	}

	/* Gamepad face-button colours for the procedural button art. */
	public static var PAD_A: Number = 0x5AA532;
	public static var PAD_B: Number = 0xD2402F;
	public static var PAD_X: Number = 0x2F7FD2;
	public static var PAD_Y: Number = 0xE3B32B;
}
