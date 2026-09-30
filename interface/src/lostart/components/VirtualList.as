/*
 * Virtualized fixed-row-height list (the pattern of SkyUI's skyui.components.list.BasicList /
 * ScrollingList: a small pool of row clips is recycled while the data array scrolls under
 * it). Only visible rows exist, so 200+ known effects cost the same as 15.
 *
 * The owner supplies:
 *   createRow(rowMc, width, height): Object      builds a row's content, returns a row object
 *   renderRow(row, entry, index, selected, focused)  paints a row for a data entry
 * and listens through onSelectionChange(index, byMouse) / onItemPress(index).
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.VirtualList
{
	public var clip: MovieClip;
	public var createRow: Function;
	public var renderRow: Function;
	public var onSelectionChange: Function;
	public var onItemPress: Function;
	public var hoverSelects: Boolean = true;

	private var _rowsMc: MovieClip;
	private var _barMc: MovieClip;
	private var _thumbMc: MovieClip;
	private var _emptyTf: TextField;
	private var _rows: Array;
	private var _data: Array;
	private var _top: Number = 0;
	private var _sel: Number = -1;
	private var _w: Number = 0;
	private var _h: Number = 0;
	private var _rowH: Number;
	private var _count: Number = 0;
	private var _focused: Boolean = false;
	private var _emptyText: String = "";

	private static var BAR_W: Number = 6;
	private static var BAR_GAP: Number = 8;

	public function VirtualList(a_parent: MovieClip, a_name: String, a_depth: Number, a_rowH: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_rowsMc = clip.createEmptyMovieClip("rows", 1);
		_barMc = clip.createEmptyMovieClip("bar", 2);
		_thumbMc = clip.createEmptyMovieClip("thumb", 3);
		_emptyTf = Text.create(clip, "empty", 4, 0, 0, 100, 40, Theme.FS_SMALL, Theme.TEXT_HINT, Theme.FONT_REGULAR, "center");
		Text.multiline(_emptyTf);
		_rowH = a_rowH;
		_rows = [];
		_data = [];
		initThumbDrag();
	}

	/* ---------------- geometry ---------------- */

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		var n: Number = Math.max(1, Math.floor(a_h / _rowH));
		if (n != _count || _rows.length == 0) {
			for (var i: Number = 0; i < _rows.length; i++)
				_rows[i].mc.removeMovieClip();
			_rows = [];
			_count = n;
			for (var j: Number = 0; j < n; j++)
				_rows.push(makeRow(j));
		} else {
			for (var k: Number = 0; k < _rows.length; k++)
				sizeRow(_rows[k]);
		}
		_emptyTf._x = 10;
		_emptyTf._y = 24;
		_emptyTf._width = Math.max(10, rowWidth() - 20);
		_emptyTf._height = 80;
		clampTop();
		refresh();
	}

	public function get rowHeight(): Number
	{
		return _rowH;
	}

	public function get visibleCount(): Number
	{
		return _count;
	}

	private function rowWidth(): Number
	{
		return _w - BAR_W - BAR_GAP;
	}

	private function makeRow(a_i: Number): Object
	{
		var self: VirtualList = this;
		var mc: MovieClip = _rowsMc.createEmptyMovieClip("row" + a_i, a_i + 1);
		var hit: MovieClip = mc.createEmptyMovieClip("hit", 1);
		var content: MovieClip = mc.createEmptyMovieClip("content", 2);
		var row: Object = createRow == undefined ? {} : createRow(content, rowWidth(), _rowH);
		row.mc = mc;
		row.hit = hit;
		row.content = content;
		row.slot = a_i;
		hit.onRollOver = function(): Void {
			if (self.hoverSelects)
				self.selectSlot(a_i, true);
		};
		hit.onPress = function(): Void {
			self.pressSlot(a_i);
		};
		hit.useHandCursor = false;
		sizeRow(row);
		return row;
	}

	private function sizeRow(a_row: Object): Void
	{
		var mc: MovieClip = a_row.mc;
		mc._x = 0;
		mc._y = a_row.slot * _rowH;
		var hit: MovieClip = a_row.hit;
		hit.clear();
		Draw.rect(hit, 0, 0, rowWidth(), _rowH, 0x000000, 0);   // invisible hit area
		a_row.width = rowWidth();
		a_row.height = _rowH;
		if (a_row.resize != undefined)
			a_row.resize(rowWidth(), _rowH);
	}

	/* ---------------- data ---------------- */

	public function setData(a_data: Array): Void
	{
		_data = a_data == undefined ? [] : a_data;
		if (_sel >= _data.length)
			_sel = _data.length - 1;
		if (_sel < 0 && _data.length > 0)
			_sel = 0;
		clampTop();
		ensureVisible(_sel);
		refresh();
	}

	public function get length(): Number
	{
		return _data.length;
	}

	public function get data(): Array
	{
		return _data;
	}

	public function get selectedIndex(): Number
	{
		return _sel;
	}

	public function get selectedEntry(): Object
	{
		return _sel >= 0 ? _data[_sel] : undefined;
	}

	public function set emptyText(a_text: String): Void
	{
		_emptyText = a_text;
		refresh();
	}

	public function setFocused(a_focused: Boolean): Void
	{
		if (_focused == a_focused)
			return;
		_focused = a_focused;
		refresh();
	}

	public function get focused(): Boolean
	{
		return _focused;
	}

	/* ---------------- selection ---------------- */

	public function select(a_index: Number, a_byMouse: Boolean, a_silent: Boolean): Void
	{
		if (_data.length == 0)
			a_index = -1;
		else
			a_index = Math.max(0, Math.min(_data.length - 1, a_index));
		if (a_index == _sel) {
			ensureVisible(_sel);
			return;
		}
		_sel = a_index;
		if (!a_byMouse)
			ensureVisible(_sel);
		refresh();
		if (!a_silent && onSelectionChange != undefined)
			onSelectionChange(_sel, a_byMouse == true);
	}

	/* Moves the selection by delta rows; true when it moved. */
	public function move(a_delta: Number): Boolean
	{
		if (_data.length == 0)
			return false;
		var target: Number = _sel < 0 ? 0 : _sel + a_delta;
		target = Math.max(0, Math.min(_data.length - 1, target));
		if (target == _sel)
			return false;
		select(target, false);
		return true;
	}

	public function page(a_dir: Number): Boolean
	{
		return move(a_dir * Math.max(1, _count - 1));
	}

	public function home(): Boolean
	{
		return move(-_data.length);
	}

	public function end(): Boolean
	{
		return move(_data.length);
	}

	/* Mouse wheel / scrollbar: scrolls the window, selection follows only if it leaves view. */
	public function scroll(a_rows: Number): Void
	{
		var old: Number = _top;
		_top += a_rows;
		clampTop();
		if (_top == old)
			return;
		if (_sel >= 0 && (_sel < _top || _sel >= _top + _count))
			select(Math.max(_top, Math.min(_top + _count - 1, _sel)), true);
		refresh();
	}

	public function ensureVisible(a_index: Number): Void
	{
		if (a_index < 0)
			return;
		if (a_index < _top)
			_top = a_index;
		else if (a_index >= _top + _count)
			_top = a_index - _count + 1;
		clampTop();
	}

	private function clampTop(): Void
	{
		var maxTop: Number = Math.max(0, _data.length - _count);
		if (_top > maxTop)
			_top = maxTop;
		if (_top < 0)
			_top = 0;
	}

	public function selectSlot(a_slot: Number, a_byMouse: Boolean): Void
	{
		var idx: Number = _top + a_slot;
		if (idx < _data.length)
			select(idx, a_byMouse);
	}

	public function pressSlot(a_slot: Number): Void
	{
		var idx: Number = _top + a_slot;
		if (idx >= _data.length)
			return;
		select(idx, true);
		if (onItemPress != undefined)
			onItemPress(idx);
	}

	public function containsMouse(): Boolean
	{
		return clip._visible && clip.hitTest(_root._xmouse, _root._ymouse, false) &&
			clip._xmouse >= 0 && clip._xmouse <= _w && clip._ymouse >= 0 && clip._ymouse <= _h;
	}

	/* ---------------- painting ---------------- */

	public function refresh(): Void
	{
		for (var i: Number = 0; i < _rows.length; i++) {
			var row: Object = _rows[i];
			var idx: Number = _top + i;
			var mc: MovieClip = row.mc;
			if (idx < _data.length) {
				mc._visible = true;
				row.index = idx;
				if (renderRow != undefined)
					renderRow(row, _data[idx], idx, idx == _sel, _focused);
			} else {
				mc._visible = false;
				row.index = -1;
			}
		}
		_emptyTf._visible = _data.length == 0 && _emptyText != "";
		if (_emptyTf._visible)
			Text.set(_emptyTf, _emptyText);
		drawBar();
	}

	private function drawBar(): Void
	{
		_barMc.clear();
		_thumbMc.clear();
		if (_data.length <= _count || _count <= 0)
			return;
		var x: Number = _w - BAR_W;
		Draw.vdivider(_barMc, x + 2, 0, _h, 30);
		var th: Number = Math.max(24, _h * _count / _data.length);
		var ty: Number = (_h - th) * _top / (_data.length - _count);
		Draw.rect(_thumbMc, x + 1, ty, 3, th, 0xFFFFFF, 55);
	}

	private function initThumbDrag(): Void
	{
		var self: VirtualList = this;
		_thumbMc.onPress = function(): Void {
			var startY: Number = self.clip._ymouse;
			var startTop: Number = self.topIndex;
			this.onMouseMove = function(): Void {
				self.dragTo(startTop, self.clip._ymouse - startY);
			};
		};
		_thumbMc.onRelease = _thumbMc.onReleaseOutside = function(): Void {
			delete this.onMouseMove;
		};
		_barMc.onPress = function(): Void {
			var y: Number = self.clip._ymouse;
			self.pageTowards(y);
		};
		_thumbMc.useHandCursor = false;
		_barMc.useHandCursor = false;
	}

	public function get topIndex(): Number
	{
		return _top;
	}

	public function dragTo(a_startTop: Number, a_dy: Number): Void
	{
		var th: Number = Math.max(24, _h * _count / _data.length);
		var range: Number = _h - th;
		if (range <= 0)
			return;
		var rowsPerUnit: Number = (_data.length - _count) / range;
		var newTop: Number = Math.round(a_startTop + a_dy * rowsPerUnit);
		scroll(newTop - _top);
	}

	public function pageTowards(a_y: Number): Void
	{
		var th: Number = Math.max(24, _h * _count / _data.length);
		var ty: Number = (_h - th) * _top / Math.max(1, _data.length - _count);
		scroll(a_y < ty ? -_count : _count);
	}
}
