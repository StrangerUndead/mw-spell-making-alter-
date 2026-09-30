/*
 * Procedural drawing helpers (the SWF ships no library symbols or bitmaps).
 */
class lostart.util.Draw
{
	public static function rect(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_color: Number, a_alpha: Number): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		a_mc.lineStyle();
		a_mc.beginFill(a_color, a_alpha);
		a_mc.moveTo(a_x, a_y);
		a_mc.lineTo(a_x + a_w, a_y);
		a_mc.lineTo(a_x + a_w, a_y + a_h);
		a_mc.lineTo(a_x, a_y + a_h);
		a_mc.lineTo(a_x, a_y);
		a_mc.endFill();
	}

	/* 1-unit frame drawn as four thin fills (crisper than lineStyle under scaling). */
	public static function frame(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_t: Number, a_color: Number, a_alpha: Number): Void
	{
		rect(a_mc, a_x, a_y, a_w, a_t, a_color, a_alpha);
		rect(a_mc, a_x, a_y + a_h - a_t, a_w, a_t, a_color, a_alpha);
		rect(a_mc, a_x, a_y + a_t, a_t, a_h - 2 * a_t, a_color, a_alpha);
		rect(a_mc, a_x + a_w - a_t, a_y + a_t, a_t, a_h - 2 * a_t, a_color, a_alpha);
	}

	public static function roundRect(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_r: Number, a_color: Number, a_alpha: Number): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		var r: Number = Math.min(a_r, Math.min(a_w, a_h) / 2);
		a_mc.lineStyle();
		a_mc.beginFill(a_color, a_alpha);
		a_mc.moveTo(a_x + r, a_y);
		a_mc.lineTo(a_x + a_w - r, a_y);
		a_mc.curveTo(a_x + a_w, a_y, a_x + a_w, a_y + r);
		a_mc.lineTo(a_x + a_w, a_y + a_h - r);
		a_mc.curveTo(a_x + a_w, a_y + a_h, a_x + a_w - r, a_y + a_h);
		a_mc.lineTo(a_x + r, a_y + a_h);
		a_mc.curveTo(a_x, a_y + a_h, a_x, a_y + a_h - r);
		a_mc.lineTo(a_x, a_y + r);
		a_mc.curveTo(a_x, a_y, a_x + r, a_y);
		a_mc.endFill();
	}

	public static function roundFrame(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_r: Number, a_t: Number, a_color: Number, a_alpha: Number): Void
	{
		var r: Number = Math.min(a_r, Math.min(a_w, a_h) / 2);
		a_mc.lineStyle(a_t, a_color, a_alpha);
		a_mc.moveTo(a_x + r, a_y);
		a_mc.lineTo(a_x + a_w - r, a_y);
		a_mc.curveTo(a_x + a_w, a_y, a_x + a_w, a_y + r);
		a_mc.lineTo(a_x + a_w, a_y + a_h - r);
		a_mc.curveTo(a_x + a_w, a_y + a_h, a_x + a_w - r, a_y + a_h);
		a_mc.lineTo(a_x + r, a_y + a_h);
		a_mc.curveTo(a_x, a_y + a_h, a_x, a_y + a_h - r);
		a_mc.lineTo(a_x, a_y + r);
		a_mc.curveTo(a_x, a_y, a_x + r, a_y);
		a_mc.lineStyle();
	}

	public static function circle(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_r: Number, a_color: Number, a_alpha: Number): Void
	{
		var k: Number = 0.4142 * a_r;   // tan(pi/8)
		var d: Number = 0.7071 * a_r;   // sin(pi/4)
		a_mc.lineStyle();
		a_mc.beginFill(a_color, a_alpha);
		a_mc.moveTo(a_cx + a_r, a_cy);
		a_mc.curveTo(a_cx + a_r, a_cy + k, a_cx + d, a_cy + d);
		a_mc.curveTo(a_cx + k, a_cy + a_r, a_cx, a_cy + a_r);
		a_mc.curveTo(a_cx - k, a_cy + a_r, a_cx - d, a_cy + d);
		a_mc.curveTo(a_cx - a_r, a_cy + k, a_cx - a_r, a_cy);
		a_mc.curveTo(a_cx - a_r, a_cy - k, a_cx - d, a_cy - d);
		a_mc.curveTo(a_cx - k, a_cy - a_r, a_cx, a_cy - a_r);
		a_mc.curveTo(a_cx + k, a_cy - a_r, a_cx + d, a_cy - d);
		a_mc.curveTo(a_cx + a_r, a_cy - k, a_cx + a_r, a_cy);
		a_mc.endFill();
	}

	/* Filled triangle pointing up (a_dir = -1) or down (a_dir = 1), centred on (cx, cy). */
	public static function triangle(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_size: Number, a_dir: Number, a_color: Number, a_alpha: Number): Void
	{
		var h: Number = a_size * 0.5;
		a_mc.lineStyle();
		a_mc.beginFill(a_color, a_alpha);
		a_mc.moveTo(a_cx - h, a_cy - a_dir * h * 0.6);
		a_mc.lineTo(a_cx + h, a_cy - a_dir * h * 0.6);
		a_mc.lineTo(a_cx, a_cy + a_dir * h * 0.6);
		a_mc.lineTo(a_cx - h, a_cy - a_dir * h * 0.6);
		a_mc.endFill();
	}

	/* An "x" made of two strokes. */
	public static function cross(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_size: Number, a_t: Number, a_color: Number, a_alpha: Number): Void
	{
		var h: Number = a_size * 0.5;
		a_mc.lineStyle(a_t, a_color, a_alpha);
		a_mc.moveTo(a_cx - h, a_cy - h);
		a_mc.lineTo(a_cx + h, a_cy + h);
		a_mc.moveTo(a_cx + h, a_cy - h);
		a_mc.lineTo(a_cx - h, a_cy + h);
		a_mc.lineStyle();
	}

	/* Magnifier glyph for the search box. */
	public static function magnifier(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_size: Number, a_color: Number, a_alpha: Number): Void
	{
		var r: Number = a_size * 0.32;
		a_mc.lineStyle(2, a_color, a_alpha);
		var k: Number = 0.4142 * r;
		var d: Number = 0.7071 * r;
		var cx: Number = a_cx - a_size * 0.1;
		var cy: Number = a_cy - a_size * 0.1;
		a_mc.moveTo(cx + r, cy);
		a_mc.curveTo(cx + r, cy + k, cx + d, cy + d);
		a_mc.curveTo(cx + k, cy + r, cx, cy + r);
		a_mc.curveTo(cx - k, cy + r, cx - d, cy + d);
		a_mc.curveTo(cx - r, cy + k, cx - r, cy);
		a_mc.curveTo(cx - r, cy - k, cx - d, cy - d);
		a_mc.curveTo(cx - k, cy - r, cx, cy - r);
		a_mc.curveTo(cx + k, cy - r, cx + d, cy - d);
		a_mc.curveTo(cx + r, cy - k, cx + r, cy);
		a_mc.moveTo(cx + d, cy + d);
		a_mc.lineTo(a_cx + a_size * 0.4, a_cy + a_size * 0.4);
		a_mc.lineStyle();
	}

	/*
	 * Linear gradient fill over a rectangle. a_vertical: the gradient runs top->bottom, else
	 * left->right. a_alphas/a_ratios as for beginGradientFill (ratios 0..255).
	 */
	public static function gradient(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number,
		a_color: Number, a_alphas: Array, a_ratios: Array, a_vertical: Boolean): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		var colors: Array = [];
		for (var i: Number = 0; i < a_alphas.length; i++)
			colors.push(a_color);
		var matrix: Object = {matrixType: "box", x: a_x, y: a_y, w: a_w, h: a_h, r: a_vertical ? Math.PI / 2 : 0};
		a_mc.lineStyle();
		a_mc.beginGradientFill("linear", colors, a_alphas, a_ratios, matrix);
		a_mc.moveTo(a_x, a_y);
		a_mc.lineTo(a_x + a_w, a_y);
		a_mc.lineTo(a_x + a_w, a_y + a_h);
		a_mc.lineTo(a_x, a_y + a_h);
		a_mc.lineTo(a_x, a_y);
		a_mc.endFill();
	}

	/*
	 * Skyrim modal box (message box, quantity/slider dialogs): a black body that is darkest in
	 * the middle, with a pair of hairlines top and bottom that fade out at both ends. No corner
	 * art, no colour.
	 */
	public static function panel(a_mc: MovieClip, a_w: Number, a_h: Number, a_alpha: Number, a_focused: Boolean): Void
	{
		a_mc.clear();
		panelBody(a_mc, a_w, a_h, a_alpha, a_focused);
	}

	/* A panel that sits over other content: an opaque dark base under the usual panel, so
	   nothing behind it shows through its softer edges. */
	public static function modalPanel(a_mc: MovieClip, a_w: Number, a_h: Number): Void
	{
		a_mc.clear();
		rect(a_mc, 0, 0, a_w, a_h, 0x0C0C0C, 100);
		panelBody(a_mc, a_w, a_h, lostart.Theme.MODAL_ALPHA, true);
	}

	private static function panelBody(a_mc: MovieClip, a_w: Number, a_h: Number, a_alpha: Number, a_focused: Boolean): Void
	{
		gradient(a_mc, 0, 0, a_w, a_h, lostart.Theme.PANEL, [a_alpha * 0.85, a_alpha, a_alpha, a_alpha * 0.85], [0, 70, 185, 255], false);
		var la: Number = a_focused ? lostart.Theme.BORDER_FOCUS_ALPHA : lostart.Theme.BORDER_ALPHA + 10;
		divider(a_mc, 0, 0, a_w, la);
		divider(a_mc, a_w * 0.08, 4, a_w * 0.84, la * 0.5);
		divider(a_mc, 0, a_h - 1, a_w, la);
		divider(a_mc, a_w * 0.08, a_h - 5, a_w * 0.84, la * 0.5);
	}

	/*
	 * A band whose alpha ramps up over the first a_fade of its width, holds, and ramps down over
	 * the last a_fade (0..0.5), built from solid strips: gradient fills leave anti-aliasing seams
	 * along their edges and in 1-unit lines in some renderers, stepped solids never do.
	 */
	public static function hfade(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number,
		a_color: Number, a_alpha: Number, a_fade: Number): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		// Strip edges are snapped to whole units so neighbouring strips share an edge exactly
		// (no anti-aliasing seam). Meant for thin rules; large areas use gradient().
		var steps: Number = 12;
		var fw: Number = a_w * a_fade;
		var x0: Number = Math.round(a_x);
		var x1: Number = Math.round(a_x + a_w);
		var prevL: Number = x0;
		var prevR: Number = x1;
		for (var i: Number = 0; i < steps; i++) {
			var a: Number = a_alpha * (i + 0.5) / steps;
			var l: Number = Math.round(a_x + (i + 1) * fw / steps);
			var r: Number = Math.round(a_x + a_w - (i + 1) * fw / steps);
			rect(a_mc, prevL, a_y, l - prevL, a_h, a_color, a);
			rect(a_mc, r, a_y, prevR - r, a_h, a_color, a);
			prevL = l;
			prevR = r;
		}
		rect(a_mc, prevL, a_y, prevR - prevL, a_h, a_color, a_alpha);
	}

	/* Vertical counterpart of hfade. */
	public static function vfade(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number,
		a_color: Number, a_alpha: Number, a_fade: Number): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		var steps: Number = 12;
		var fh: Number = a_h * a_fade;
		var prevT: Number = Math.round(a_y);
		var prevB: Number = Math.round(a_y + a_h);
		for (var i: Number = 0; i < steps; i++) {
			var a: Number = a_alpha * (i + 0.5) / steps;
			var t: Number = Math.round(a_y + (i + 1) * fh / steps);
			var b: Number = Math.round(a_y + a_h - (i + 1) * fh / steps);
			rect(a_mc, a_x, prevT, a_w, t - prevT, a_color, a);
			rect(a_mc, a_x, b, a_w, prevB - b, a_color, a);
			prevT = t;
			prevB = b;
		}
		rect(a_mc, a_x, prevT, a_w, prevB - prevT, a_color, a_alpha);
	}

	/* Horizontal hairline that fades out at both ends (the vanilla menu divider). */
	public static function divider(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_alpha: Number): Void
	{
		hfade(a_mc, a_x, a_y, a_w, 1, lostart.Theme.BORDER, a_alpha, 0.25);
	}

	/* Vertical hairline fading at both ends (column separator). */
	public static function vdivider(a_mc: MovieClip, a_x: Number, a_y: Number, a_h: Number, a_alpha: Number): Void
	{
		vfade(a_mc, a_x, a_y, 1, a_h, lostart.Theme.BORDER, a_alpha, 0.25);
	}

	/*
	 * List selection highlight (SkyUI's selectIndicator): a soft white band that fades towards
	 * both ends, with faint hairlines above and below when the list has focus.
	 */
	public static function selectBar(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_focused: Boolean): Void
	{
		var a: Number = a_focused ? lostart.Theme.ROW_SELECT_FOCUS_ALPHA : lostart.Theme.ROW_SELECT_ALPHA;
		gradient(a_mc, a_x, a_y, a_w, a_h, lostart.Theme.ROW_SELECT, [0, a, a, 0], [0, 50, 205, 255], false);
		if (a_focused) {
			divider(a_mc, a_x, a_y, a_w, 30);
			divider(a_mc, a_x, a_y + a_h - 1, a_w, 30);
		}
	}

	/* Small diamond (the vanilla bullet / fallback effect icon). */
	public static function diamond(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_r: Number, a_color: Number, a_alpha: Number): Void
	{
		a_mc.lineStyle();
		a_mc.beginFill(a_color, a_alpha);
		a_mc.moveTo(a_cx, a_cy - a_r);
		a_mc.lineTo(a_cx + a_r, a_cy);
		a_mc.lineTo(a_cx, a_cy + a_r);
		a_mc.lineTo(a_cx - a_r, a_cy);
		a_mc.lineTo(a_cx, a_cy - a_r);
		a_mc.endFill();
	}

	/* Outlined diamond. */
	public static function diamondFrame(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_r: Number, a_t: Number, a_color: Number, a_alpha: Number): Void
	{
		a_mc.lineStyle(a_t, a_color, a_alpha);
		a_mc.moveTo(a_cx, a_cy - a_r);
		a_mc.lineTo(a_cx + a_r, a_cy);
		a_mc.lineTo(a_cx, a_cy + a_r);
		a_mc.lineTo(a_cx - a_r, a_cy);
		a_mc.lineTo(a_cx, a_cy - a_r);
		a_mc.lineStyle();
	}

	/* Thin chevron "<" (a_dir -1) or ">" (a_dir 1), for option selectors. */
	public static function chevron(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_size: Number, a_dir: Number, a_color: Number, a_alpha: Number): Void
	{
		var h: Number = a_size * 0.5;
		a_mc.lineStyle(2, a_color, a_alpha);
		a_mc.moveTo(a_cx - a_dir * h * 0.5, a_cy - h);
		a_mc.lineTo(a_cx + a_dir * h * 0.5, a_cy);
		a_mc.lineTo(a_cx - a_dir * h * 0.5, a_cy + h);
		a_mc.lineStyle();
	}
}
