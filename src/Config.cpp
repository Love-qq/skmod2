// ---------------------------------------------------------------------------
// Config.cpp  –  INI configuration loader implementation
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "Config.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <SimpleIni.h>

namespace CastSprint
{
    Config& Config::GetSingleton() noexcept
    {
        static Config instance;
        return instance;
    }

    void Config::Load()
    {
        // The DLL lives in Data/SKSE/Plugins/; build the .ini path beside it.
        const std::string iniPath = "Data/SKSE/Plugins/CastSprint.ini";

        CSimpleIniA ini;
        ini.SetUnicode(false);

        const auto rc = ini.LoadFile(iniPath.c_str());
        if (rc == SI_FILE) {
            logger::info("[Config] '{}' not found – using defaults and creating it.", iniPath);
        } else if (rc < SI_OK) {
            logger::warn("[Config] Failed to load '{}' (err {}). Using defaults.", iniPath, rc);
        }

        // ── Helper lambdas ──────────────────────────────────────────────────
        auto getBool = [&](const char* section, const char* key, bool def) -> bool {
            return ini.GetBoolValue(section, key, def);
        };
        auto getFloat = [&](const char* section, const char* key, double def) -> float {
            return static_cast<float>(ini.GetDoubleValue(section, key, def));
        };

        // ── Read values ─────────────────────────────────────────────────────
        bAllowSprintWhileCasting = getBool("General", "bAllowSprintWhileCasting", bAllowSprintWhileCasting);
        bRemoveCastSlowdown      = getBool("General", "bRemoveCastSlowdown",      bRemoveCastSlowdown);
        bApplyToNPCs             = getBool("General", "bApplyToNPCs",             bApplyToNPCs);
        fCastSpeedMult           = getFloat("General", "fCastSpeedMult",          fCastSpeedMult);

        // Clamp fCastSpeedMult to a reasonable range so the game doesn't break.
        if (fCastSpeedMult < 0.0f) fCastSpeedMult = 0.0f;
        if (fCastSpeedMult > 10.0f) fCastSpeedMult = 10.0f;

        // ── Write back defaults so the user has a template ──────────────────
        ini.SetBoolValue  ("General", "bAllowSprintWhileCasting", bAllowSprintWhileCasting,
            "; Prevent sprint cancellation when a spell starts charging.\n"
            "; Default: true");
        ini.SetBoolValue  ("General", "bRemoveCastSlowdown",      bRemoveCastSlowdown,
            "; Force run/sprint movement type while casting (removes cast slowdown).\n"
            "; Default: true");
        ini.SetBoolValue  ("General", "bApplyToNPCs",             bApplyToNPCs,
            "; Apply the same fix to NPCs (experimental).\n"
            "; Default: false");
        ini.SetDoubleValue("General", "fCastSpeedMult",           fCastSpeedMult,
            "; Speed multiplier applied while casting (1.0 = full run speed).\n"
            "; Active only when bRemoveCastSlowdown=true. Range: 0.0 – 10.0\n"
            "; Default: 1.0");

        ini.SaveFile(iniPath.c_str());

        // ── Log final config ────────────────────────────────────────────────
        logger::info("[Config] bAllowSprintWhileCasting = {}", bAllowSprintWhileCasting);
        logger::info("[Config] bRemoveCastSlowdown      = {}", bRemoveCastSlowdown);
        logger::info("[Config] bApplyToNPCs             = {}", bApplyToNPCs);
        logger::info("[Config] fCastSpeedMult           = {:.2f}", fCastSpeedMult);
    }
}
