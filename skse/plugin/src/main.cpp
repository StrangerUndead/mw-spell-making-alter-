// LostArt.dll -- SKSE entry point.
//
// Builds against CommonLibSSE-NG for Skyrim SE 1.5.97 and AE 1.6.x ("flatrim") in one DLL:
//   * version independence through Address Library (no hard-coded runtime versions),
//   * struct layout independence: CommonLib-NG resolves the pre/post-1.6.629 layouts at
//     runtime, so the plugin is flagged StructCompatibility::Independent (the NG equivalent of
//     "uses up-to-date structs" that also keeps AE < 1.6.629 working).
// SKSEPluginInfo emits both SKSEPlugin_Version (read by AE SKSE) and SKSEPlugin_Query (read by
// SE SKSE 2.0.x).
//
// Start-up order (OUTLINE "Load and save sequence"):
//   SKSEPluginLoad  logging, trampoline, messaging listener, Papyrus natives, co-save callbacks
//   kDataLoaded     data files + records (Core), then Services, UI, Casting, Effects, Console
//   kPreLoadGame    early restore of the save's custom spells (Persistence)
//   kPostLoadGame   post-load checks, hand art, Effects::OnGameLoaded
//   kNewGame        clean spellbook

#include "Casting/CastRouter.h"
#include "Console/Commands.h"
#include "Core/Persistence.h"
#include "Core/Spellbook.h"
#include "Core/State.h"
#include "Effects/EffectSystems.h"
#include "Papyrus/Natives.h"
#include "Services/Services.h"
#include "Tests/InGameTests.h"
#include "UI/SpellmakingMenu.h"

#ifndef LOSTART_VERSION_MAJOR
#	define LOSTART_VERSION_MAJOR 1
#	define LOSTART_VERSION_MINOR 0
#	define LOSTART_VERSION_PATCH 0
#endif

SKSEPluginInfo(
	.Version = REL::Version{ LOSTART_VERSION_MAJOR, LOSTART_VERSION_MINOR, LOSTART_VERSION_PATCH, 0 },
	.Name = "LostArt"sv,
	.Author = "Lost Art of Spellmaking"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary);

namespace
{
	// SKSE's default branch trampoline (SKSE::GetTrampoline()), allocated in SKSE::Init. No hook
	// uses it today: the vtable hooks (Casting, Effects' frame / AddTarget hooks) need none, and the
	// two write_call sites own named trampolines (Effects/Movement.cpp 64 B for 2 calls,
	// Effects/Combat.cpp 32 B for 1 call; a write_call<5> takes 14 bytes). Kept as headroom.
	constexpr std::size_t kTrampolineSize = 1 << 10;

	// Documents/My Games/Skyrim Special Edition[ GOG]/SKSE/LostArt.log. Starts at info so the
	// load summary is always written; iLogLevel applies once the settings are read (kDataLoaded).
	void InitializeLogging()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("LostArt: unable to locate the SKSE log directory"sv);
		}
		*path /= "LostArt.log"sv;

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
#ifndef NDEBUG
		logger->set_level(spdlog::level::debug);
		logger->flush_on(spdlog::level::debug);
#else
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);
#endif
		spdlog::set_default_logger(std::move(logger));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v"s);
	}

	// LostArt_SettingsChanged is sent by the MCM script's OnSettingChange (CONTRACTS section 5).
	class SettingsEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		static SettingsEventSink* GetSingleton()
		{
			static SettingsEventSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (a_event && a_event->eventName == "LostArt_SettingsChanged"sv) {
				LA::RunOnMainThread([] { LA::State::Get().ReloadSettings(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void OnDataLoaded()
	{
		if (!LA::LoadData()) {
			// Without slot records nothing can be compiled; keep the game running and say why.
			RE::DebugMessageBox("Lost Art of Spellmaking: LostArt_Slots.esp or LostArt.esp is not active. The mod is disabled; see "
								"SKSE/LostArt.log.");
			return;
		}
		LA::Services::Install();
		LA::UI::RegisterSpellmakingMenu();
		LA::UI::InstallMagicMenuExtensions();
		LA::Casting::Install();
		LA::Effects::Install();
		LA::Console::Install();
		if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
			modEvents->AddEventSink(SettingsEventSink::GetSingleton());
		}
		logger::info("all components installed");
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			// data: the save's file name (char*), dataLen its length.
			LA::Persistence::OnPreLoadGame(static_cast<const char*>(a_msg->data), a_msg->dataLen);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// data: (void*)(bool) whether the load succeeded.
			LA::Persistence::OnPostLoadGame(a_msg->data != nullptr);
			break;
		case SKSE::MessagingInterface::kNewGame:
			LA::Persistence::OnNewGame();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = kTrampolineSize });
	InitializeLogging();
	LA::State::MarkMainThread();  // SKSEPluginLoad runs on the game's main thread

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} v{} loading (runtime {})"sv, plugin->GetName(), plugin->GetVersion().string("."sv),
		a_skse->RuntimeVersion().string("."sv));

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		logger::critical("failed to register the SKSE messaging listener"sv);
		return false;
	}
	if (const auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(LA::Papyrus::Register)) {
		logger::critical("failed to register the Papyrus natives"sv);
		return false;
	}
	if (!LA::Persistence::Install()) {
		return false;
	}

	logger::info("{} loaded"sv, plugin->GetName());
	return true;
}
