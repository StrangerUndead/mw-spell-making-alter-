/*
 * Text button with key/button art (like SkyUI's ButtonPanel entries: glyph + label).
 * Mouse: hover highlight, click fires onPress. Keyboard/gamepad activation is the menu's job;
 * it only calls setFocused for the visual state.
 */
import lostart.Theme;
import lostart.components.KeyGlyph;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.Button
{
	public var clip: MovieClip;
	public var onPress: Function;

	private var _bg: MovieClip;
	private var _tf: TextField;
	private var _glyph: KeyGlyph;
	private var _label: String = "";
	private var _h: Number;
	private var _w: Number = 0;
	private var _minW: Number = 0;
	private var _enabled: Boolean = true;
	private var _hover: Boolean = false;
	private var _focused: Boolean = false;
	private var _accent: Number;

	public function Button(a_parent: MovieClip, a_name: String, a_depth: Number, a_h: Number)
	{
		var self: Button = this;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_h = a_h == undefined ? 44 : a_h;
		_glyph = new KeyGlyph(clip, "glyph", 2, Math.round(_h * 0.6));
		_tf = Text.create(clip, "label", 3, 0, 0, 200, _h, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		_bg.onRollOver = function(): Void { self.setHover(true); };
		_bg.onRollOut = _bg.onDragOut = function(): Void { self.setHover(false); };
		_bg.onRelease = function(): Void {
			if (self.enabled && self.onPress != undefined)
				self.onPress();
		};
		_bg.useHandCursor = false;
	}

	public function setLabel(a_key: String): Void
	{
		if (a_key == _label)
			return;
		_label = a_key;
		redraw();
	}

	public function setGlyph(a_caption: String, a_device: String): Void
	{
		_glyph.setKey(a_caption, a_device);
		redraw();
	}

	public function setMinWidth(a_w: Number): Void
	{
		_minW = a_w;
		redraw();
	}

	public function setAccent(a_color: Number): Void
	{
		_accent = a_color;
		redraw();
	}

	public function set enabled(a_value: Boolean): Void
	{
		if (_enabled == a_value)
			return;
		_enabled = a_value;
		redraw();
	}

	public function get enabled(): Boolean
	{
		return _enabled;
	}

	public function setFocused(a_value: Boolean): Void
	{
		if (_focused == a_value)
			return;
		_focused = a_value;
		redraw();
	}

	public function setHover(a_value: Boolean): Void
	{
		_hover = a_value;
		redraw();
	}

	public function get width(): Number
	{
		return _w;
	}

	public function get height(): Number
	{
		return _h;
	}

	private function redraw(): Void
	{
		Text.set(_tf, _label);
		var pad: Number = 14;
		var gw: Number = _glyph.width;
		var tw: Number = _tf.textWidth + 6;
		var w: Number = Math.max(_minW, pad + (gw > 0 ? gw + 8 : 0) + tw + pad);
		_w = Math.round(w);
		var inner: Number = (gw > 0 ? gw + 8 : 0) + tw;
		var x0: Number = Math.round((_w - inner) / 2);
		_glyph.clip._x = x0;
		_glyph.clip._y = Math.round((_h - _glyph.clip._height) / 2);
		_tf._x = x0 + (gw > 0 ? gw + 8 : 0);
		_tf._width = tw + 4;
		_tf._y = Math.round((_h - _tf.textHeight) / 2) - 2;

		_bg.clear();
		var hot: Boolean = _enabled && (_hover || _focused);
		Draw.rect(_bg, 0, 0, _w, _h, 0x000000, hot ? 70 : 45);
		Draw.frame(_bg, 0, 0, _w, _h, 1, _accent != undefined && hot ? _accent : Theme.BORDER, hot ? 75 : 30);
		if (hot)
			Draw.rect(_bg, 1, _h - 3, _w - 2, 2, _accent != undefined ? _accent : Theme.ACCENT, 90);
		clip._alpha = _enabled ? 100 : 45;
		Text.setColor(_tf, _enabled ? Theme.TEXT : Theme.TEXT_DIM);
	}
}
