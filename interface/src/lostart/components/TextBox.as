/*
 * Single-line text entry used by the spell-name field and the search box.
 *
 * Skyrim text-input behaviour (as SkyUI's SearchWidget and the vanilla crafting rename):
 * the field is a dynamic TextField until editing starts; then it becomes an input field,
 * takes focus and skse.AllowTextInput(true) stops the game from treating typed letters as
 * controls. Ending the edit restores everything and calls AllowTextInput(false). The calls are
 * kept balanced across all TextBoxes (SKSE keeps a counter).
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.TextBox
{
	public var clip: MovieClip;
	public var onChange: Function;   // fn(text) while typing
	public var onStart: Function;    // fn()
	public var onEnd: Function;      // fn(text, accepted:Boolean)

	private var _bg: MovieClip;
	private var _hit: MovieClip;
	private var _icon: MovieClip;
	private var _tf: TextField;
	private var _placeholderTf: TextField;
	private var _placeholder: String = "";
	private var _text: String = "";
	private var _active: Boolean = false;
	private var _swallow: String;
	private var _startTime: Number = 0;
	private var _maxChars: Number;
	private var _w: Number = 200;
	private var _h: Number = 40;
	private var _hasIcon: Boolean;
	private var _focusedLook: Boolean = false;

	private static var _textInputHolder: TextBox;

	public function TextBox(a_parent: MovieClip, a_name: String, a_depth: Number, a_fontSize: Number, a_maxChars: Number, a_searchIcon: Boolean)
	{
		var self: TextBox = this;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_bg = clip.createEmptyMovieClip("bg", 1);
		_icon = clip.createEmptyMovieClip("icon", 2);
		_placeholderTf = Text.create(clip, "ph", 3, 0, 0, 100, 30, a_fontSize, Theme.TEXT_HINT, Theme.FONT_REGULAR, "left");
		_tf = Text.create(clip, "tf", 4, 0, 0, 100, 30, a_fontSize, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		_hit = clip.createEmptyMovieClip("hit", 5);
		_maxChars = a_maxChars;
		_hasIcon = a_searchIcon == true;
		_hit.onRelease = function(): Void { self.start(undefined); };
		_hit.useHandCursor = false;
		_tf.onChanged = function(): Void { self.changed(); };
		_tf.onKillFocus = function(): Void { self.end(false); };   // clicking away keeps the text, never "accepts"
	}

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		redraw();
	}

	public function setPlaceholder(a_key: String): Void
	{
		_placeholder = a_key;
		redraw();
	}

	public function setFocusedLook(a_value: Boolean): Void
	{
		_focusedLook = a_value;
		redraw();
	}

	public function get active(): Boolean
	{
		return _active;
	}

	public function get text(): String
	{
		return _text;
	}

	/* Programmatic update (state from the DLL); ignored while the player is typing. */
	public function setText(a_text: String): Void
	{
		if (_active)
			return;
		_text = a_text == undefined ? "" : a_text;
		_tf.text = _text;
		redraw();
	}

	/*
	 * Begins editing. a_swallow: the character of the hotkey that opened the field ('/', 't'),
	 * removed if the key's own character lands in the field.
	 */
	public function start(a_swallow: String): Void
	{
		if (_active)
			return;
		_active = true;
		_swallow = a_swallow;
		_startTime = getTimer();
		_tf.type = "input";
		_tf.selectable = true;
		if (_maxChars != undefined)
			_tf.maxChars = _maxChars;
		_tf.text = _text;
		Selection.setFocus(_tf);
		Selection.setSelection(_text.length, _text.length);
		if (_textInputHolder == undefined)
			skse.AllowTextInput(true);
		_textInputHolder = this;
		redraw();
		if (onStart != undefined)
			onStart();
	}

	public function end(a_accepted: Boolean): Void
	{
		if (!_active)
			return;
		_active = false;
		_text = _tf.text;
		_tf.type = "dynamic";
		_tf.selectable = false;
		Selection.setFocus(null);   // _active is already false, so onKillFocus is a no-op
		if (_textInputHolder == this) {
			_textInputHolder = undefined;
			skse.AllowTextInput(false);
		}
		redraw();
		if (onEnd != undefined)
			onEnd(_text, a_accepted);
	}

	/* Clears the text (search: Esc). */
	public function clear(): Void
	{
		_tf.text = "";
		_text = "";
		redraw();
		if (onChange != undefined)
			onChange("");
	}

	private function changed(): Void
	{
		var s: String = _tf.text;
		if (_swallow != undefined && getTimer() - _startTime < 250) {
			var c: String = s.charAt(s.length - 1).toLowerCase();
			if (s.length > 0 && c == _swallow.toLowerCase() && s.length == _text.length + 1) {
				s = s.substr(0, s.length - 1);
				_tf.text = s;
			}
		}
		_swallow = undefined;
		_text = s;
		redraw();
		if (onChange != undefined)
			onChange(s);
	}

	private function redraw(): Void
	{
		_bg.clear();
		_icon.clear();
		var focus: Boolean = _active || _focusedLook;
		Draw.rect(_bg, 0, 0, _w, _h, 0x000000, _active ? 75 : 50);
		Draw.frame(_bg, 0, 0, _w, _h, 1, Theme.BORDER, focus ? 70 : 28);
		if (_active)
			Draw.rect(_bg, 1, _h - 3, _w - 2, 2, Theme.ACCENT, 90);
		var x0: Number = 10;
		if (_hasIcon) {
			Draw.magnifier(_icon, _h / 2, _h / 2, _h * 0.55, Theme.TEXT_SOFT, 80);
			x0 = _h;
		}
		_tf._x = x0;
		_tf._width = _w - x0 - 8;
		_tf._height = _h;
		_tf._y = Math.round((_h - Math.max(_tf.textHeight, 20)) / 2) - 3;
		_placeholderTf._x = x0;
		_placeholderTf._width = _w - x0 - 8;
		_placeholderTf._height = _h;
		Text.set(_placeholderTf, _placeholder);
		_placeholderTf._y = Math.round((_h - _placeholderTf.textHeight) / 2) - 3;
		_placeholderTf._visible = !_active && (_text == undefined || _text.length == 0);
		_hit.clear();
		_hit._visible = !_active;
		Draw.rect(_hit, 0, 0, _w, _h, 0, 0);
	}
}
