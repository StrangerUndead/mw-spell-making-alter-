/*
 * Procedural drawing helpers (the SWF ships no library symbols or bitmaps).
 */
class lostart.util.Draw
{
	public static function rect(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_h: Number, a_color: Number, a_alpha: Number): Void
	{
		if (a_w <= 0 || a_h <= 0)
			return;
		a_mc.lineStyle(undefined);
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
		a_mc.lineStyle(undefined);
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
		a_mc.lineStyle(undefined);
	}

	public static function circle(a_mc: MovieClip, a_cx: Number, a_cy: Number, a_r: Number, a_color: Number, a_alpha: Number): Void
	{
		var k: Number = 0.4142 * a_r;   // tan(pi/8)
		var d: Number = 0.7071 * a_r;   // sin(pi/4)
		a_mc.lineStyle(undefined);
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
		a_mc.lineStyle(undefined);
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
		a_mc.lineStyle(undefined);
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
		a_mc.lineStyle(undefined);
	}

	/* Skyrim-style panel: dark translucent body, hairline border, brighter corner ticks. */
	public static function panel(a_mc: MovieClip, a_w: Number, a_h: Number, a_alpha: Number, a_focused: Boolean): Void
	{
		a_mc.clear();
		rect(a_mc, 0, 0, a_w, a_h, lostart.Theme.PANEL, a_alpha);
		var ba: Number = a_focused ? lostart.Theme.BORDER_FOCUS_ALPHA : lostart.Theme.BORDER_ALPHA;
		frame(a_mc, 0, 0, a_w, a_h, 1, lostart.Theme.BORDER, ba);
		var t: Number = 12;
		var ca: Number = a_focused ? 90 : 45;
		rect(a_mc, 0, 0, t, 2, lostart.Theme.ACCENT, ca);
		rect(a_mc, 0, 0, 2, t, lostart.Theme.ACCENT, ca);
		rect(a_mc, a_w - t, 0, t, 2, lostart.Theme.ACCENT, ca);
		rect(a_mc, a_w - 2, 0, 2, t, lostart.Theme.ACCENT, ca);
		rect(a_mc, 0, a_h - 2, t, 2, lostart.Theme.ACCENT, ca);
		rect(a_mc, 0, a_h - t, 2, t, lostart.Theme.ACCENT, ca);
		rect(a_mc, a_w - t, a_h - 2, t, 2, lostart.Theme.ACCENT, ca);
		rect(a_mc, a_w - 2, a_h - t, 2, t, lostart.Theme.ACCENT, ca);
	}

	/* Horizontal rule that fades at both ends (vanilla menu divider look). */
	public static function divider(a_mc: MovieClip, a_x: Number, a_y: Number, a_w: Number, a_alpha: Number): Void
	{
		var steps: Number = 8;
		var seg: Number = a_w / (steps * 2);
		for (var i: Number = 0; i < steps; i++) {
			var a: Number = a_alpha * (i + 1) / steps;
			rect(a_mc, a_x + i * seg, a_y, seg, 1, lostart.Theme.BORDER, a);
			rect(a_mc, a_x + a_w - (i + 1) * seg, a_y, seg, 1, lostart.Theme.BORDER, a);
		}
	}
}
