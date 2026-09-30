/*
 * Small helpers for applying LA_SetState snapshots: the DLL always sends the whole state, the
 * SWF compares with the previous one to decide what to redraw and which feedback to play.
 */
class lostart.model.StateUtil
{
	public static function toSet(a_ids: Array): Object
	{
		var set: Object = {};
		if (a_ids == undefined)
			return set;
		for (var i: Number = 0; i < a_ids.length; i++)
			set[String(a_ids[i])] = true;
		return set;
	}

	public static function sameSet(a: Object, b: Object): Boolean
	{
		var k: String;
		for (k in a) {
			if (a[k] && !b[k])
				return false;
		}
		for (k in b) {
			if (b[k] && !a[k])
				return false;
		}
		return true;
	}

	/* Effects rows equal (same ids, texts, schools, editability, order). */
	public static function sameEffects(a: Array, b: Array): Boolean
	{
		if (a == undefined || b == undefined)
			return a == b;
		if (a.length != b.length)
			return false;
		for (var i: Number = 0; i < a.length; i++) {
			var x: Object = a[i];
			var y: Object = b[i];
			if (x.id != y.id || x.text != y.text || x.school != y.school || x.canEdit != y.canEdit)
				return false;
		}
		return true;
	}

	public static function indexOfId(a_list: Array, a_id: String): Number
	{
		if (a_list == undefined || a_id == undefined)
			return -1;
		for (var i: Number = 0; i < a_list.length; i++) {
			if (a_list[i].id == a_id)
				return i;
		}
		return -1;
	}

	public static function num(a_value: Object, a_default: Number): Number
	{
		if (a_value == undefined || a_value == null)
			return a_default;
		var n: Number = Number(a_value);
		return isNaN(n) ? a_default : n;
	}

	public static function str(a_value: Object): String
	{
		if (a_value == undefined || a_value == null)
			return "";
		return String(a_value);
	}

	public static function clamp(a_v: Number, a_lo: Number, a_hi: Number): Number
	{
		return a_v < a_lo ? a_lo : (a_v > a_hi ? a_hi : a_v);
	}
}
