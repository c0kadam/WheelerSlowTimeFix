#include "Plugin.h"
#include "SlowTimeCompat.h"
#include "TimeTargetWatch.h"

namespace
{
	void InitializeLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Failed to find standard logging directory");
		}

		*path /= std::string(Plugin::NAME) + ".log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %T] [%^%l%$] %v");
		spdlog::info("[SlowTimeFix] logging to {}", path->string());
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message ? a_message->type : 0) {
		case SKSE::MessagingInterface::kDataLoaded:
			SlowTimeCompat::RefreshTrackedForms();
			TimeTargetWatch::QueueInstall();
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME);
	v.PluginVersion(Plugin::VERSION);
	v.AuthorName(Plugin::AUTHOR);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse(true);
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST_SE, SKSE::RUNTIME_SSE_LATEST_AE });
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	SKSE::Init(a_skse);

	SlowTimeCompat::InstallHooks();
	TimeTargetWatch::SetMainThreadID(::GetCurrentThreadId());
	TimeTargetWatch::QueueInstall();

	SKSE::log::info(
		"{} {} by {} loading (pluginVersion={})",
		Plugin::NAME,
		Plugin::VERSION_LABEL,
		Plugin::AUTHOR,
		Plugin::VERSION.string());

	if (auto* messaging = SKSE::GetMessagingInterface(); !messaging->RegisterListener("SKSE", MessageHandler)) {
		SKSE::log::critical("[SlowTimeFix] failed to register SKSE message listener");
		return false;
	}

	SKSE::log::info("[SlowTimeFix] queued early watchpoint install; DataLoaded will retry if needed");
	return true;
}
