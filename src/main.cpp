#include "Compatibility.h"
#include "Hooks.h"
#include "ImGui/Renderer.h"
#include "Manager.h"
#include "Papyrus.h"
#include "SettingLoader.h"

void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		{
			logger::info("{:*^30}", "POST LOAD");
			ModAPIHandler::GetSingleton()->LoadModSettings();
			Hooks::Install();
		}
		break;
	case SKSE::MessagingInterface::kPostPostLoad:
		{
			logger::info("{:*^30}", "POST POST LOAD");
			ModAPIHandler::GetSingleton()->LoadAPIs();
			ImGui::Renderer::Connect();  // ImGuiVRHelper handshake (no-op on flat builds)
		}
		break;
	case SKSE::MessagingInterface::kDataLoaded:
		{
			logger::info("{:*^30}", "DATA LOADED");
			Manager::GetSingleton()->OnDataLoaded();
		}
		break;
	default:
		break;
	}
}

#if defined(SKYRIM_SUPPORT_VR) || defined(COMMONLIBSSE_NG)
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(Version::MAJOR);
	v.PluginName("FloatingSubtitles");
	v.AuthorName("powerofthree");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_1_5_97, SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_VR_1_4_15 });

	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "FloatingSubtitles";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver.major() == 1 && ver.minor() == 4) {
		if (ver < SKSE::RUNTIME_VR_1_4_15) {
			logger::critical(FMT_STRING("Unsupported VR runtime version {}"), ver.string());
			return false;
		}
	} else {
		if (ver < SKSE::RUNTIME_SSE_1_5_39) {
			logger::critical(FMT_STRING("Unsupported SSE runtime version {}"), ver.string());
			return false;
		}
	}

	return true;
}
#else
#ifdef SKYRIM_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(Version::MAJOR);
	v.PluginName("FloatingSubtitles");
	v.AuthorName("powerofthree");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });

	return v;
}();
#else
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "FloatingSubtitles";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
#ifdef SKYRIMVR
	if (ver < SKSE::RUNTIME_VR_1_4_15) {
		logger::critical(FMT_STRING("Unsupported VR runtime version {}"), ver.string());
		return false;
	}
#else
	if (ver < SKSE::RUNTIME_SSE_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}
#endif

	return true;
}
#endif
#endif

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	bool debugLog = false;
	CSimpleIniA ini;
	ini.SetUnicode();
	if (ini.LoadFile(L"Data/MCM/Settings/FloatingSubtitles.ini") >= SI_OK) {
		debugLog = ini.GetBoolValue("Settings", "bDebugLog", false);
	} else if (ini.LoadFile(L"Data/MCM/Config/FloatingSubtitles/settings.ini") >= SI_OK) {
		debugLog = ini.GetBoolValue("Settings", "bDebugLog", false);
	}

	auto level = debugLog ? spdlog::level::debug : spdlog::level::info;
	log->set_level(level);
	log->flush_on(level);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%H:%M:%S] %v");

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);
	if (debugLog) {
		logger::info("Debug logging enabled.");
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();

	logger::info("Game version : {}", a_skse->RuntimeVersion().string());

	SKSE::Init(a_skse, false);

	SKSE::AllocTrampoline(1 << 7);

	ImGui::Renderer::Install();

	const auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", OnInit);

	SKSE::GetPapyrusInterface()->Register(Papyrus::Register);

	return true;
}
