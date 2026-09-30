/*
 * Runtime TextField factory. All fields use Skyrim's fontconfig fonts ("$EverywhereFont" ...),
 * so no font is embedded in the SWF. Every string is resolved through Translator first and
 * then assigned with noTranslate, so player-typed names starting with '$' are never looked up.
 */
import lostart.Theme;
import lostart.util.Translator;

class lostart.util.Text
{
	public static function create(a_parent: MovieClip, a_name: String, a_depth: Number,
		a_x: Number, a_y: Number, a_w: Number, a_h: Number,
		a_size: Number, a_color: Number, a_font: String, a_align: String): TextField
	{
		a_parent.createTextField(a_name, a_depth, a_x, a_y, a_w, a_h);
		var tf: TextField = a_parent[a_name];
		tf.selectable = false;
		tf.multiline = false;
		tf.wordWrap = false;
		tf.embedFonts = Theme.embedFonts;
		tf["noTranslate"] = true;       // GFx extension: we translate ourselves
		tf["verticalAlign"] = "none";   // GFx extension (ignored elsewhere)
		tf.setNewTextFormat(format(a_size, a_color, a_font, a_align));
		tf.text = "";
		return tf;
	}

	public static function format(a_size: Number, a_color: Number, a_font: String, a_align: String): TextFormat
	{
		var fmt: TextFormat = new TextFormat();
		fmt.font = Theme.embedFonts ? (a_font == undefined ? Theme.FONT_REGULAR : a_font) : Theme.FALLBACK_FONT;
		fmt.size = a_size;
		fmt.color = a_color;
		fmt.align = a_align == undefined ? "left" : a_align;
		fmt.leading = 0;
		return fmt;
	}

	public static function multiline(a_tf: TextField): TextField
	{
		a_tf.multiline = true;
		a_tf.wordWrap = true;
		return a_tf;
	}

	/* Sets translated text; keeps the field's format. */
	public static function set(a_tf: TextField, a_str: String): Void
	{
		var s: String = Translator.tr(a_str);
		if (a_tf.text != s)
			a_tf.text = s;
	}

	public static function setColor(a_tf: TextField, a_color: Number): Void
	{
		if (a_tf.textColor != a_color)
			a_tf.textColor = a_color;
		var fmt: TextFormat = a_tf.getNewTextFormat();
		fmt.color = a_color;
		a_tf.setNewTextFormat(fmt);
	}

	/* Sets text and trims it with an ellipsis so it fits the field width (single line). */
	public static function setFit(a_tf: TextField, a_str: String): Void
	{
		var s: String = Translator.tr(a_str);
		a_tf.text = s;
		var max: Number = a_tf._width - 6;
		if (a_tf.textWidth <= max || s.length < 2)
			return;
		var lo: Number = 1;
		var hi: Number = s.length;
		while (lo < hi) {   // binary search the longest prefix that fits
			var mid: Number = Math.ceil((lo + hi) / 2);
			a_tf.text = s.substr(0, mid) + "...";
			if (a_tf.textWidth <= max)
				lo = mid;
			else
				hi = mid - 1;
		}
		a_tf.text = s.substr(0, lo) + "...";
	}

	/* Letter spacing (GFx/Flash 8 TextFormat.letterSpacing), kept for later text. */
	public static function spacing(a_tf: TextField, a_value: Number): TextField
	{
		var fmt: TextFormat = a_tf.getNewTextFormat();
		fmt["letterSpacing"] = a_value;
		a_tf.setNewTextFormat(fmt);
		a_tf.setTextFormat(fmt);
		return a_tf;
	}

	/* Sets translated text in upper case (Skyrim's labels and headers). */
	public static function setCaps(a_tf: TextField, a_str: String): Void
	{
		var s: String = Translator.tr(a_str).toUpperCase();
		if (a_tf.text != s)
			a_tf.text = s;
	}

	/* A grey upper-case, letter-spaced label (SkyUI column header / stat label). */
	public static function label(a_parent: MovieClip, a_name: String, a_depth: Number, a_w: Number, a_align: String): TextField
	{
		var tf: TextField = create(a_parent, a_name, a_depth, 0, 0, a_w, 24, Theme.FS_LABEL, Theme.TEXT_HINT, Theme.FONT_MEDIUM, a_align);
		return spacing(tf, Theme.LETTER_SPACING);
	}
}
