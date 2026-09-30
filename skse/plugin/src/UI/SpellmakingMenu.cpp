#include "UI/SpellmakingMenu.h"

#include "Services/Altars.h"
#include "Services/Services.h"
#include "UI/MenuModel.h"

// Scaleform host of the spellmaking menu (docs/dev/CONTRACTS.md section 7).
//
// Registration and movie loading follow the usual CommonLib custom-menu pattern (QuickLoot /
// ConsoleUtil style: RE::UI::Register + BSScaleformManager::LoadMovieEx in the constructor).
// Unlike a vanilla menu, LoadMovieEx does not install an FxDelegate (see LoadMovie_Impl in
// CommonLib's BSScaleformManager.cpp, which is what vanilla menus use), so the delegate is
// created here and set as the kExternalInterface state on the movie *definition* inside the
// LoadMovieEx callback: the definition's state bag is the parent of the view's, and the view
// executes frame 1 (our LA_Ready) inside LoadMovieEx, before we could set it on the view.
//
// Flags: the vanilla crafting menus use kUsesMenuContext | kDisablePauseMenu | kUpdateUsesCursor
// (CraftingMenu.h); the outline asks for a menu that also pauses the game and shows the cursor
// (kPausesGame, kUsesCursor; IMenu::RefreshPlatform drops the cursor on gamepad because of
// kUpdateUsesCursor), and kModal as docs/dev/MENU.md asks (lower movies stop advancing).
// kRequiresUpdate keeps AdvanceMovie running for the delayed close.
// depthPriority 3 matches LockpickingMenu/TrainingMenu, below MessageBoxMenu (10).

namespace LA::UI
{
	namespace
	{
		using namespace std::chrono_literals;

		constexpr auto kCloseDelay = 220ms;  // SWF fade-out is 150 ms (LA_Close)

		struct Pending
		{
			Provider            provider;
			RE::ObjectRefHandle ref;
			bool                valid{ false };
		};

		std::mutex                    g_mutex;
		Pending                       g_pending;
		std::weak_ptr<MenuController> g_active;

		// Val -> GFxValue (managed strings: GFxValue(const char*) only stores the pointer).
		void ToGFx(RE::GFxMovieView* a_movie, const Val& a_val, RE::GFxValue& a_out)
		{
			switch (a_val.Kind()) {
			case Val::Type::kUndefined:
				a_out.SetUndefined();
				break;
			case Val::Type::kNull:
				a_out.SetNull();
				break;
			case Val::Type::kBool:
				a_out.SetBoolean(a_val.AsBool());
				break;
			case Val::Type::kNumber:
				a_out.SetNumber(a_val.AsNum());
				break;
			case Val::Type::kString:
				a_movie->CreateString(&a_out, a_val.AsStr().c_str());
				break;
			case Val::Type::kArray:
				a_movie->CreateArray(&a_out);
				for (const auto& item : a_val.Items()) {
					RE::GFxValue element;
					ToGFx(a_movie, item, element);
					a_out.PushBack(element);
				}
				break;
			case Val::Type::kObject:
				a_movie->CreateObject(&a_out);
				for (const auto& [key, value] : a_val.Members()) {
					RE::GFxValue member;
					ToGFx(a_movie, value, member);
					a_out.SetMember(key.c_str(), member);
				}
				break;
			}
		}

		Val FromGFx(const RE::GFxValue& a_value)
		{
			if (a_value.IsBool()) {
				return Val::Bool(a_value.GetBool());
			}
			if (a_value.IsNumber()) {
				return Val::Num(a_value.GetNumber());
			}
			if (a_value.IsString()) {
				const char* text = a_value.GetString();
				return Val::Str(text ? text : "");
			}
			if (a_value.IsNull()) {
				return Val::Null();
			}
			return Val();
		}

		class SpellmakingMenu;

		// DLL -> SWF: Invoke on _root.Menu_mc.
		class MovieSink final : public MenuSink
		{
		public:
			explicit MovieSink(SpellmakingMenu* a_menu) :
				_menu(a_menu)
			{}

			void SetKnown(const Val& a_known) override { Invoke("LA_SetKnown", &a_known); }
			void SetState(const Val& a_state) override { Invoke("LA_SetState", &a_state); }
			void SetLoadList(const Val& a_list) override { Invoke("LA_SetLoadList", &a_list); }
			void ShowMessage(const std::string& a_text) override
			{
				const auto text = Val::Str(a_text);
				Invoke("LA_ShowMessage", &text);
				// The SWF asks for the UI error sound itself when its message box opens
				// (lostart.Sounds.ERROR through LA_PlaySound), so none is played here.
			}
			void Close() override;
			void PlaySound(const std::string& a_soundKey) override
			{
				if (!a_soundKey.empty()) {
					PlayUISound(a_soundKey.c_str());
				}
			}

		private:
			void Invoke(const char* a_function, const Val* a_arg);

			SpellmakingMenu* _menu;
		};

		class SpellmakingMenu final : public RE::IMenu
		{
		public:
			static RE::IMenu* Create() { return new SpellmakingMenu(); }

			SpellmakingMenu() :
				_sink(this)
			{
				using Flag = RE::UI_MENU_FLAGS;
				depthPriority = 3;
				menuFlags.set(Flag::kPausesGame, Flag::kUsesCursor, Flag::kUsesMenuContext, Flag::kModal, Flag::kDisablePauseMenu,
					Flag::kUpdateUsesCursor, Flag::kRequiresUpdate, Flag::kDontHideCursorWhenTopmost);
				inputContext = Context::kMenuMode;

				const auto start = std::chrono::steady_clock::now();

				Pending pending;
				{
					std::scoped_lock lock(g_mutex);
					pending = std::exchange(g_pending, {});
				}
				if (!pending.valid) {
					pending.provider = Services::MakeProvider(nullptr);
				}
				_controller = std::make_shared<MenuController>(pending.provider, pending.ref, &_sink);
				_controller->Start();
				g_active = _controller;

				fxDelegate = RE::make_gptr<RE::FxDelegate>();
				fxDelegate->RegisterHandler(this);

				auto*      scaleform = RE::BSScaleformManager::GetSingleton();
				auto*      delegate = fxDelegate.get();
				const bool loaded = scaleform && scaleform->LoadMovieEx(this, kMoviePath, RE::GFxMovieView::ScaleModeType::kShowAll,
					[delegate](RE::GFxMovieDef* a_def) {
						// VERIFY(in-game): the view inherits this state, so LA_Ready (frame 1) reaches us.
						a_def->SetState(RE::GFxState::StateType::kExternalInterface, delegate);
					});
				if (!loaded || !uiMovie) {
					logger::error("spellmaking menu: could not load Interface/{}.swf"sv, kMoviePath);
					// Queued behind the kShow being processed; without a movie nothing else would
					// ever close this game-pausing menu.
					CloseSpellmakingMenu();
				} else {
					uiMovie->SetState(RE::GFxState::StateType::kExternalInterface, delegate);
				}

				logger::debug("spellmaking menu: opened in {:.2f} ms (provider '{}')"sv,
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), pending.provider.name);
			}

			~SpellmakingMenu() override
			{
				if (_controller) {
					_controller->Detach();
				}
			}

			void Accept(CallbackProcessor* a_processor) override
			{
				// One captureless function per protocol call (FxDelegate callbacks carry no name).
#define LA_CALL(name) a_processor->Process(name, [](const RE::FxDelegateArgs& a_args) { Dispatch(name, a_args); })
				LA_CALL("LA_Ready");
				LA_CALL("LA_SetName");
				LA_CALL("LA_AddEffect");
				LA_CALL("LA_PickTarget");
				LA_CALL("LA_EditEffect");
				LA_CALL("LA_RemoveEffect");
				LA_CALL("LA_MoveEffect");
				LA_CALL("LA_EditorRange");
				LA_CALL("LA_EditorSet");
				LA_CALL("LA_EditorStep");
				LA_CALL("LA_EditorOk");
				LA_CALL("LA_EditorCancel");
				LA_CALL("LA_EditorDelete");
				LA_CALL("LA_Clear");
				LA_CALL("LA_LoadList");
				LA_CALL("LA_Load");
				LA_CALL("LA_Create");
				LA_CALL("LA_ToggleCostMath");
				LA_CALL("LA_Exit");
				LA_CALL("LA_PlaySound");
				LA_CALL("LA_RequestKeyboard");
#undef LA_CALL
			}

			RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override
			{
				using Type = RE::UI_MESSAGE_TYPE;
				switch (*a_message.type) {
				case Type::kShow:
				case Type::kReshow:
					return RE::UI_MESSAGE_RESULTS::kHandled;
				case Type::kHide:
				case Type::kForceHide:
					OnClosing();
					return RE::UI_MESSAGE_RESULTS::kHandled;
				default:
					return RE::IMenu::ProcessMessage(a_message);
				}
			}

			void AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override
			{
				if (_readyPending && _controller) {
					// LA_Ready arrives while the movie runs frame 1 (inside LoadMovieEx); answering
					// on the next advance keeps our Invokes out of that re-entrant call.
					_readyPending = false;
					_ready = true;
					_controller->HandleIntent("LA_Ready", {});
				}
				++_frames;
				if (!_ready && _frames == 30) {
					logger::error("spellmaking menu: the movie never called LA_Ready (ExternalInterface not reached?)"sv);
				}
				if (_closeAt && std::chrono::steady_clock::now() >= *_closeAt) {
					_closeAt.reset();
					CloseSpellmakingMenu();
				}
				// Like the vanilla IMenu::AdvanceMovie (CurrentTime variable, then Advance), but
				// with the frame interval as GFx's delta time in seconds. CommonLib's
				// reimplementation passes a_currentTime as the delta. VERIFY(in-game): the SWF
				// tweens (150 ms fades, cost count-up) run at real speed.
				if (uiMovie) {
					const RE::GFxValue now(static_cast<double>(a_currentTime));
					uiMovie->SetVariable("CurrentTime", now, RE::GFxMovie::SetVarType::kNormal);
					uiMovie->Advance(a_interval);
				}
			}

			void OnCall(std::string_view a_name, const RE::FxDelegateArgs& a_args)
			{
				if (!_controller) {
					return;
				}
				if (a_name == "LA_Ready") {
					_readyPending = true;
					return;
				}
				std::vector<Val> args;
				args.reserve(a_args.GetArgCount());
				for (std::uint32_t i = 0; i < a_args.GetArgCount(); ++i) {
					args.push_back(FromGFx(a_args[i]));
				}
				_controller->HandleIntent(a_name, args);
			}

			void RequestClose()
			{
				if (!_closeAt) {
					_closeAt = std::chrono::steady_clock::now() + kCloseDelay;
				}
			}

			RE::GFxMovieView* Movie() const { return uiMovie.get(); }

		private:
			static void Dispatch(const char* a_name, const RE::FxDelegateArgs& a_args)
			{
				// GetHandler() is the FxDelegateHandler registered above: this menu.
				auto* menu = static_cast<SpellmakingMenu*>(a_args.GetHandler());
				if (menu) {
					menu->OnCall(a_name, a_args);
				}
			}

			void OnClosing()
			{
				if (_closed) {
					return;
				}
				_closed = true;
				RE::ObjectRefHandle provider;
				bool                altar = false;
				if (_controller) {
					provider = _controller->ProviderRef();
					altar = _controller->GetProvider().kind == Provider::Kind::kAltar;
					_controller->Detach();
				}
				// The delegate's callback table holds a GPtr to this menu (AddCallbackVisitor), a
				// cycle through our own fxDelegate member: break it so the menu is destroyed.
				if (fxDelegate) {
					fxDelegate->UnregisterHandler(this);
				}
				// OUTLINE "Altars": closing the menu stands the player up.
				if (altar) {
					RunOnMainThread([provider]() { Services::StandUpFromAltar(provider.get().get()); });
				}
			}

			MovieSink                                           _sink;
			std::shared_ptr<MenuController>                     _controller;
			std::optional<std::chrono::steady_clock::time_point> _closeAt;
			std::uint32_t                                       _frames{ 0 };
			bool                                                _readyPending{ false };
			bool                                                _ready{ false };
			bool                                                _closed{ false };
		};

		void MovieSink::Invoke(const char* a_function, const Val* a_arg)
		{
			auto* movie = _menu ? _menu->Movie() : nullptr;
			if (!movie) {
				return;
			}
			const std::string path = std::string("_root.Menu_mc.") + a_function;
			if (a_arg) {
				RE::GFxValue arg;
				ToGFx(movie, *a_arg, arg);
				movie->Invoke(path.c_str(), nullptr, &arg, 1);
			} else {
				movie->Invoke(path.c_str(), nullptr, nullptr, 0);
			}
		}

		void MovieSink::Close()
		{
			Invoke("LA_Close", nullptr);  // play the fade-out; the menu closes after kCloseDelay
			if (_menu) {
				_menu->RequestClose();
			}
		}
	}

	void RegisterSpellmakingMenu()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->Register(kMenuName, SpellmakingMenu::Create);
			logger::info("registered {} (Interface/{}.swf)"sv, kMenuName, kMoviePath);
		}
		RegisterParityTests();
	}

	void OpenSpellmakingMenu(const Provider& a_provider, RE::TESObjectREFR* a_providerRef)
	{
		if (IsSpellmakingMenuOpen()) {
			logger::debug("spellmaking menu: already open"sv);
			return;
		}
		if (!State::Get().dataReady) {
			logger::warn("spellmaking menu: data not ready, not opening"sv);
			return;
		}
		{
			std::scoped_lock lock(g_mutex);
			g_pending.provider = a_provider;
			g_pending.ref = a_providerRef ? a_providerRef->CreateRefHandle() : RE::ObjectRefHandle();
			g_pending.valid = true;
		}
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
	}

	bool IsSpellmakingMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(kMenuName);
	}

	void CloseSpellmakingMenu()
	{
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}

	std::shared_ptr<MenuController> ActiveController()
	{
		return g_active.lock();
	}

	void ShowMessage(const std::string& a_text)
	{
		// The OStim / Devious Devices NG pattern: a MessageBoxData from the UI message factory,
		// queued on MessageBoxMenu (vanilla Debug.MessageBox builds the same object).
		auto* factory = RE::MessageDataFactoryManager::GetSingleton();
		auto* strings = RE::InterfaceStrings::GetSingleton();
		auto* creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
		auto* box = creator ? creator->Create() : nullptr;
		if (!box) {
			RE::DebugMessageBox(a_text.c_str());
			return;
		}
		box->bodyText = a_text;
		const char* ok = "OK";
		if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
			if (auto* setting = settings->GetSetting("sOk"); setting && setting->GetString()) {
				ok = setting->GetString();
			}
		}
		box->buttonText.push_back(ok);
		RE::MessageBoxMenu::QueueMessage(box);
	}

	void PlayUISound(const char* a_editorId)
	{
		if (a_editorId && *a_editorId) {
			RE::PlaySound(a_editorId);  // BSAudioManager lookup by SNDR EditorID, as vanilla UI sounds
		}
	}
}
