#include "UI/SpellmakingMenu.h"

#include "Core/Spellbook.h"
#include "UI/MenuModel.h"

// Magic-menu extensions (OUTLINE "Spell management"):
//
// (a) Accurate item cards. Approach: wrap the movie's UpdateItemCardInfo.
//     The engine hands the item card data to the SWF by calling the AS function
//     Menu_mc.UpdateItemCardInfo(obj): SkyUI's ItemMenu registers it with
//     GameDelegate.addCallBack("UpdateItemCardInfo", this, "UpdateItemCardInfo") and asks for it
//     with GameDelegate.call("RequestItemCardInfo", [], this, "UpdateItemCardInfo") on every
//     highlight change (SkyUI src/ItemMenus/ItemMenu.as). Both paths look the function up *by
//     name on the instance* at call time, so replacing Menu_mc.UpdateItemCardInfo with a native
//     GFx function (after the menu opened) intercepts every card update without an engine hook.
//     The wrapper rewrites obj.effects (the HTML text ItemCard.as puts in MagicEffectsLabel for
//     ICT_SPELL) with Morrowind-style lines when the selected spell is custom, then calls the
//     original. The selection comes from the engine's own list (MagicMenu::itemList
//     ->GetSelectedItem()->data.baseForm), so it works with vanilla and SkyUI movies alike.
//     Chosen over hooking ItemCard::SetForm (non-virtual, several call sites per runtime) and
//     over per-frame polling of the card clip (fragile against SkyUI layout changes).
//
// (b) Delete hotkey. An InputEvent sink active only while MagicMenu is open: Shift+LMB
//     (Morrowind's gesture), Delete, or the gamepad left-stick click delete the selected spell
//     after sQuestionDeleteSpell; protected spells answer sDeleteSpellError.

namespace LA::UI
{
	namespace
	{
		constexpr const char* kOrigMember = "LA_UpdateItemCardInfo_orig";

		std::atomic<bool> g_magicOpen{ false };

		RE::SpellItem* SelectedSpell()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return nullptr;
			}
			auto menu = ui->GetMenu<RE::MagicMenu>();
			if (!menu) {
				return nullptr;
			}
			if (auto* list = menu->GetRuntimeData().itemList) {
				if (auto* item = list->GetSelectedItem(); item && item->data.baseForm) {
					return item->data.baseForm->As<RE::SpellItem>();
				}
			}
			// Fallback: SkyUI extends entries with formId (skse.ExtendData(true)).
			if (auto movie = ui->GetMovieView(RE::MagicMenu::MENU_NAME)) {
				RE::GFxValue formId;
				if (movie->GetVariable(&formId, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId") && formId.IsNumber()) {
					if (auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(formId.GetNumber()))) {
						return form->As<RE::SpellItem>();
					}
				}
			}
			return nullptr;
		}

		std::string CardText(const CustomSpell& a_spell)
		{
			const auto  fmt = State::Get().Formatter();
			std::string text;
			for (const auto& effect : a_spell.def.effects) {
				if (!text.empty()) {
					text += "<br>";
				}
				text += EscapeHtml(fmt.Line(effect));
			}
			return text;
		}

		class ItemCardWrapper final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (a_params.argCount >= 1 && a_params.args[0].IsObject()) {
					if (auto* spell = SelectedSpell()) {
						if (const auto* custom = State::Get().FindBySpell(spell)) {
							const auto text = CardText(*custom);
							RE::GFxValue effects;
							a_params.movie->CreateString(&effects, text.c_str());
							a_params.args[0].SetMember("effects", effects);
						}
					}
				}
				if (a_params.thisPtr && a_params.thisPtr->IsObject()) {
					a_params.thisPtr->Invoke(kOrigMember, a_params.retVal, a_params.args, a_params.argCount);
				}
			}
		};

		void InstallItemCardWrapper()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(RE::MagicMenu::MENU_NAME) : nullptr;
			if (!movie) {
				return;
			}
			RE::GFxValue menu;
			if (!movie->GetVariable(&menu, "_root.Menu_mc") || !menu.IsObject()) {
				return;
			}
			RE::GFxValue existing;
			if (menu.GetMember(kOrigMember, &existing) && !existing.IsUndefined()) {
				return;  // already wrapped (menu instance reused)
			}
			// VERIFY(in-game): vanilla (non-SkyUI) MagicMenu.swf also routes card updates through
			// Menu_mc.UpdateItemCardInfo by name; SkyUI does (ItemMenu.as).
			RE::GFxValue original;
			if (!menu.GetMember("UpdateItemCardInfo", &original) || original.IsUndefined() || original.IsNull()) {
				logger::warn("magic menu: Menu_mc.UpdateItemCardInfo not found; custom item cards disabled"sv);
				return;
			}
			static RE::GPtr<ItemCardWrapper> handler{ new ItemCardWrapper() };
			RE::GFxValue                     wrapper;
			movie->CreateFunction(&wrapper, handler.get());
			menu.SetMember(kOrigMember, original);
			menu.SetMember("UpdateItemCardInfo", wrapper);
			logger::debug("magic menu: item card wrapper installed"sv);
		}

		void RefreshMagicMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			if (auto menu = ui->GetMenu<RE::MagicMenu>()) {
				if (auto* list = menu->GetRuntimeData().itemList) {
					list->Update();  // engine re-population from the player's spells (MagicItemList::Update)
					return;
				}
			}
			if (auto movie = ui->GetMovieView(RE::MagicMenu::MENU_NAME)) {
				movie->Invoke("_root.Menu_mc.inventoryLists.InvalidateListData", nullptr, nullptr, 0);  // SkyUI
			}
		}

		std::string GameSettingString(const char* a_name, const char* a_fallback)
		{
			if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
				if (auto* setting = settings->GetSetting(a_name); setting && setting->GetString()) {
					return setting->GetString();
				}
			}
			return a_fallback;
		}

		// Yes/No confirmation; the callback receives the button index.
		class DeleteConfirm final : public RE::IMessageBoxCallback
		{
		public:
			explicit DeleteConfirm(RE::FormID a_spell) :
				_spell(a_spell)
			{}

			void Run(std::uint8_t a_button) override
			{
				if (a_button != 0) {  // buttonPressOffset 0: 0 = Yes
					return;
				}
				const auto formId = _spell;
				RunOnMainThread([formId]() {
					auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
					if (spell && Spellbook::DeleteAny(spell)) {
						logger::info("magic menu: deleted spell {:08X}"sv, formId);
					}
					SKSE::GetTaskInterface()->AddUITask([]() { RefreshMagicMenu(); });
				});
			}

		private:
			RE::FormID _spell;
		};

		void RequestDelete()
		{
			if (!g_magicOpen) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
				return;
			}
			auto* spell = SelectedSpell();
			if (!spell) {
				return;
			}
			const auto& state = State::Get();
			const auto  check = Spellbook::CanDelete(spell);
			if (check != Spellbook::DeleteCheck::kOk) {
				ShowMessage(state.strings.Get("$LA_Msg_sDeleteSpellError"));
				PlayUISound("UIMenuCancel");
				return;
			}
			const char* name = spell->GetName();
			const auto  text = SubstituteName(state.strings.Get("$LA_Msg_sQuestionDeleteSpell"), name ? name : "");

			auto* factory = RE::MessageDataFactoryManager::GetSingleton();
			auto* strings = RE::InterfaceStrings::GetSingleton();
			auto* creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
			auto* box = creator ? creator->Create() : nullptr;
			if (!box) {
				return;
			}
			box->bodyText = text;
			box->buttonText.push_back(GameSettingString("sYes", "Yes").c_str());
			box->buttonText.push_back(GameSettingString("sNo", "No").c_str());
			// VERIFY(in-game): the factory-created box has buttonPressOffset 0 (button 0 = Yes).
			box->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new DeleteConfirm(spell->GetFormID()));
			RE::MessageBoxMenu::QueueMessage(box);
		}

		class MagicMenuWatcher final :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
			public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static MagicMenuWatcher* Get()
			{
				static MagicMenuWatcher instance;
				return &instance;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->menuName == RE::MagicMenu::MENU_NAME) {
					g_magicOpen = a_event->opening;
					_shift = false;
					if (a_event->opening) {
						SKSE::GetTaskInterface()->AddUITask([]() { InstallItemCardWrapper(); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!g_magicOpen || !a_events) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* event = *a_events; event; event = event->next) {
					const auto* button = event->AsButtonEvent();
					if (!button) {
						continue;
					}
					const auto code = button->GetIDCode();
					switch (button->GetDevice()) {
					case RE::INPUT_DEVICE::kKeyboard:
						if (code == RE::BSKeyboardDevice::Keys::kLeftShift || code == RE::BSKeyboardDevice::Keys::kRightShift) {
							_shift = button->IsPressed();
						} else if (code == RE::BSKeyboardDevice::Keys::kDelete && button->IsDown() && !TextEntryActive()) {
							Queue();
						}
						break;
					case RE::INPUT_DEVICE::kMouse:
						if (code == RE::BSWin32MouseDevice::Keys::kLeftButton && button->IsDown() && _shift) {
							Queue();
						}
						break;
					case RE::INPUT_DEVICE::kGamepad:
						if (code == RE::BSWin32GamepadDevice::Keys::kLeftThumb && button->IsDown()) {
							Queue();
						}
						break;
					default:
						break;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			static bool TextEntryActive()
			{
				// SkyUI's search field enables text input (skse.AllowTextInput -> ControlMap).
				auto* controls = RE::ControlMap::GetSingleton();
				return controls && controls->GetRuntimeData().textEntryCount > 0;
			}

			static void Queue()
			{
				// Deferred to the UI task queue so the movie has processed this click (hover
				// selection) before we read the selected entry.
				SKSE::GetTaskInterface()->AddUITask([]() { RequestDelete(); });
			}

			bool _shift{ false };
		};
	}

	void InstallMagicMenuExtensions()
	{
		auto* watcher = MagicMenuWatcher::Get();
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(watcher);
		}
		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(watcher);
		}
		RegisterParityTests();
		logger::info("magic menu extensions installed (item cards, delete hotkey)"sv);
	}
}
