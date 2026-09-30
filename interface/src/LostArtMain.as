/*
 * Entry point (MTASC -main). Builds _root.Menu_mc as an instance of LostArtSpellmakingMenu.
 *
 * The movie has no library symbols, so Object.registerClass cannot be used; instead the empty
 * clip's prototype chain is pointed at the class and the constructor is run on it (the usual
 * way to bind an AS2 class to a runtime-created clip).
 *
 * Dev mode (_root.la_dev == "1", set through FlashVars by the offline harness): device fonts
 * instead of Skyrim's fontconfig fonts, plus an ExternalInterface bridge the harness uses to
 * play the DLL's part. In game there are no FlashVars, so none of this is active.
 */
import flash.external.ExternalInterface;
import lostart.Theme;
import lostart.util.Translator;

class LostArtMain
{
	public static function main(a_root: MovieClip): Void
	{
		var dev: Boolean = a_root["la_dev"] == "1";
		if (dev)
			Theme.embedFonts = false;

		Stage.scaleMode = "noScale";
		Stage.align = "TL";

		var mc: MovieClip = a_root.createEmptyMovieClip("Menu_mc", 100);
		mc["__proto__"] = LostArtSpellmakingMenu.prototype;
		var ctor: Function = _global["LostArtSpellmakingMenu"];
		ctor.call(mc);
		var menu: LostArtSpellmakingMenu = LostArtSpellmakingMenu(mc);
		menu.devMode = dev;

		if (dev)
			installDevBridge(menu);   // the harness calls LA_DevStart(translations) to init
		else
			menu.init();
	}

	private static function installDevBridge(a_menu: LostArtSpellmakingMenu): Void
	{
		// harness -> SWF: invoke a Menu_mc function the way the DLL's Invoke would
		ExternalInterface.addCallback("LA_Invoke", null, function(a_fn: String, a_arg0: Object, a_arg1: Object, a_arg2: Object): Void {
			var fn: Function = a_menu[a_fn];
			if (fn != undefined)
				fn.call(a_menu, a_arg0, a_arg1, a_arg2);
		});
		// harness -> SWF: English strings from the translation fragment, then start the menu
		ExternalInterface.addCallback("LA_DevStart", null, function(a_dict: Object): Void {
			if (a_dict != undefined)
				Translator.setDictionary(a_dict);
			a_menu.init();
		});
	}
}
