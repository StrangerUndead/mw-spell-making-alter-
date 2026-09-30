/*
 * An icon from SkyUI's icon libraries (Interface/skyui/icons_*_psychosteve.swf), loaded at run
 * time from the player's SkyUI install exactly as SkyUI's own list entries do
 * (CategoryListEntry / InventoryListEntry: MovieClipLoader.loadClip, then gotoAndStop(label)).
 * Nothing from SkyUI is shipped with this mod. When the library can't be loaded (SkyUI missing,
 * or the offline harness), a small drawn glyph stands in so the layout never changes.
 */
import lostart.util.Draw;

class lostart.components.SkyIcon
{
	public var clip: MovieClip;
	public var onLoaded: Function;   // fn(ok:Boolean) once the library answered

	private var _holder: MovieClip;
	private var _fallback: MovieClip;
	private var _source: String;
	private var _label: String;
	private var _size: Number;
	private var _color: Number;
	private var _state: String = "none";   // none | loading | ready | failed

	public function SkyIcon(a_parent: MovieClip, a_name: String, a_depth: Number, a_source: String, a_size: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_fallback = clip.createEmptyMovieClip("fallback", 1);
		_holder = clip.createEmptyMovieClip("holder", 2);
		_source = a_source;
		_size = a_size;
		_color = 0xFFFFFF;
	}

	public function get ready(): Boolean
	{
		return _state == "ready";
	}

	public function get failed(): Boolean
	{
		return _state == "failed";
	}

	public function setSize(a_size: Number): Void
	{
		_size = a_size;
		apply();
	}

	/* a_color: 0xFFFFFF for none (SkyUI tints only the elemental icons). */
	public function show(a_label: String, a_color: Number): Void
	{
		_label = a_label;
		_color = a_color == undefined ? 0xFFFFFF : a_color;
		if (_state == "none")
			load();
		apply();
	}

	private function load(): Void
	{
		var self: SkyIcon = this;
		_state = "loading";
		var loader: MovieClipLoader = new MovieClipLoader();
		var listener: Object = {};
		listener.onLoadInit = function(a_mc: MovieClip): Void {
			self.loaded(true);
		};
		listener.onLoadError = function(a_mc: MovieClip, a_error: String): Void {
			self.loaded(false);
		};
		loader.addListener(listener);
		loader.loadClip(_source, _holder);
	}

	private function loaded(a_ok: Boolean): Void
	{
		_state = a_ok ? "ready" : "failed";
		apply();
		if (onLoaded != undefined)
			onLoaded(a_ok);
	}

	private function apply(): Void
	{
		_fallback.clear();
		if (_state == "ready") {
			_holder._visible = true;
			_holder.gotoAndStop(_label);
			// As SkyUI's CategoryListEntry.onLoadInit: icon sets with a background clip are scaled
			// until the background is the icon size; others get _width = _height = size.
			_holder._xscale = _holder._yscale = 100;
			var bg: MovieClip = _holder["background"];
			if (bg != undefined && bg._width > 0) {
				_holder._xscale = _holder._yscale = _size / bg._width * 100;
			} else {
				_holder._width = _size;
				_holder._height = _size;
			}
			var ct: Color = new Color(_holder);
			if (_color == 0xFFFFFF) {
				ct.setTransform({ra: 100, rb: 0, ga: 100, gb: 0, ba: 100, bb: 0, aa: 100, ab: 0});
			} else {
				ct.setRGB(_color);
			}
			return;
		}
		_holder._visible = false;
		drawFallback();
	}

	/* Drawn stand-in: a diamond for effects, school sigils for the magic categories. */
	private function drawFallback(): Void
	{
		var s: Number = _size;
		var c: Number = s / 2;
		var col: Number = _color;
		var mc: MovieClip = _fallback;
		switch (_label) {
			case "mag_all":
				Draw.diamondFrame(mc, c, c, s * 0.42, 2, col, 100);
				Draw.diamond(mc, c, c, s * 0.16, col, 100);
				break;
			case "mag_alteration":
				Draw.diamondFrame(mc, c, c, s * 0.42, 2, col, 100);
				Draw.rect(mc, c - 1, c - s * 0.28, 2, s * 0.56, col, 100);
				break;
			case "mag_conjuration":
				Draw.circle(mc, c, c, s * 0.40, col, 100);
				Draw.circle(mc, c, c, s * 0.30, 0x000000, 100);
				Draw.diamond(mc, c, c, s * 0.14, col, 100);
				break;
			case "mag_destruction":
				Draw.triangle(mc, c, c + s * 0.05, s * 0.8, -1, col, 100);
				break;
			case "mag_illusion":
				Draw.circle(mc, c, c, s * 0.40, col, 100);
				Draw.circle(mc, c + s * 0.12, c - s * 0.06, s * 0.34, 0x000000, 100);
				break;
			case "mag_restoration":
				Draw.rect(mc, c - s * 0.07, c - s * 0.38, s * 0.14, s * 0.76, col, 100);
				Draw.rect(mc, c - s * 0.38, c - s * 0.07, s * 0.76, s * 0.14, col, 100);
				break;
			default:
				Draw.diamondFrame(mc, c, c, s * 0.34, 1.5, col, 90);
				Draw.diamond(mc, c, c, s * 0.14, col, 100);
				break;
		}
	}
}
