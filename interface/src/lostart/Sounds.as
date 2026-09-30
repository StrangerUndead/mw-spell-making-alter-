/*
 * UI sounds the SWF asks the DLL to play (LA_PlaySound [soundKey]). Values are vanilla
 * Skyrim.esm SoundDescriptor EditorIDs (checked against Mutagen.Bethesda.FormKeys.SkyrimSE):
 * the DLL can pass them straight to RE::PlaySound(editorId).
 *
 * The SWF only plays sounds for things it can see: navigation, opening panels, and confirmed
 * state changes (an effect appeared/disappeared in LA_SetState). Rule outcomes that end in a
 * message box get the error sound when the box opens. Create's success sound
 * (UIEnchantingItemCreate + UISpellLearned) is the DLL's, because only it knows it succeeded.
 */
class lostart.Sounds
{
	public static var FOCUS: String = "UIMenuFocus";         // selection moved
	public static var OK: String = "UIMenuOK";               // open editor / picker / confirm
	public static var CANCEL: String = "UIMenuCancel";       // back out of a panel
	public static var ERROR: String = "UIMenuCancel";        // message box (Morrowind error text)
	public static var TAB: String = "UIMenuPrevNext";        // school tab, pane switch, range cycle
	public static var ADD: String = "UIEnchantingLearnEffect"; // an effect row was added
	public static var REMOVE: String = "UIMenuPrevNext";     // an effect row was removed / moved
	public static var STEP: String = "UIMenuFocus";          // slider step (throttled)
}
