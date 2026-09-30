/*
 * School tabs for Effects Known: All, Alteration, Conjuration, Destruction, Illusion,
 * Restoration (SkyUI category bar look: dim captions, the active one bright with a coloured
 * underline). Clicking a tab selects it; A/D and LB/RB are routed here by the menu.
 */
import lostart.Theme;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.TabBar
{
	public var clip: MovieClip;
	public var onChange: Function;   // fn(index)

	private var _tabs: Array;
	private var _sel: Number = 0;
	private var _hover: Number = -1;
	private var _w: Number = 0;
	private var _h: Number = 40;
	private var _labels: Array;
	private var _colors: Array;
	private var _counts: Array;

	public function TabBar(a_parent: MovieClip, a_name: String, a_depth: Number, a_labels: Array, a_colors: Array)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_labels = a_labels;
		_colors = a_colors;
		_tabs = [];
		var self: TabBar = this;
		for (var i: Number = 0; i < a_labels.length; i++) {
			var mc: MovieClip = clip.createEmptyMovieClip("tab" + i, i + 1);
			var tf: TextField = Text.create(mc, "tf", 2, 0, 0, 50, _h, Theme.FS_SMALL, Theme.TEXT_HINT, Theme.FONT_MEDIUM, "center");
			var hit: MovieClip = mc.createEmptyMovieClip("hit", 3);
			var tab: Object = {mc: mc, tf: tf, hit: hit, index: i};
			_tabs.push(tab);
			bindTab(hit, i);
		}
	}

	private function bindTab(a_hit: MovieClip, a_i: Number): Void
	{
		var self: TabBar = this;
		a_hit.onRelease = function(): Void { self.select(a_i, false); };
		a_hit.onRollOver = function(): Void { self.setHover(a_i); };
		a_hit.onRollOut = a_hit.onDragOut = function(): Void { self.setHover(-1); };
		a_hit.useHandCursor = false;
	}

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		redraw();
	}

	public function get selectedIndex(): Number
	{
		return _sel;
	}

	public function get count(): Number
	{
		return _tabs.length;
	}

	public function select(a_i: Number, a_silent: Boolean): Void
	{
		var n: Number = _tabs.length;
		a_i = ((a_i % n) + n) % n;
		if (a_i == _sel)
			return;
		_sel = a_i;
		redraw();
		if (!a_silent && onChange != undefined)
			onChange(_sel);
	}

	public function step(a_delta: Number): Void
	{
		select(_sel + a_delta, false);
	}

	public function setCounts(a_counts: Array): Void
	{
		_counts = a_counts;
		redraw();
	}

	public function setHover(a_i: Number): Void
	{
		_hover = a_i;
		redraw();
	}

	private function redraw(): Void
	{
		var n: Number = _tabs.length;
		if (n == 0 || _w <= 0)
			return;
		var tw: Number = _w / n;
		for (var i: Number = 0; i < n; i++) {
			var t: Object = _tabs[i];
			var mc: MovieClip = t.mc;
			var tf: TextField = t.tf;
			var hit: MovieClip = t.hit;
			mc._x = Math.round(i * tw);
			mc._y = 0;
			mc.clear();
			hit.clear();
			Draw.rect(hit, 0, 0, tw - 2, _h, 0, 0);
			var active: Boolean = i == _sel;
			var empty: Boolean = _counts != undefined && _counts[i] == 0;
			var color: Number = _colors[i] == undefined ? Theme.ACCENT : _colors[i];
			if (active) {
				Draw.rect(mc, 0, 0, tw - 2, _h, color, 14);
				Draw.rect(mc, 0, _h - 3, tw - 2, 3, color, 100);
			} else if (i == _hover) {
				Draw.rect(mc, 0, 0, tw - 2, _h, 0xFFFFFF, 6);
			}
			Draw.rect(mc, 0, _h - 1, tw - 2, 1, Theme.BORDER, 20);
			tf._width = tw - 2;
			tf._height = _h;
			Text.set(tf, _labels[i]);
			tf._y = Math.round((_h - tf.textHeight) / 2) - 3;
			Text.setColor(tf, active ? Theme.TEXT : (empty ? Theme.TEXT_DIM : Theme.TEXT_HINT));
		}
	}
}
