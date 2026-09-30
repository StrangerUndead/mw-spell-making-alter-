/*
 * Modal message box for LA_ShowMessage (Morrowind's messages, already built and translated by
 * the DLL). Messages queue; OK / Enter / A / Esc / B dismisses one.
 */
import lostart.Theme;
import lostart.components.Button;
import lostart.input.KeyMap;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.MessageBox
{
	public var clip: MovieClip;
	public var onClosed: Function;   // fn() after the last queued message is dismissed

	private var _shade: MovieClip;
	private var _bg: MovieClip;
	private var _tf: TextField;
	private var _ok: Button;
	private var _queue: Array;
	private var _frameW: Number = 1920;
	private var _frameH: Number = 1080;

	public function MessageBox(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		var self: MessageBox = this;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_shade = clip.createEmptyMovieClip("shade", 1);
		_bg = clip.createEmptyMovieClip("bg", 2);
		_tf = Text.multiline(Text.create(clip, "text", 3, 0, 0, 600, 100, Theme.FS_HEADER, Theme.TEXT, Theme.FONT_REGULAR, "center"));
		_ok = new Button(clip, "ok", 4, 44);
		_ok.setLabel("$LA_UI_OK");
		_ok.onPress = function(): Void { self.dismiss(); };
		_shade.onRelease = function(): Void {};
		_shade.useHandCursor = false;
		_queue = [];
		clip._visible = false;
	}

	public function get isOpen(): Boolean
	{
		return clip._visible;
	}

	public function setFrame(a_w: Number, a_h: Number): Void
	{
		_frameW = a_w;
		_frameH = a_h;
		if (isOpen)
			layout();
	}

	public function setDevice(a_device: String): Void
	{
		_ok.setGlyph(KeyMap.caption(KeyMap.ACCEPT, KeyMap.CTX_MESSAGE, a_device), a_device);
		if (isOpen)
			layout();
	}

	public function show(a_text: String): Void
	{
		_queue.push(Translator.tr(a_text));
		if (!isOpen)
			next();
	}

	public function dismiss(): Void
	{
		if (_queue.length > 0) {
			next();
			return;
		}
		clip._visible = false;
		if (onClosed != undefined)
			onClosed();
	}

	public function handleAction(a_action: String): Boolean
	{
		if (!isOpen)
			return false;
		if (a_action == KeyMap.ACCEPT || a_action == KeyMap.CANCEL)
			dismiss();
		return true;
	}

	private function next(): Void
	{
		var t: String = String(_queue.shift());
		_tf.text = t;
		clip._visible = true;
		layout();
	}

	private function layout(): Void
	{
		_shade.clear();
		Draw.rect(_shade, 0, 0, _frameW, _frameH, 0x000000, 50);
		var w: Number = Math.min(760, _frameW - 200);
		var pad: Number = 32;
		_tf._width = w - 2 * pad;
		_tf._height = 400;
		var th: Number = Math.max(30, _tf.textHeight + 8);
		_tf._height = th;
		var h: Number = pad + th + 24 + 44 + pad;
		var x: Number = Math.round((_frameW - w) / 2);
		var y: Number = Math.round((_frameH - h) / 2);
		_bg.clear();
		_bg._x = x;
		_bg._y = y;
		Draw.modalPanel(_bg, w, h);
		_tf._x = x + pad;
		_tf._y = y + pad;
		_ok.setMinWidth(160);
		_ok.clip._x = x + Math.round((w - _ok.width) / 2);
		_ok.clip._y = y + h - pad - 44;
	}
}
