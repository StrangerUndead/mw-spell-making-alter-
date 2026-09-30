/*
 * Item card for the focused effect, laid out like Skyrim's own item card (and SkyUI's
 * ItemCard): the name centred in large type, a fading rule, one centred line of grey
 * upper-case labels with white values (school, Morrowind base cost, ranges, unit), and the
 * one-line "In Skyrim" note centred underneath. The body is the dark band the vanilla card sits
 * on, with no box or colour.
 * Entry = one element of LA_SetKnown: {id, text, school, schoolName, baseCost, ranges, unit, card, icon, iconColor}.
 */
import lostart.Theme;
import lostart.components.SkyIcon;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.ItemCard
{
	public var clip: MovieClip;

	private var _bg: MovieClip;
	private var _icon: SkyIcon;
	private var _titleTf: TextField;
	private var _statsTf: TextField;
	private var _noteTf: TextField;
	private var _w: Number = 400;
	private var _h: Number = 200;
	private var _entry: Object;

	public function ItemCard(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_icon = new SkyIcon(clip, "icon", 2, Theme.ICONS_ITEM, 30);
		_titleTf = Text.create(clip, "title", 3, 0, 0, 100, 44, Theme.FS_TITLE, Theme.TEXT, Theme.FONT_MEDIUM, "center");
		_statsTf = Text.create(clip, "stats", 4, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT, Theme.FONT_MEDIUM, "center");
		_statsTf.html = true;
		_noteTf = Text.multiline(Text.create(clip, "note", 5, 0, 0, 100, 60, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_REGULAR, "center"));
	}

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		render();
	}

	public function setEntry(a_entry: Object): Void
	{
		_entry = a_entry;
		render();
	}

	public static function formatNumber(a_n: Number): String
	{
		if (a_n == undefined || isNaN(a_n))
			return "-";
		var r: Number = Math.round(a_n * 100) / 100;
		return String(r);
	}

	private static function esc(a_s: String): String
	{
		var out: String = "";
		for (var i: Number = 0; i < a_s.length; i++) {
			var c: String = a_s.charAt(i);
			if (c == "<")
				out += "&lt;";
			else if (c == ">")
				out += "&gt;";
			else if (c == "&")
				out += "&amp;";
			else
				out += c;
		}
		return out;
	}

	/* One "LABEL value" pair in the stats line. */
	private static function pair(a_labelKey: String, a_value: String): String
	{
		var label: String = esc(Translator.tr(a_labelKey).toUpperCase());
		return "<font color=\"#999999\" size=\"" + Theme.FS_LABEL + "\">" + label + "</font> " + esc(a_value);
	}

	private function render(): Void
	{
		_bg.clear();
		var has: Boolean = _entry != undefined;
		_titleTf._visible = _statsTf._visible = _noteTf._visible = has;
		_icon.clip._visible = has;
		if (!has)
			return;

		// the dark band behind the card: strongest in the middle, fading to the sides and edges
		Draw.gradient(_bg, 0, 0, _w, _h, 0x000000, [0, Theme.PANEL_ALPHA, Theme.PANEL_ALPHA, 0], [0, 50, 205, 255], false);
		Draw.divider(_bg, 0, 0, _w, Theme.BORDER_ALPHA);
		Draw.divider(_bg, 0, _h - 1, _w, Theme.BORDER_ALPHA);

		var pad: Number = 24;
		var y: Number = pad - 4;
		_titleTf._x = pad;
		_titleTf._y = y;
		_titleTf._width = _w - 2 * pad;
		_titleTf._height = 44;
		Text.setFit(_titleTf, String(_entry.text));
		// icon just left of the centred title
		var tw: Number = Math.min(_titleTf.textWidth, _w - 2 * pad);
		_icon.clip._x = Math.round(_w / 2 - tw / 2 - 42);
		_icon.clip._y = y + 5;
		_icon.show(_entry.icon == undefined || String(_entry.icon) == "" ? Theme.schoolIcon(Number(_entry.school)) : String(_entry.icon),
			_entry.iconColor == undefined ? 0xFFFFFF : Number(_entry.iconColor));
		y += 46;
		Draw.divider(_bg, _w * 0.15, y, _w * 0.7, 45);
		y += 12;

		var gap: String = "      ";
		var stats: String = pair("$LA_UI_School", Translator.tr(String(_entry.schoolName)));
		stats += gap + pair("$LA_UI_BaseCost", formatNumber(Number(_entry.baseCost)));
		if (_entry.ranges != undefined && String(_entry.ranges).length > 0)
			stats += gap + pair("$LA_UI_Ranges", Translator.tr(String(_entry.ranges)));
		var unit: String = _entry.unit == undefined ? "" : Translator.tr(String(_entry.unit));
		if (unit.length > 0 && unit != "none")
			stats += gap + pair("$LA_UI_Unit", unit);
		_statsTf._x = pad;
		_statsTf._y = y;
		_statsTf._width = _w - 2 * pad;
		_statsTf._height = 30;
		_statsTf.htmlText = stats;
		y += 36;

		var card: String = _entry.card == undefined ? "" : Translator.tr(String(_entry.card));
		_noteTf._visible = card.length > 0;
		_noteTf._x = pad * 2;
		_noteTf._y = y;
		_noteTf._width = _w - 4 * pad;
		_noteTf._height = Math.max(24, _h - y - pad + 8);
		if (_noteTf._visible)
			_noteTf.text = Translator.tr("$LA_UI_InSkyrim") + " " + card;
	}
}
