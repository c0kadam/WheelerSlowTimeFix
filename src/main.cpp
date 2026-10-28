#include "Plugin.h"
#if WSTF_BOUNDED_DIAGNOSTICS
#include "OwnershipDiagnostics.h"
#endif

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

	std::string FormatVersion(const REL::Version& a_version)
	{
		auto text = a_version.string(".");
		if (a_version.build() == 0) {
			text.resize(text.rfind('.'));
		}
		return text;
	}

	void LogStartupVersions(const SKSE::LoadInterface* a_skse)
	{
		const auto runtime = a_skse->RuntimeVersion();
		// SKSE uses the same 8/8/12/4-bit packed format as REL::Version.
		const auto skseVersion = REL::Version::unpack(a_skse->SKSEVersion());
		const auto family = runtime.major() == 1 && runtime.minor() == 5 ? "SE" :
		                    runtime.major() == 1 && runtime.minor() >= 6 ? "AE" : "Unknown";

		SKSE::log::info("{} {}", Plugin::NAME, FormatVersion(Plugin::VERSION));
		SKSE::log::info("Skyrim runtime: {}", FormatVersion(runtime));
		SKSE::log::info("SKSE version: {}", FormatVersion(skseVersion));
		SKSE::log::info("Runtime family: {}", family);
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message ? a_message->type : 0) {
		case SKSE::MessagingInterface::kDataLoaded:
			SlowTimeCompat::RefreshTrackedForms();
			TimeTargetWatch::QueueInstall();
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				spdlog::default_logger()->flush();
			}
#endif

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
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_1_5_97, SKSE::RUNTIME_SSE_1_6_1170, SKSE::RUNTIME_SSE_1_7_104 });
	return v;
}();

extern "C" DLLEXPORT bool __cdecl SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	if (!a_skse || !a_info) {
		return false;
	}

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION.pack();
	if (a_skse->IsEditor()) {
		return false;
	}

	const auto runtime = a_skse->RuntimeVersion().pack();
	const auto& compatible = SKSEPlugin_Version.compatibleVersions;
	return runtime != 0 && std::find(std::begin(compatible), std::end(compatible), runtime) != std::end(compatible);
}

extern "C" DLLEXPORT bool __cdecl SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	if (a_skse) {
		LogStartupVersions(a_skse);
	}
	// Keep InitializeLog's sink; CommonLib's default logger truncates the same file.
	SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });
#if WSTF_BOUNDED_DIAGNOSTICS
	OwnershipDiagnostics::Initialize();
#endif


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
#if WSTF_BOUNDED_DIAGNOSTICS
	if (OwnershipDiagnostics::IsEnabled()) {
		spdlog::default_logger()->flush();
	}
#endif

	return true;
}
