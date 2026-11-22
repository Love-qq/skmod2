// ---------------------------------------------------------------------------
// main.cpp  –  SKSE plugin entry point
// Skyrim SE 1.5.97 / SKSE64
// ---------------------------------------------------------------------------
#include "PCH.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <fstream>
#include <filesystem>
#include "Config.h"
#include "Hooks.h"
#include "Diagnostics.h"

namespace
{
    void SetupLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Unable to determine SKSE log directory.");
        }
        *path /= "CastSprint.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log  = std::make_shared<spdlog::logger>("CastSprint", std::move(sink));

        // Diagnostics: flush every line immediately to disk
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
    }

    bool VerifyExe()
    {
        wchar_t exePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
            logger::warn("[Main] Could not get executable path");
            return true;
        }
        logger::info("[Main] Game executable: {}", std::filesystem::path(exePath).string());
        return true;
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                logger::info("========================================");
                logger::info("CastSprint v9 (CastSprint_v9)");
                logger::info("========================================");
                logger::info("[Main] kDataLoaded received – installing hooks.");
                CastSprint::InstallHooks();
                break;

            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                logger::info("[Main] Load/new game message {} - animation sink will be (re)registered by the frame watcher.", static_cast<int>(a_msg->type));
                CastSprint::Diagnostics::TryRegisterAnimEventSink();
                break;

            default:
                break;
        }
    }
}

// ── SKSE plugin query ──────────────────────────────────────────────────────
extern "C" __declspec(dllexport)
bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    SetupLog();

    logger::info("CastSprint v{}.{}.{} – queried by SKSE",
                 CASTSPRINT_VERSION_MAJOR,
                 CASTSPRINT_VERSION_MINOR,
                 CASTSPRINT_VERSION_PATCH);

    if (a_skse->IsEditor()) {
        logger::critical("Loaded in editor – aborting.");
        return false;
    }

    if (a_skse->RuntimeVersion() != SKSE::RUNTIME_SSE_1_5_97) {
        logger::critical("Unsupported runtime version {}. This plugin requires 1.5.97.",
                         a_skse->RuntimeVersion().string());
        return false;
    }

    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name        = "CastSprint";
    a_info->version     = CASTSPRINT_VERSION_MAJOR;

    return true;
}

// ── SKSE plugin load ───────────────────────────────────────────────────────
extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    logger::info("[Main] SKSEPlugin_Load begin.");

    if (!VerifyExe()) {
        logger::critical("[Main] Executable verification failed.");
        return false;
    }

    SKSE::Init(a_skse);

    // Load configuration.
    CastSprint::Config::GetSingleton().Load();

    // Register messaging callback - hooks will be installed at kDataLoaded
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::error("[Main] Failed to register messaging listener.");
        return false;
    }

    // Allocate trampoline memory
    try {
        SKSE::GetTrampoline().create(256);
    }
    catch (const std::exception& e) {
        logger::error("[Main] Trampoline allocation failed: {}", e.what());
        return false;
    }

    logger::info("[Main] SKSEPlugin_Load complete – hooks will install at kDataLoaded.");
    return true;
}
