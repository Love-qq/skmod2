#pragma once

// ---------------------------------------------------------------------------
// Config.h  –  INI configuration loader
// ---------------------------------------------------------------------------

namespace CastSprint
{
    struct Config
    {
        // ── Feature flags ───────────────────────────────────────────────────
        bool bAllowSprintWhileCasting{ true };   // prevent sprint cancellation on cast start
        bool bRemoveCastSlowdown{ true };         // force run/sprint movement type during cast
        bool bApplyToNPCs{ false };               // also affect NPC actors

        // ── Speed tweak ─────────────────────────────────────────────────────
        // Multiplier applied to movement speed while casting.
        // 1.0 = full run speed, 0.0 = use vanilla, >1.0 = faster.
        // Only active when bRemoveCastSlowdown == true.
        float fCastSpeedMult{ 1.0f };

        // ── Load from Data/SKSE/Plugins/CastSprint.ini ─────────────────────
        static Config& GetSingleton() noexcept;
        void Load();

    private:
        Config() = default;
    };
}
