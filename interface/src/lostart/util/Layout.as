/*
 * Screen geometry. Uses the GFx extensions Stage.visibleRect (visible area in movie
 * coordinates, whatever scale mode the DLL chose) and Stage.safeRect (TV safe-area insets),
 * exactly like vanilla/SkyUI menus; falls back to Stage.width/height elsewhere.
 */
import lostart.Theme;

class lostart.util.Layout
{
	/* {x, y, w, h} in stage coordinates. */
	public static function viewport(): Object
	{
		var vr: Object = Stage["visibleRect"];
		if (vr != undefined && vr.width > 0 && vr.height > 0)
			return {x: vr.x, y: vr.y, w: vr.width, h: vr.height};
		return {x: 0, y: 0, w: Stage.width, h: Stage.height};
	}

	/* Safe-area insets {x, y} in stage coordinates (0 when unknown). */
	public static function safeInset(): Object
	{
		var sr: Object = Stage["safeRect"];
		if (sr != undefined && sr.x != undefined)
			return {x: Math.max(0, sr.x), y: Math.max(0, sr.y)};
		return {x: 0, y: 0};
	}

	/*
	 * Computes the design-space frame: the root content clip is scaled by `scale` so that the
	 * visible height maps to 1080 design units. `width` is the visible width in design units
	 * (1920 at 16:9, 2520 at 21:9, 1440 at 4:3).
	 */
	public static function frame(): Object
	{
		var vp: Object = viewport();
		var safe: Object = safeInset();
		var scale: Number = vp.h / Theme.DESIGN_HEIGHT;
		if (!(scale > 0))
			scale = 1;
		var w: Number = vp.w / scale;
		var h: Number = Theme.DESIGN_HEIGHT;
		var mx: Number = Math.max(Theme.MIN_MARGIN_X, safe.x / scale + 24);
		var my: Number = Math.max(Theme.MIN_MARGIN_Y, safe.y / scale + 20);
		var cw: Number = Math.min(w - 2 * mx, Theme.MAX_CONTENT_WIDTH);
		var cx: Number = Math.round((w - cw) / 2);
		return {x: vp.x, y: vp.y, scale: scale, width: w, height: h,
			contentX: cx, contentY: my, contentW: Math.round(cw), contentH: Math.round(h - 2 * my)};
	}
}
