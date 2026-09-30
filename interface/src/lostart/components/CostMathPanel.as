/*
 * "Show cost math" (F1 / left stick): per-effect parts of the cost as the DLL computed them,
 * with each effect's share and the running total. In Classic mode the running total makes the
 * Target x1.5 order quirk visible.
 * Rows: state.costMath = [{text, share, runningTotal}].
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.CostMathPanel
{
	public var clip: MovieClip;

	private var _bg: MovieClip;
	private var _titleTf: TextField;
	private var _headShare: TextField;
	private var _headRun: TextField;
	private var _rows: Array;
	private var _totalTf: TextField;
	private var _w: Number = 600;
	private var _h: Number = 300;
	private static var MAX_ROWS: Number = 8;

	public function CostMathPanel(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_titleTf = Text.spacing(Text.create(clip, "title", 2, 0, 0, 300, 34, Theme.FS_HEADER, Theme.TEXT, Theme.FONT_MEDIUM, "left"), Theme.LETTER_SPACING);
		_headShare = Text.label(clip, "hs", 3, 150, "right");
		_headRun = Text.label(clip, "hr", 4, 150, "right");
		Text.setCaps(_headShare, "$LA_UI_CostShare");
		Text.setCaps(_headRun, "$LA_UI_CostMathRunning");
		_rows = [];
		for (var i: Number = 0; i < MAX_ROWS; i++) {
			var t: TextField = Text.create(clip, "t" + i, 10 + i * 3, 0, 0, 100, 26, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_REGULAR, "left");
			var s: TextField = Text.create(clip, "s" + i, 11 + i * 3, 0, 0, 100, 26, Theme.FS_SMALL, Theme.TEXT, Theme.FONT_MEDIUM, "right");
			var r: TextField = Text.create(clip, "r" + i, 12 + i * 3, 0, 0, 100, 26, Theme.FS_SMALL, Theme.TEXT, Theme.FONT_MEDIUM, "right");
			_rows.push({text: t, share: s, run: r});
		}
		_totalTf = Text.create(clip, "total", 50, 0, 0, 300, 30, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "right");
		clip._visible = false;
	}

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
	}

	public function setData(a_visible: Boolean, a_rows: Array, a_modelName: String, a_costText: String): Void
	{
		clip._visible = a_visible;
		if (!a_visible)
			return;
		var pad: Number = 20;
		Draw.modalPanel(_bg, _w, _h);
		var title: String = Translator.tr("$LA_UI_CostMathTitle").toUpperCase();
		if (a_modelName != undefined && a_modelName.length > 0)
			title += "  (" + Translator.tr(a_modelName) + ")";
		_titleTf._x = pad;
		_titleTf._y = pad - 4;
		_titleTf._width = _w - 2 * pad;
		Text.setFit(_titleTf, title);
		var colW: Number = 150;
		var y: Number = pad + 38;
		_headShare._x = _w - pad - 2 * colW - 10;
		_headShare._y = y;
		_headShare._width = colW;
		_headRun._x = _w - pad - colW;
		_headRun._y = y;
		_headRun._width = colW;
		y += 26;
		Draw.divider(_bg, pad, y, _w - 2 * pad, 35);
		y += 6;
		var rowH: Number = Math.max(22, Math.min(30, (_h - y - 50) / MAX_ROWS));
		var n: Number = a_rows == undefined ? 0 : Math.min(MAX_ROWS, a_rows.length);
		for (var i: Number = 0; i < MAX_ROWS; i++) {
			var r: Object = _rows[i];
			var vis: Boolean = i < n;
			r.text._visible = r.share._visible = r.run._visible = vis;
			if (!vis)
				continue;
			var d: Object = a_rows[i];
			r.text._x = pad;
			r.text._y = y;
			r.text._width = _w - 2 * pad - 2 * colW - 16;
			Text.setFit(r.text, String(d.text));
			r.share._x = _w - pad - 2 * colW - 10;
			r.share._y = y;
			r.share._width = colW;
			r.share.text = fmt(d.share);
			r.run._x = _w - pad - colW;
			r.run._y = y;
			r.run._width = colW;
			r.run.text = fmt(d.runningTotal);
			y += rowH;
		}
		_totalTf._x = pad;
		_totalTf._y = _h - pad - 30;
		_totalTf._width = _w - 2 * pad;
		_totalTf.text = Translator.tr("$LA_UI_MagickaCost") + "  " + (a_costText == undefined ? "" : a_costText);
	}

	private static function fmt(a_v: Object): String
	{
		if (a_v == undefined || a_v == null)
			return "";
		var n: Number = Number(a_v);
		if (isNaN(n))
			return String(a_v);
		return String(Math.round(n * 100) / 100);
	}
}
