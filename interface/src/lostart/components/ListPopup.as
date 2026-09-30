/*
 * Modal list popup, used for the attribute/skill picker (state.picker) and the Load list
 * (LA_SetLoadList). Rows are one line (picker) or two (load: spell name + its effect summary).
 */
import lostart.Theme;
import lostart.components.Button;
import lostart.components.VirtualList;
import lostart.input.KeyMap;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;

class lostart.components.ListPopup
{
	public var clip: MovieClip;
	public var onPick: Function;     // fn(entry, index)
	public var onCancel: Function;   // fn()
	public var onFocusMove: Function;

	private var _shade: MovieClip;
	private var _bg: MovieClip;
	private var _titleTf: TextField;
	private var _list: VirtualList;
	private var _okBtn: Button;
	private var _cancelBtn: Button;
	private var _twoLine: Boolean;
	private var _w: Number = 700;
	private var _h: Number = 600;
	private var _device: String = "kbm";
	private var _accent: Number;

	public function ListPopup(a_parent: MovieClip, a_name: String, a_depth: Number, a_twoLine: Boolean)
	{
		var self: ListPopup = this;
		_twoLine = a_twoLine == true;
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_shade = clip.createEmptyMovieClip("shade", 1);
		_bg = clip.createEmptyMovieClip("bg", 2);
		_titleTf = Text.create(clip, "title", 3, 0, 0, 100, 40, Theme.FS_TITLE, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		_list = new VirtualList(clip, "list", 4, _twoLine ? Theme.ROW_H_EFFECT : Theme.ROW_H + 4);
		_list.createRow = function(mc: MovieClip, w: Number, h: Number): Object { return self.createRow(mc, w, h); };
		_list.renderRow = function(row: Object, e: Object, i: Number, sel: Boolean, foc: Boolean): Void { self.renderRow(row, e, i, sel, foc); };
		_list.onItemPress = function(i: Number): Void { self.pick(); };
		_list.onSelectionChange = function(i: Number, byMouse: Boolean): Void {
			if (self.onFocusMove != undefined)
				self.onFocusMove();
		};
		_list.setFocused(true);
		_okBtn = new Button(clip, "ok", 5, 44);
		_cancelBtn = new Button(clip, "cancel", 6, 44);
		_okBtn.setLabel("$LA_UI_OK");
		_cancelBtn.setLabel("$LA_UI_Cancel");
		_okBtn.onPress = function(): Void { self.pick(); };
		_cancelBtn.onPress = function(): Void { self.onCancel(); };
		_shade.onRelease = function(): Void {};   // swallow clicks behind the modal
		_shade.useHandCursor = false;
		clip._visible = false;
	}

	public function get isOpen(): Boolean
	{
		return clip._visible;
	}

	public function get list(): VirtualList
	{
		return _list;
	}

	/* a_frameW/H: full design frame (for the shade); popup is centred in it. */
	public function setBounds(a_frameW: Number, a_frameH: Number, a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		_shade.clear();
		Draw.rect(_shade, 0, 0, a_frameW, a_frameH, 0x000000, 45);
		var x: Number = Math.round((a_frameW - a_w) / 2);
		var y: Number = Math.round((a_frameH - a_h) / 2);
		_bg._x = x;
		_bg._y = y;
		Draw.panel(_bg, a_w, a_h, Theme.MODAL_ALPHA, true);
		var pad: Number = 26;
		_titleTf._x = x + pad;
		_titleTf._y = y + pad - 4;
		_titleTf._width = a_w - 2 * pad;
		Draw.divider(_bg, pad, pad + 46, a_w - 2 * pad, 45);
		_list.clip._x = x + pad;
		_list.clip._y = y + pad + 58;
		_list.setSize(a_w - 2 * pad, a_h - 2 * pad - 58 - 60);
		_okBtn.setMinWidth(150);
		_cancelBtn.setMinWidth(150);
		_cancelBtn.clip._x = x + a_w - pad - _cancelBtn.width;
		_cancelBtn.clip._y = y + a_h - pad - 44;
		_okBtn.clip._x = _cancelBtn.clip._x - 12 - _okBtn.width;
		_okBtn.clip._y = _cancelBtn.clip._y;
	}

	public function setDevice(a_device: String): Void
	{
		_device = a_device;
		_okBtn.setGlyph(KeyMap.caption(KeyMap.ACCEPT, KeyMap.CTX_POPUP, a_device), a_device);
		_cancelBtn.setGlyph(KeyMap.caption(KeyMap.CANCEL, KeyMap.CTX_POPUP, a_device), a_device);
	}

	public function open(a_title: String, a_entries: Array, a_emptyKey: String, a_accent: Number): Void
	{
		_accent = a_accent;
		Text.setFit(_titleTf, Translator.tr(a_title));
		var wasOpen: Boolean = clip._visible;
		clip._visible = true;
		_list.emptyText = a_emptyKey == undefined ? "" : a_emptyKey;
		_list.setData(a_entries);
		if (!wasOpen)
			_list.select(0, false, true);
		_okBtn.enabled = a_entries != undefined && a_entries.length > 0;
		setDevice(_device);
	}

	public function close(): Void
	{
		clip._visible = false;
	}

	public function handleAction(a_action: String): Boolean
	{
		if (!isOpen)
			return false;
		switch (a_action) {
			case KeyMap.UP: _list.move(-1); return true;
			case KeyMap.DOWN: _list.move(1); return true;
			case KeyMap.PAGE_UP: case KeyMap.LEFT: _list.page(-1); return true;
			case KeyMap.PAGE_DOWN: case KeyMap.RIGHT: _list.page(1); return true;
			case KeyMap.HOME: _list.home(); return true;
			case KeyMap.END: _list.end(); return true;
			case KeyMap.ACCEPT: pick(); return true;
			case KeyMap.CANCEL: onCancel(); return true;
		}
		return true;   // modal: swallow everything else
	}

	public function handleWheel(a_delta: Number): Void
	{
		_list.scroll(a_delta > 0 ? -1 : 1);
	}

	private function pick(): Void
	{
		var i: Number = _list.selectedIndex;
		if (i < 0)
			return;
		if (onPick != undefined)
			onPick(_list.selectedEntry, i);
	}

	/* ---------------- rows ---------------- */

	private function createRow(a_mc: MovieClip, a_w: Number, a_h: Number): Object
	{
		var bg: MovieClip = a_mc.createEmptyMovieClip("bg", 1);
		var main: TextField = Text.create(a_mc, "main", 2, 14, 4, a_w - 28, 32, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		var sub: TextField = Text.create(a_mc, "sub", 3, 14, 30, a_w - 28, 26, Theme.FS_HINT, Theme.TEXT_SOFT, Theme.FONT_REGULAR, "left");
		var row: Object = {bg: bg, main: main, sub: sub};
		row.resize = function(w: Number, h: Number): Void {
			main._width = w - 28;
			sub._width = w - 28;
		};
		return row;
	}

	private function renderRow(a_row: Object, a_entry: Object, a_index: Number, a_sel: Boolean, a_focused: Boolean): Void
	{
		var bg: MovieClip = a_row.bg;
		var main: TextField = a_row.main;
		var sub: TextField = a_row.sub;
		bg.clear();
		var h: Number = a_row.height;
		if (a_sel) {
			Draw.rect(bg, 0, 1, a_row.width, h - 2, Theme.ROW_SELECT, Theme.ROW_SELECT_FOCUS_ALPHA);
			Draw.rect(bg, 0, 1, 3, h - 2, _accent == undefined ? Theme.ACCENT : _accent, 100);
		}
		if (_twoLine) {
			Text.setFit(main, a_entry.name == undefined ? String(a_entry.text) : String(a_entry.name));
			sub._visible = true;
			Text.setFit(sub, a_entry.name == undefined ? "" : String(a_entry.text));
			main._y = 3;
		} else {
			sub._visible = false;
			Text.setFit(main, String(a_entry.text));
			main._y = Math.round((h - main.textHeight) / 2) - 3;
		}
		Text.setColor(main, a_sel ? Theme.TEXT : Theme.TEXT_SOFT);
	}
}
