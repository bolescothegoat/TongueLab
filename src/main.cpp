#include "PCH.h"

#include "Config.h"
#include "Npc.h"
#include "Tongue.h"
#include "UI.h"

namespace
{
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "TongueLab.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v"s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Config::Load();
			Npc::Init();
			Tongue::Init();
			UI::Register();
		}
	}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v{};
	v.PluginVersion({ 1, 0, 1, 0 });
	v.PluginName("TongueLab");
	v.AuthorName("Bolescothegoat");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	// no CompatibleVersions list: an Address Library plugin loads on every AE runtime (1.6.x, 1.7.x)
	return v;
}();

// Skyrim SE 1.5.97 uses the old SKSE entry point instead of SKSEPlugin_Version
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "TongueLab";
	a_info->version = REL::Version(1, 0, 1, 0).pack();
	return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();
	SKSE::Init(a_skse);
	logger::info("TongueLab 1.0.1 loaded (runtime {})", REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
