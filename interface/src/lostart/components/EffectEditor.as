/*
 * Effect editor: modal panel over the Spell Effects pane, mirroring Morrowind's dialog.
 *   header  : school orb, effect title ("Fortify Strength"), school name
 *   Range   : button cycling Self/Touch/Target (allowed ranges only; the DLL decides)
 *   Magnitude min slider, "to" max slider, Duration slider, Area slider (each hidden when the
 *             state says so)
 *   preview : the finished Morrowind line + this effect's cost
 *   OK / Cancel / Delete (Delete only when editing an existing row)
 *
 * The editor never computes a value: every change is an intent (onStep / onSet / onRange ...)
 * and the next LA_SetState repaints it.
 */
import lostart.Theme;
import lostart.components.Button;
import lostart.components.KeyGlyph;
import lostart.components.Slider;
import lostart.input.KeyMap;
import lostart.model.StateUtil;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.EffectEditor
{
	public var clip: MovieClip;

	/* intents */
	public var onStep: Function;     // fn(field, steps, big)
	public var onSet: Function;      // fn(field, value)
	public var onRange: Function;    // fn()
	public var onOk: Function;       // fn()
	public var onCancel: Function;   // fn()
	public var onDelete: Function;   // fn()
	public var onFocusMove: Function; // fn() - for the focus sound

	private var _bg: MovieClip;
	private var _orb: MovieClip;
	private var _titleTf: TextField;
	private var _schoolTf: TextField;
	private var _rangeRow: MovieClip;
	private var _rangeLabelTf: TextField;
	private var _rangeBox: MovieClip;
	private var _rangeTf: TextField;
	private var _rangeGlyph: KeyGlyph;
	private var _sliders: Object;
	private var _previewBg: MovieClip;
	private var _lineTf: TextField;
	private var _costTf: TextField;
	private var _okBtn: Button;
	private var _cancelBtn: Button;
	private var _deleteBtn: Button;

	private var _w: Number = 900;
	private var _h: Number = 640;
	private var _ed: Object;
	private var _rows: Array;        // focusable row ids in visual order
	private var _focusId: String = "range";
	private var _btnFocus: Number = 0;
	private var _device: String = "kbm";
	private var _visibleKey: String = "";

	private static var FIELDS: Array = ["min", "max", "duration", "area"];

	public function EffectEditor(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		var self: EffectEditor = this;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_orb = clip.createEmptyMovieClip("orb", 2);
		_titleTf = Text.create(clip, "title", 3, 0, 0, 100, 44, Theme.FS_TITLE, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		_schoolTf = Text.create(clip, "school", 4, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_MEDIUM, "right");

		_rangeRow = clip.createEmptyMovieClip("rangeRow", 10);
		_rangeLabelTf = Text.create(_rangeRow, "label", 2, 0, 0, 160, 30, Theme.FS_BODY, Theme.TEXT_SOFT, Theme.FONT_MEDIUM, "left");
		_rangeBox = _rangeRow.createEmptyMovieClip("box", 3);
		_rangeTf = Text.create(_rangeRow, "value", 4, 0, 0, 160, 30, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "center");
		_rangeGlyph = new KeyGlyph(_rangeRow, "glyph", 5, 24);
		Text.set(_rangeLabelTf, "$LA_UI_Range");
		_rangeBox.onRelease = function(): Void {
			self.focus("range");
			if (self.canCycle())
				self.onRange();
		};
		_rangeBox.useHandCursor = false;

		_sliders = {};
		var labels: Object = {min: "$LA_UI_Magnitude", max: "$LA_UI_To", duration: "$LA_UI_Duration", area: "$LA_UI_Area"};
		for (var i: Number = 0; i < FIELDS.length; i++) {
			var f: String = FIELDS[i];
			var s: Slider = new Slider(clip, "slider_" + f, 20 + i);
			s.setLabel(labels[f]);
			bindSlider(s, f);
			_sliders[f] = s;
		}

		_previewBg = clip.createEmptyMovieClip("preview", 30);
		_lineTf = Text.multiline(Text.create(clip, "line", 31, 0, 0, 100, 60, Theme.FS_BODY, Theme.TEXT, Theme.FONT_REGULAR, "left"));
		_costTf = Text.create(clip, "cost", 32, 0, 0, 100, 30, Theme.FS_SMALL, Theme.TEXT_SOFT, Theme.FONT_MEDIUM, "right");

		_okBtn = new Button(clip, "ok", 40, 46);
		_cancelBtn = new Button(clip, "cancel", 41, 46);
		_deleteBtn = new Button(clip, "delete", 42, 46);
		_okBtn.setLabel("$LA_UI_OK");
		_cancelBtn.setLabel("$LA_UI_Cancel");
		_deleteBtn.setLabel("$LA_UI_Delete");
		_okBtn.onPress = function(): Void { self.onOk(); };
		_cancelBtn.onPress = function(): Void { self.onCancel(); };
		_deleteBtn.onPress = function(): Void { self.onDelete(); };

		clip._visible = false;
	}

	private function bindSlider(a_slider: Slider, a_field: String): Void
	{
		var self: EffectEditor = this;
		a_slider.onChange = function(v: Number, final: Boolean): Void {
			self.focus(a_field);
			self.onSet(a_field, v);
		};
	}

	/* ---------------- public API ---------------- */

	public function get isOpen(): Boolean
	{
		return clip._visible;
	}

	public function setBounds(a_x: Number, a_y: Number, a_w: Number, a_h: Number): Void
	{
		clip._x = a_x;
		clip._y = a_y;
		_w = a_w;
		_h = a_h;
		layout();
	}

	public function setDevice(a_device: String): Void
	{
		_device = a_device;
		refreshGlyphs();
	}

	/* a_ed: state.editor or null. */
	public function setData(a_ed: Object): Void
	{
		var wasOpen: Boolean = clip._visible;
		var reopened: Boolean = a_ed != undefined && (!wasOpen || _ed == undefined || _ed.index != a_ed.index || _ed.id != a_ed.id);
		_ed = a_ed;
		if (a_ed == undefined || a_ed == null) {
			clip._visible = false;
			return;
		}
		clip._visible = true;

		var key: String = String(a_ed.hasMagnitude) + String(a_ed.hasDuration) + String(a_ed.hasArea) + String(a_ed.index >= 0);
		if (reopened || key != _visibleKey) {
			_visibleKey = key;
			layout();
		}
		if (reopened) {
			_btnFocus = 0;
			_focusId = firstRow();
		}
		if (!rowVisible(_focusId))
			_focusId = firstRow();
		render();
	}

	/* Routes a resolved action (editor context); true when consumed. */
	public function handleAction(a_action: String): Boolean
	{
		if (!isOpen)
			return false;
		switch (a_action) {
			case KeyMap.UP:
				return moveFocus(-1);
			case KeyMap.DOWN:
				return moveFocus(1);
			case KeyMap.STEP_DEC:
			case KeyMap.STEP_INC:
				return horizontal(a_action == KeyMap.STEP_INC ? 1 : -1, false);
			case KeyMap.BIG_DEC:
			case KeyMap.BIG_INC:
				return horizontal(a_action == KeyMap.BIG_INC ? 1 : -1, true);
			case KeyMap.CYCLE_RANGE:
				if (canCycle())
					onRange();
				return true;
			case KeyMap.REMOVE:
				if (_ed.index >= 0)
					onDelete();
				return true;
			case KeyMap.ACCEPT:
				if (_focusId == "buttons") {
					activateButton();
				} else {
					onOk();
				}
				return true;
			case KeyMap.CANCEL:
				onCancel();
				return true;
		}
		return false;
	}

	/* ---------------- focus ---------------- */

	public function canCycle(): Boolean
	{
		return _ed != undefined && _ed.canCycleRange == true;
	}

	private function rowVisible(a_id: String): Boolean
	{
		if (_ed == undefined)
			return false;
		switch (a_id) {
			case "range": return true;
			case "min":
			case "max": return _ed.hasMagnitude == true;
			case "duration": return _ed.hasDuration == true;
			case "area": return _ed.hasArea == true;
			case "buttons": return true;
		}
		return false;
	}

	private function visibleRows(): Array
	{
		var all: Array = ["range", "min", "max", "duration", "area", "buttons"];
		var out: Array = [];
		for (var i: Number = 0; i < all.length; i++) {
			if (rowVisible(all[i]))
				out.push(all[i]);
		}
		return out;
	}

	private function firstRow(): String
	{
		// Land on the first slider when there is one: range is one key away (F / Y).
		if (rowVisible("min"))
			return "min";
		if (rowVisible("duration"))
			return "duration";
		if (rowVisible("area"))
			return "area";
		return "range";
	}

	public function focus(a_id: String): Void
	{
		if (_focusId == a_id)
			return;
		_focusId = a_id;
		render();
	}

	private function moveFocus(a_delta: Number): Boolean
	{
		var rows: Array = visibleRows();
		var idx: Number = 0;
		for (var i: Number = 0; i < rows.length; i++) {
			if (rows[i] == _focusId)
				idx = i;
		}
		var next: Number = Math.max(0, Math.min(rows.length - 1, idx + a_delta));
		if (rows[next] == _focusId)
			return true;
		_focusId = rows[next];
		render();
		if (onFocusMove != undefined)
			onFocusMove();
		return true;
	}

	private function horizontal(a_dir: Number, a_big: Boolean): Boolean
	{
		if (_focusId == "range") {
			if (canCycle())
				onRange();
			return true;
		}
		if (_focusId == "buttons") {
			var btns: Array = buttonList();
			var nb: Number = Math.max(0, Math.min(btns.length - 1, _btnFocus + a_dir));
			if (nb != _btnFocus) {
				_btnFocus = nb;
				render();
				if (onFocusMove != undefined)
					onFocusMove();
			}
			return true;
		}
		onStep(_focusId, a_dir, a_big);
		return true;
	}

	private function buttonList(): Array
	{
		var out: Array = [_okBtn, _cancelBtn];
		if (_ed != undefined && _ed.index >= 0)
			out.push(_deleteBtn);
		return out;
	}

	private function activateButton(): Void
	{
		var btns: Array = buttonList();
		var b: Button = btns[_btnFocus];
		if (b == _okBtn)
			onOk();
		else if (b == _cancelBtn)
			onCancel();
		else if (b == _deleteBtn)
			onDelete();
	}

	/* ---------------- layout / paint ---------------- */

	private function layout(): Void
	{
		Draw.panel(_bg, _w, _h, Theme.MODAL_ALPHA, true);
		var pad: Number = 28;
		var labelW: Number = Math.min(200, Math.round(_w * 0.22));
		var rowH: Number = 50;
		var y: Number = pad;

		_titleTf._x = pad + 34;
		_titleTf._y = y - 2;
		_titleTf._width = _w - 2 * pad - 34 - 200;
		_schoolTf._x = _w - pad - 200;
		_schoolTf._y = y + 6;
		_schoolTf._width = 200;
		y += 52;
		Draw.divider(_bg, pad, y, _w - 2 * pad, 45);
		y += 18;

		_rangeRow._x = pad;
		_rangeRow._y = y;
		_rangeLabelTf._width = labelW - 8;
		_rangeLabelTf._y = Math.round((rowH - 30) / 2) - 2;
		y += rowH + 6;

		for (var i: Number = 0; i < FIELDS.length; i++) {
			var f: String = FIELDS[i];
			var s: Slider = _sliders[f];
			var vis: Boolean = rowVisible(f);
			s.clip._visible = vis;
			if (!vis)
				continue;
			s.clip._x = pad;
			s.clip._y = y;
			s.setSize(_w - 2 * pad, rowH, labelW);
			y += rowH + 6;
		}

		y += 10;
		var btnY: Number = _h - pad - 46;
		var pvH: Number = Math.max(70, Math.min(120, btnY - 16 - y));
		_previewBg.clear();
		_previewBg._x = pad;
		_previewBg._y = y;
		Draw.rect(_previewBg, 0, 0, _w - 2 * pad, pvH, 0xFFFFFF, 6);
		Draw.frame(_previewBg, 0, 0, _w - 2 * pad, pvH, 1, Theme.BORDER, 22);
		_lineTf._x = pad + 14;
		_lineTf._y = y + 8;
		_lineTf._width = _w - 2 * pad - 28;
		_lineTf._height = pvH - 40;
		_costTf._x = pad + 14;
		_costTf._y = y + pvH - 34;
		_costTf._width = _w - 2 * pad - 28;

		_deleteBtn.clip._visible = _ed != undefined && _ed.index >= 0;
		var btns: Array = buttonList();
		var bx: Number = _w - pad;
		for (var j: Number = btns.length - 1; j >= 0; j--) {
			var b: Button = btns[j];
			b.setMinWidth(150);
			bx -= b.width;
			b.clip._x = bx;
			b.clip._y = btnY;
			bx -= 12;
		}
		refreshGlyphs();
	}

	private function refreshGlyphs(): Void
	{
		var ctx: String = KeyMap.CTX_EDITOR;
		_okBtn.setGlyph(KeyMap.caption(KeyMap.ACCEPT, ctx, _device), _device);
		_cancelBtn.setGlyph(KeyMap.caption(KeyMap.CANCEL, ctx, _device), _device);
		_deleteBtn.setGlyph(KeyMap.caption(KeyMap.REMOVE, ctx, _device), _device);
		_rangeGlyph.setKey(KeyMap.caption(KeyMap.CYCLE_RANGE, ctx, _device), _device);
	}

	private function render(): Void
	{
		if (_ed == undefined)
			return;
		var color: Number = Theme.schoolColor(_ed.school);
		_orb.clear();
		Draw.circle(_orb, 28 + 13, 28 + 20, 13, color, 30);
		Draw.circle(_orb, 28 + 13, 28 + 20, 8, color, 100);
		Text.setFit(_titleTf, StateUtil.str(_ed.title));
		Text.set(_schoolTf, StateUtil.str(_ed.schoolName));
		Text.setColor(_schoolTf, color);

		// range row
		var labelW: Number = Math.min(200, Math.round(_w * 0.22));
		var boxW: Number = 240;
		var rowH: Number = 50;
		var focusedRange: Boolean = _focusId == "range";
		var rr: MovieClip = _rangeRow;
		rr.clear();
		if (focusedRange) {
			Draw.rect(rr, -10, 0, _w - 56 + 20, rowH, 0xFFFFFF, 9);
			Draw.rect(rr, -10, 0, 3, rowH, color, 100);
		}
		_rangeBox.clear();
		var can: Boolean = canCycle();
		Draw.rect(_rangeBox, labelW, 5, boxW, rowH - 10, 0x000000, can ? 60 : 30);
		Draw.frame(_rangeBox, labelW, 5, boxW, rowH - 10, 1, focusedRange ? color : Theme.BORDER, can ? 80 : 25);
		_rangeTf._x = labelW;
		_rangeTf._width = boxW;
		_rangeTf._y = Math.round((rowH - 30) / 2) - 2;
		Text.set(_rangeTf, StateUtil.str(_ed.rangeText));
		Text.setColor(_rangeTf, can ? Theme.TEXT : Theme.TEXT_SOFT);
		_rangeGlyph.clip._visible = can;
		_rangeGlyph.clip._x = labelW + boxW + 12;
		_rangeGlyph.clip._y = Math.round((rowH - 24) / 2);

		// sliders
		var unit: String = Translator.tr(StateUtil.str(_ed.unit));
		setSlider("min", 1, StateUtil.num(_ed.magCap, 100), StateUtil.num(_ed.min, 1), unit, color);
		setSlider("max", 1, StateUtil.num(_ed.magCap, 100), StateUtil.num(_ed.max, 1), unit, color);
		setSlider("duration", 1, StateUtil.num(_ed.durCap, 1440), StateUtil.num(_ed.duration, 1),
			Translator.tr(StateUtil.num(_ed.duration, 1) == 1 ? "$LA_Unit_sec" : "$LA_Unit_secs"), color);
		setSlider("area", 0, StateUtil.num(_ed.areaCap, 50), StateUtil.num(_ed.area, 0), Translator.tr("$LA_Unit_ft"), color);

		// preview
		_lineTf.text = Translator.tr(StateUtil.str(_ed.lineText));
		_costTf.text = Translator.tr("$LA_UI_EffectCost") + " " + String(StateUtil.num(_ed.effectCost, 0));

		// buttons
		var btns: Array = buttonList();
		if (_btnFocus >= btns.length)
			_btnFocus = btns.length - 1;
		for (var i: Number = 0; i < btns.length; i++) {
			var b: Button = btns[i];
			b.setFocused(_focusId == "buttons" && i == _btnFocus);
			b.setAccent(color);
		}
		if (!_deleteBtn.clip._visible)
			_deleteBtn.setFocused(false);
	}

	private function setSlider(a_field: String, a_min: Number, a_max: Number, a_value: Number, a_suffix: String, a_color: Number): Void
	{
		var s: Slider = _sliders[a_field];
		if (!s.clip._visible)
			return;
		s.setColor(a_color);
		s.setRange(a_min, a_max);
		s.setSuffix(a_suffix);
		s.setValue(a_value);
		s.setFocused(_focusId == a_field);
	}
}
