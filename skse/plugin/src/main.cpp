// LostArt.dll -- SKSE entry point (placeholder).
//
// Builds against CommonLibSSE-NG for Skyrim SE 1.5.97 and AE 1.6.x ("flatrim") in one DLL:
//   * version independence through Address Library (no hard-coded runtime versions),
//   * struct layout independence: CommonLib-NG resolves the pre/post-1.6.629 layouts at
//     runtime, so the plugin is flagged StructCompatibility::Independent (the NG equivalent of
//     "uses up-to-date structs" that also keeps AE < 1.6.629 working).
// SKSEPluginInfo emits both SKSEPlugin_Version (read by AE SKSE) and SKSEPlugin_Query (read by
// SE SKSE 2.0.x).

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <memory>
#include <string_view>

using namespace std::literals;

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
	// Documents/My Games/Skyrim Special Edition[ GOG]/SKSE/LostArt.log
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

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			// All ESP/ESM data is loaded: the place to resolve LostArt.esp forms (formmap.json).
			SKSE::log::info("kDataLoaded received"sv);
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = false });
	InitializeLogging();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	SKSE::log::info("{} v{} loading (runtime {})"sv,
		plugin->GetName(), plugin->GetVersion().string("."sv), a_skse->RuntimeVersion().string("."sv));

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::critical("failed to register the SKSE messaging listener"sv);
		return false;
	}

	SKSE::log::info("{} loaded"sv, plugin->GetName());
	return true;
}
