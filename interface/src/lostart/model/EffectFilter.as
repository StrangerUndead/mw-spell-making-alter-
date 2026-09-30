/*
 * Effects Known filtering: school tab + case-insensitive substring search.
 * Pure functions over the LA_SetKnown array; no UI. Sorting is the DLL's (it sends the list
 * alphabetised), filtering keeps that order.
 */
class lostart.model.EffectFilter
{
	public static var TAB_ALL: Number = -1;

	/* Tab index 0 = All, 1..5 = schools 0..4. */
	public static function schoolForTab(a_tab: Number): Number
	{
		return a_tab <= 0 ? TAB_ALL : a_tab - 1;
	}

	public static function normalize(a_query: String): String
	{
		if (a_query == undefined || a_query == null)
			return "";
		var s: String = a_query;
		while (s.length > 0 && (s.charAt(0) == " " || s.charAt(0) == "/"))
			s = s.substr(1);
		while (s.length > 0 && s.charAt(s.length - 1) == " ")
			s = s.substr(0, s.length - 1);
		return s.toLowerCase();
	}

	public static function matches(a_entry: Object, a_school: Number, a_queryLower: String): Boolean
	{
		if (a_entry == undefined)
			return false;
		if (a_school != TAB_ALL && a_entry.school != a_school)
			return false;
		if (a_queryLower.length == 0)
			return true;
		var name: String = a_entry.text == undefined ? "" : String(a_entry.text);
		return name.toLowerCase().indexOf(a_queryLower) >= 0;
	}

	public static function apply(a_known: Array, a_school: Number, a_query: String): Array
	{
		var out: Array = [];
		if (a_known == undefined)
			return out;
		var q: String = normalize(a_query);
		for (var i: Number = 0; i < a_known.length; i++) {
			if (matches(a_known[i], a_school, q))
				out.push(a_known[i]);
		}
		return out;
	}

	/* Count per tab (All + 5 schools) for the tab captions. */
	public static function countBySchool(a_known: Array): Array
	{
		var counts: Array = [0, 0, 0, 0, 0, 0];
		if (a_known == undefined)
			return counts;
		counts[0] = a_known.length;
		for (var i: Number = 0; i < a_known.length; i++) {
			var s: Number = a_known[i].school;
			if (s >= 0 && s <= 4)
				counts[s + 1]++;
		}
		return counts;
	}
}
