/*
 * SkyUI-style item card for the focused effect: name, school, Morrowind base cost, allowed
 * ranges, magnitude unit and the one-line "In Skyrim" note.
 * Entry = one element of LA_SetKnown: {id, text, school, schoolName, baseCost, ranges, unit, card}.
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.ItemCard
{
	public var clip: MovieClip;

	private var _bg: MovieClip;
	private var _orb: MovieClip;
	private var _titleTf: TextField;
	private var _metaTf: TextField;
	private var _unitTf: TextField;
	private var _noteTf: TextField;
	private var _emptyTf: TextField;
	private var _w: Number = 400;
	private var _h: Number = 200;
	private var _entry: Object;

	public function ItemCard(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_orb = clip.createEmptyMovieClip("orb", 2);
		_titleTf = Text.create(clip, "title", 3, 0, 0, 100, 40, Theme.FS_HEADER, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		_metaTf = Text.create(clip, "meta", 4, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_REGULAR, "left");
		_unitTf = Text.create(clip, "unit", 5, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_REGULAR, "left");
		_noteTf = Text.multiline(Text.create(clip, "note", 6, 0, 0, 100, 60, Theme.FS_SMALL, Theme.TEXT, Theme.FONT_REGULAR, "left"));
		_emptyTf = Text.create(clip, "empty", 7, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT_HINT, Theme.FONT_REGULAR, "center");
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

	private function render(): Void
	{
		Draw.panel(_bg, _w, _h, Theme.PANEL_ALPHA, false);
		_orb.clear();
		var has: Boolean = _entry != undefined;
		_titleTf._visible = _metaTf._visible = _unitTf._visible = _noteTf._visible = has;
		_emptyTf._visible = !has;
		var pad: Number = 22;
		if (!has) {
			_emptyTf._x = pad;
			_emptyTf._width = _w - 2 * pad;
			_emptyTf._y = _h / 2 - 16;
			Text.set(_emptyTf, "$LA_UI_CardEmpty");
			return;
		}
		var color: Number = Theme.schoolColor(_entry.school);
		Draw.circle(_orb, pad + 11, pad + 17, 11, color, 30);
		Draw.circle(_orb, pad + 11, pad + 17, 7, color, 100);

		var x: Number = pad + 32;
		var y: Number = pad;
		_titleTf._x = x;
		_titleTf._y = y;
		_titleTf._width = _w - x - pad;
		_titleTf._height = 40;
		Text.setFit(_titleTf, String(_entry.text));
		y += 42;
		Draw.divider(_bg, pad, y, _w - 2 * pad, 35);
		y += 10;

		var dot: String = "  " + String.fromCharCode(183) + "  ";
		var meta: String = Translator.tr(String(_entry.schoolName)) + dot +
			Translator.format("$LA_UI_BaseCost", [formatNumber(Number(_entry.baseCost))]);
		if (_entry.ranges != undefined && String(_entry.ranges).length > 0)
			meta += dot + Translator.tr(String(_entry.ranges));
		_metaTf._x = pad;
		_metaTf._y = y;
		_metaTf._width = _w - 2 * pad;
		_metaTf._height = 30;
		Text.setFit(_metaTf, meta);
		y += 30;

		var unit: String = _entry.unit == undefined ? "" : Translator.tr(String(_entry.unit));
		_unitTf._visible = unit.length > 0 && unit != "none";
		_unitTf._x = pad;
		_unitTf._y = y;
		_unitTf._width = _w - 2 * pad;
		_unitTf._height = 30;
		if (_unitTf._visible) {
			Text.setFit(_unitTf, Translator.format("$LA_UI_UnitLine", [unit]));
			y += 30;
		}

		var card: String = _entry.card == undefined ? "" : Translator.tr(String(_entry.card));
		_noteTf._visible = card.length > 0;
		_noteTf._x = pad;
		_noteTf._y = y + 6;
		_noteTf._width = _w - 2 * pad;
		_noteTf._height = Math.max(24, _h - y - 6 - pad + 6);
		if (_noteTf._visible)
			_noteTf.text = Translator.format("$LA_UI_InSkyrim", [card]);
	}
}
