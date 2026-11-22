// ---------------------------------------------------------------------------
// MovementPatch.cpp
//
// Goal: remove the movement speed penalty while casting on the run/sprint.
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "MovementPatch.h"
#include "Config.h"
#include "Diagnostics.h"

namespace CastSprint::MovementPatch
{
    namespace detail
    {
        using SetMovementType_t = void (*)(RE::Actor* a_actor, RE::BGSMovementType* a_type);
        static SetMovementType_t g_originalSetMovementType_LH{ nullptr };
        static SetMovementType_t g_originalSetMovementType_RH{ nullptr };

        static void Hook_SetMovementType_LH(RE::Actor* a_actor, RE::BGSMovementType* a_type)
        {
            // Diagnostics: log unconditionally before any check
            logger::trace("[Hook_SetMovementType_LH] Actor FormID: {:08X}, MovementType: {}",
                          a_actor ? a_actor->formID : 0,
                          Diagnostics::GetActorMovementTypeName(a_actor));

            Diagnostics::LogCastingStateIfThrottled(a_actor);

            const auto& cfg = Config::GetSingleton();
            if (cfg.bRemoveCastSlowdown && a_actor) {
                const bool isPlayer = a_actor->IsPlayerRef();
                if (isPlayer || cfg.bApplyToNPCs) {
                    logger::trace("[MovementPatch] Suppressed LH MT_Cast slowdown for actor {:08X}.", a_actor->formID);
                    return; // Do NOT set cast slowdown movement type
                }
            }

            g_originalSetMovementType_LH(a_actor, a_type);
        }

        static void Hook_SetMovementType_RH(RE::Actor* a_actor, RE::BGSMovementType* a_type)
        {
            // Diagnostics: log unconditionally before any check
            logger::trace("[Hook_SetMovementType_RH] Actor FormID: {:08X}, MovementType: {}",
                          a_actor ? a_actor->formID : 0,
                          Diagnostics::GetActorMovementTypeName(a_actor));

            Diagnostics::LogCastingStateIfThrottled(a_actor);

            const auto& cfg = Config::GetSingleton();
            if (cfg.bRemoveCastSlowdown && a_actor) {
                const bool isPlayer = a_actor->IsPlayerRef();
                if (isPlayer || cfg.bApplyToNPCs) {
                    logger::trace("[MovementPatch] Suppressed RH MT_Cast slowdown for actor {:08X}.", a_actor->formID);
                    return; // Do NOT set cast slowdown movement type
                }
            }

            g_originalSetMovementType_RH(a_actor, a_type);
        }
    }  // namespace detail

    void Install()
    {
        const auto& cfg = Config::GetSingleton();
        if (!cfg.bRemoveCastSlowdown) {
            logger::info("[MovementPatch] bRemoveCastSlowdown=false – skipping hook install.");
            return;
        }

        auto& trampoline = SKSE::GetTrampoline();

        // Call site 1: Left hand casting movement type (ID 33570 + 0xA7 -> RVA 0x549EC7)
        REL::Relocation<std::uintptr_t> hookLH{ REL::ID(33570), 0xA7 };

        // Call site 2: Right hand casting movement type (ID 34298 + 0xA7 -> RVA 0x568397)
        REL::Relocation<std::uintptr_t> hookRH{ REL::ID(34298), 0xA7 };

        detail::g_originalSetMovementType_LH = reinterpret_cast<detail::SetMovementType_t>(
            trampoline.write_call<5>(hookLH.address(), reinterpret_cast<std::uintptr_t>(detail::Hook_SetMovementType_LH))
        );

        detail::g_originalSetMovementType_RH = reinterpret_cast<detail::SetMovementType_t>(
            trampoline.write_call<5>(hookRH.address(), reinterpret_cast<std::uintptr_t>(detail::Hook_SetMovementType_RH))
        );

        logger::info("[MovementPatch] Installed 5-byte call hooks at {:08X} (ID 33570+0xA7) and {:08X} (ID 34298+0xA7).",
                     hookLH.address(), hookRH.address());
    }
}
