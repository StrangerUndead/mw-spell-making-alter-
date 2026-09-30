/*
 * Labelled horizontal slider for the effect editor (label | track | value).
 * Mouse: drag the thumb or press the track. While dragging, onChange(value, false) fires at
 * most once per frame; on release onChange(value, true). The DLL clamps and answers with a new
 * state, which the menu feeds back through setValue.
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.Slider
{
	public var clip: MovieClip;
	public var onChange: Function;   // fn(value, final)

	private var _labelTf: TextField;
	private var _valueTf: TextField;
	private var _track: MovieClip;
	private var _thumb: MovieClip;
	private var _focusMc: MovieClip;
	private var _w: Number = 400;
	private var _h: Number = 44;
	private var _labelW: Number = 170;
	private var _valueW: Number = 150;
	private var _min: Number = 0;
	private var _max: Number = 100;
	private var _value: Number = 0;
	private var _suffix: String = "";
	private var _focused: Boolean = false;
	private var _dragging: Boolean = false;
	private var _pending: Number;
	private var _color: Number;

	public function Slider(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		var self: Slider = this;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_focusMc = clip.createEmptyMovieClip("focus", 1);
		_labelTf = Text.label(clip, "label", 2, 100, "left");
		_track = clip.createEmptyMovieClip("track", 3);
		_thumb = clip.createEmptyMovieClip("thumb", 4);
		_valueTf = Text.create(clip, "value", 5, 0, 0, 100, 30, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "right");
		_color = Theme.ACCENT;

		_track.onPress = function(): Void { self.beginDrag(); };
		_thumb.onPress = function(): Void { self.beginDrag(); };
		_track.onRelease = _track.onReleaseOutside = _thumb.onRelease = _thumb.onReleaseOutside = function(): Void {
			self.endDrag();
		};
		_track.useHandCursor = false;
		_thumb.useHandCursor = false;
	}

	public function setSize(a_w: Number, a_h: Number, a_labelW: Number): Void
	{
		_w = a_w;
		_h = a_h;
		if (a_labelW != undefined)
			_labelW = a_labelW;
		redraw();
	}

	public function setLabel(a_key: String): Void
	{
		Text.setCaps(_labelTf, a_key);
	}

	public function setColor(a_color: Number): Void
	{
		_color = a_color;
		redraw();
	}

	/* Suffix appended to the number ("pts", "secs", "ft"), already translated. */
	public function setSuffix(a_suffix: String): Void
	{
		_suffix = a_suffix == undefined ? "" : a_suffix;
		redraw();
	}

	public function setRange(a_min: Number, a_max: Number): Void
	{
		_min = a_min;
		_max = Math.max(a_min, a_max);
		redraw();
	}

	public function setValue(a_value: Number): Void
	{
		if (_dragging)
			return;   // the player's hand wins until release
		_value = a_value;
		redraw();
	}

	public function get value(): Number
	{
		return _value;
	}

	public function setFocused(a_focused: Boolean): Void
	{
		_focused = a_focused;
		redraw();
	}

	private function trackX(): Number
	{
		return _labelW;
	}

	private function trackW(): Number
	{
		return Math.max(40, _w - _labelW - _valueW - 16);
	}

	private function beginDrag(): Void
	{
		var self: Slider = this;
		_dragging = true;
		updateFromMouse();
		clip.onEnterFrame = function(): Void {
			self.updateFromMouse();
			self.flush(false);
		};
	}

	private function endDrag(): Void
	{
		if (!_dragging)
			return;
		updateFromMouse();
		_dragging = false;
		delete clip.onEnterFrame;
		_pending = _value;
		flush(true);
	}

	private function updateFromMouse(): Void
	{
		var t: Number = (clip._xmouse - trackX()) / trackW();
		t = Math.max(0, Math.min(1, t));
		var v: Number = Math.round(_min + t * (_max - _min));
		if (v != _value) {
			_value = v;
			_pending = v;
			redraw();
		}
	}

	private function flush(a_final: Boolean): Void
	{
		if (_pending == undefined)
			return;
		var v: Number = _pending;
		_pending = undefined;
		if (onChange != undefined)
			onChange(v, a_final);
	}

	private function redraw(): Void
	{
		_focusMc.clear();
		_track.clear();
		_thumb.clear();
		if (_focused)
			Draw.selectBar(_focusMc, -14, 0, _w + 28, _h, true);
		_labelTf._x = 0;
		_labelTf._width = _labelW - 8;
		_labelTf._height = _h;
		_labelTf._y = Math.round((_h - _labelTf.textHeight) / 2) - 1;
		Text.setColor(_labelTf, _focused ? Theme.TEXT : Theme.TEXT_HINT);

		var tx: Number = trackX();
		var tw: Number = trackW();
		var cy: Number = Math.round(_h / 2);
		var range: Number = _max - _min;
		var t: Number = range > 0 ? (_value - _min) / range : 0;
		t = Math.max(0, Math.min(1, t));
		// Skyrim's settings slider: a hairline track, the filled part brighter, a white notch.
		Draw.rect(_track, tx, 0, tw, _h, 0, 0);   // hit area
		Draw.rect(_track, tx, cy - 1, tw, 2, 0xFFFFFF, 22);
		Draw.rect(_track, tx, cy - 1, tw * t, 2, 0xFFFFFF, _focused ? 85 : 60);
		Draw.rect(_track, tx, cy - 6, 1, 12, 0xFFFFFF, 35);
		Draw.rect(_track, tx + tw - 1, cy - 6, 1, 12, 0xFFFFFF, 35);
		var thx: Number = tx + tw * t;
		Draw.rect(_thumb, thx - 3, cy - 11, 6, 22, 0x000000, 70);
		Draw.rect(_thumb, thx - 2, cy - 10, 4, 20, 0xFFFFFF, _focused ? 100 : 75);

		_valueTf._x = _w - _valueW;
		_valueTf._width = _valueW;
		_valueTf._height = _h;
		_valueTf.text = String(_value) + (_suffix.length > 0 ? " " + _suffix : "");
		_valueTf._y = Math.round((_h - _valueTf.textHeight) / 2) - 3;
	}
}
