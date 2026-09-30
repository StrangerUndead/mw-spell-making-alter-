/*
 * School filter for Effects Known, drawn as SkyUI's category icon bar: a row of magic-category
 * icons (SkyUI's mag_all / mag_alteration ... from icons_category_psychosteve.swf) where the
 * active one is fully opaque, the others at 50 % and empty ones at 15 % (CategoryListEntry
 * alphas), with the active category's name in grey capitals under a fading rule.
 * Clicking an icon selects it; A/D and LB/RB are routed here by the menu.
 */
import lostart.Theme;
import lostart.components.SkyIcon;
import lostart.util.Draw;
import lostart.util.Text;

class lostart.components.TabBar
{
	public var clip: MovieClip;
	public var onChange: Function;   // fn(index)

	private var _tabs: Array;
	private var _lineMc: MovieClip;
	private var _captionTf: TextField;
	private var _sel: Number = 0;
	private var _hover: Number = -1;
	private var _w: Number = 0;
	private var _h: Number = 70;
	private var _captions: Array;
	private var _counts: Array;

	/* a_labels: tooltip/caption keys per tab; a_colors: unused (Skyrim draws icons white). */
	public function TabBar(a_parent: MovieClip, a_name: String, a_depth: Number, a_labels: Array, a_colors: Array)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_lineMc = clip.createEmptyMovieClip("line", 1);
		_captionTf = Text.label(clip, "caption", 2, 300, "left");
		_captions = ["$LA_UI_TabAll", "$LA_School_Alteration", "$LA_School_Conjuration",
			"$LA_School_Destruction", "$LA_School_Illusion", "$LA_School_Restoration"];
		_tabs = [];
		for (var i: Number = 0; i < a_labels.length; i++) {
			var mc: MovieClip = clip.createEmptyMovieClip("tab" + i, 10 + i);
			var hit: MovieClip = mc.createEmptyMovieClip("hit", 1);
			var icon: SkyIcon = new SkyIcon(mc, "icon", 2, Theme.ICONS_CATEGORY, Theme.TAB_ICON_SIZE);
			icon.show(Theme.TAB_ICON_LABELS[i], 0xFFFFFF);
			var tab: Object = {mc: mc, hit: hit, icon: icon, index: i};
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
		var s: Number = Theme.TAB_ICON_SIZE;
		var cell: Number = Math.min(Math.floor(_w / n), s + 34);
		for (var i: Number = 0; i < n; i++) {
			var t: Object = _tabs[i];
			var mc: MovieClip = t.mc;
			mc._x = Math.round(i * cell);
			mc._y = 0;
			t.hit.clear();
			Draw.rect(t.hit, 0, 0, cell, s + 8, 0, 0);
			var icon: SkyIcon = t.icon;
			icon.clip._x = Math.round((cell - s) / 2);
			icon.clip._y = 4;
			var active: Boolean = i == _sel;
			var empty: Boolean = _counts != undefined && _counts[i] == 0;
			icon.clip._alpha = active ? 100 : (empty ? 15 : (i == _hover ? 80 : 50));
		}
		_lineMc.clear();
		var lineY: Number = s + 14;
		Draw.divider(_lineMc, 0, lineY, _w, Theme.BORDER_ALPHA);
		// a short bright tick under the active icon, like the vanilla category marker
		Draw.hfade(_lineMc, _sel * cell, lineY - 1, cell, 2, 0xFFFFFF, 90, 0.5);
		_captionTf._x = 2;
		_captionTf._y = lineY + 6;
		_captionTf._width = _w;
		Text.setCaps(_captionTf, _captions[_sel] == undefined ? "" : _captions[_sel]);
	}
}
