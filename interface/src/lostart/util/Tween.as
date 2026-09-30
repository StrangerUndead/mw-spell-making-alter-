/*
 * Minimal time-based tween driven by a clip's onEnterFrame (one tween per driver clip).
 * Used for the cost count-up and the open/close fade.
 */
class lostart.util.Tween
{
	/*
	 * Animates from a_from to a_to over a_ms, calling a_update(value) each frame and
	 * a_done() at the end. Starting a new tween on the same driver replaces the old one.
	 */
	public static function run(a_driver: MovieClip, a_from: Number, a_to: Number, a_ms: Number, a_update: Function, a_done: Function): Void
	{
		var start: Number = getTimer();
		if (a_ms <= 0 || a_from == a_to) {
			delete a_driver.onEnterFrame;
			a_update(a_to);
			if (a_done != undefined)
				a_done();
			return;
		}
		a_driver.onEnterFrame = function(): Void {
			var t: Number = (getTimer() - start) / a_ms;
			if (t >= 1) {
				delete this.onEnterFrame;
				a_update(a_to);
				if (a_done != undefined)
					a_done();
				return;
			}
			var e: Number = 1 - (1 - t) * (1 - t);   // ease-out quad
			a_update(a_from + (a_to - a_from) * e);
		};
	}

	public static function stop(a_driver: MovieClip): Void
	{
		delete a_driver.onEnterFrame;
	}
}
