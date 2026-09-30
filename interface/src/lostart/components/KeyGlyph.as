/*
 * Procedural button art: a keyboard keycap ("R", "Enter", "F1") or an Xbox-layout gamepad
 * button (coloured A/B/X/Y discs, grey pills for LB/RB/LT/RT/LS/RS/Back/Start). Replaces the
 * bitmap button art SkyUI loads, since this movie ships no assets.
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.KeyGlyph
{
	public var clip: MovieClip;
	private var _tf: TextField;
	private var _caption: String;
	private var _device: String;
	private var _size: Number;

	public function KeyGlyph(a_parent: MovieClip, a_name: String, a_depth: Number, a_size: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_size = a_size == undefined ? 26 : a_size;
		_tf = Text.create(clip, "cap", 2, 0, 0, 10, _size, Math.round(_size * 0.62), Theme.TEXT, Theme.FONT_BOLD, "center");
	}

	public function get width(): Number
	{
		return clip._visible ? clip._width : 0;
	}

	public function setKey(a_caption: String, a_device: String): Void
	{
		if (a_caption == _caption && a_device == _device)
			return;
		_caption = a_caption;
		_device = a_device;
		redraw();
	}

	private function redraw(): Void
	{
		var bg: MovieClip = clip;
		bg.clear();
		if (_caption == undefined || _caption == "") {
			clip._visible = false;
			return;
		}
		clip._visible = true;
		var s: Number = _size;
		_tf.text = _caption;
		var textW: Number = _tf.textWidth + 4;

		var isFace: Boolean = _device == "pad" && (_caption == "A" || _caption == "B" || _caption == "X" || _caption == "Y");
		var w: Number;
		if (isFace) {
			w = s;
			var c: Number = _caption == "A" ? Theme.PAD_A : (_caption == "B" ? Theme.PAD_B : (_caption == "X" ? Theme.PAD_X : Theme.PAD_Y));
			Draw.circle(bg, s / 2, s / 2, s / 2, 0x1A1A1A, 90);
			Draw.circle(bg, s / 2, s / 2, s / 2 - 2, c, 100);
			Text.setColor(_tf, 0xFFFFFF);
		} else if (_device == "pad") {
			w = Math.max(s * 1.4, textW + 12);
			Draw.roundRect(bg, 0, 0, w, s, s / 2, 0x3C3C3C, 95);
			Draw.roundFrame(bg, 0, 0, w, s, s / 2, 1, 0xFFFFFF, 40);
			Text.setColor(_tf, Theme.TEXT);
		} else {
			w = Math.max(s, textW + 10);
			Draw.roundRect(bg, 0, 0, w, s, 4, 0x000000, 40);
			Draw.roundFrame(bg, 0, 0, w, s, 4, 1, 0xFFFFFF, 70);
			Text.setColor(_tf, Theme.TEXT);
		}
		_tf._x = 0;
		_tf._width = w;
		_tf._height = s;
		_tf._y = Math.round((s - _tf.textHeight) / 2) - 2;
	}
}
