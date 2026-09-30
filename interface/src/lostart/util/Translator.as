/*
 * Translation helper.
 *
 * In game, Skyrim installs a GFx translator: any "$KEY" assigned to a TextField is replaced by
 * the value from Interface/Translations/<mod>_<LANGUAGE>.txt when it is set. We exploit that with
 * one hidden TextField to translate a key into a plain string, so labels can be composed
 * ("Magicka" + " 42") and then assigned with noTranslate. Strings that do not start with '$'
 * (already-built lines from the DLL, player-typed names) pass through untouched.
 *
 * The offline harness has no translator, so it can inject a dictionary via setDictionary().
 */
class lostart.util.Translator
{
	private static var _field: TextField;
	private static var _dict: Object;
	private static var _cache: Object = {};

	public static function setDictionary(a_dict: Object): Void
	{
		_dict = a_dict;
		_cache = {};
	}

	public static function tr(a_str: String): String
	{
		if (a_str == undefined || a_str == null)
			return "";
		if (a_str.length == 0 || a_str.charAt(0) != "$")
			return a_str;

		var hit: String = _cache[a_str];
		if (hit != undefined)
			return hit;

		var out: String;
		if (_dict != undefined && _dict[a_str] != undefined) {
			out = String(_dict[a_str]);
		} else {
			if (_field == undefined) {
				_field = _root.createTextField("__la_translator", 16000, -100, -100, 1, 1);
				_field._visible = false;
			}
			_field.text = a_str;
			out = _field.text;
		}
		_cache[a_str] = out;
		return out;
	}

	/* Translates a key whose value holds {0}, {1} ... placeholders and substitutes a_args. */
	public static function format(a_key: String, a_args: Array): String
	{
		var s: String = tr(a_key);
		if (a_args == undefined)
			return s;
		for (var i: Number = 0; i < a_args.length; i++)
			s = s.split("{" + i + "}").join(String(a_args[i]));
		return s;
	}
}
