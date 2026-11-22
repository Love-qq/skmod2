// ---------------------------------------------------------------------------
// SprintPatch.cpp
//
// Goal:
// 1. Prevent sprint from being cancelled when the player starts charging /
//    holding a spell while sprinting.
// 2. Allow entering sprint even if already charging / holding a spell.
// 3. Keep vanilla behavior for melee attacks, bows, shield block, etc.
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "SprintPatch.h"
#include "Config.h"
#include "Diagnostics.h"

namespace CastSprint::SprintPatch
{
    inline bool IsActorCastingOrHoldingSpell(RE::Actor* a_actor) noexcept
    {
        if (!a_actor) return false;

        // Check if magic casters are currently charging or casting
        for (auto source : { RE::MagicSystem::CastingSource::kLeftHand,
                             RE::MagicSystem::CastingSource::kRightHand,
                             RE::MagicSystem::CastingSource::kOther,
                             RE::MagicSystem::CastingSource::kInstant }) {
            if (auto* caster = a_actor->GetMagicCaster(source)) {
                auto st = caster->state.get();
                if (st == RE::MagicCaster::State::kCharging ||
                    st == RE::MagicCaster::State::kCasting) {
                    return true;
                }
            }
        }

        // Also check if either hand has a spell, scroll, or staff equipped
        for (bool leftHand : { true, false }) {
            if (auto* equipped = a_actor->GetEquippedObject(leftHand)) {
                if (equipped->Is(RE::FormType::Spell) ||
                    equipped->Is(RE::FormType::Scroll) ||
                    equipped->Is(RE::FormType::Enchantment)) {
                    return true;
                }
                if (auto* wep = equipped->As<RE::TESObjectWEAP>()) {
                    if (wep->IsStaff()) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    namespace detail
    {
        // ── Hook 1: Attack sprint stop check ────────────────────────────────
        using IsSprinting_t = bool (*)(RE::PlayerCharacter* a_player);
        static IsSprinting_t g_originalIsSprinting{ nullptr };

        static bool Hook_ShouldStopSprintOnAttack(RE::PlayerCharacter* a_player)
        {
            // Diagnostics: log unconditionally before any check
            logger::trace("[Hook_ShouldStopSprintOnAttack] Actor FormID: {:08X}, MovementType: {}",
                          a_player ? a_player->formID : 0,
                          Diagnostics::GetActorMovementTypeName(a_player));

            Diagnostics::LogCastingStateIfThrottled(a_player);

            const bool isSprinting = g_originalIsSprinting(a_player);
            if (!isSprinting) {
                return false;
            }

            const auto& cfg = Config::GetSingleton();
            if (cfg.bAllowSprintWhileCasting && a_player) {
                if (IsActorCastingOrHoldingSpell(a_player)) {
                    logger::trace("[SprintPatch] Prevented sprint cancellation during spell cast.");
                    return false;  // Tells caller player is NOT sprinting -> skips kActionSprintStop
                }
            }

            return true;  // Stop sprint normally for melee, bows, etc.
        }

        // ── Hook 2: Can enter sprint check ──────────────────────────────────
        using CheckCanSprint_t = bool (*)(RE::Actor* a_actor, std::uint32_t a_hand);
        static CheckCanSprint_t g_originalCheckCanSprint{ nullptr };

        static bool Hook_CheckCanSprint(RE::Actor* a_actor, std::uint32_t a_hand)
        {
            // Diagnostics: log unconditionally before any check
            logger::trace("[Hook_CheckCanSprint] Actor FormID: {:08X}, MovementType: {}",
                          a_actor ? a_actor->formID : 0,
                          Diagnostics::GetActorMovementTypeName(a_actor));

            Diagnostics::LogCastingStateIfThrottled(a_actor);

            const bool canSprintVanilla = g_originalCheckCanSprint(a_actor, a_hand);
            if (canSprintVanilla) {
                return true;
            }

            const auto& cfg = Config::GetSingleton();
            if (cfg.bAllowSprintWhileCasting && a_actor) {
                if (IsActorCastingOrHoldingSpell(a_actor)) {
                    logger::trace("[SprintPatch] Allowed sprint start while casting.");
                    return true;
                }
            }

            return false;
        }
    }  // namespace detail

    void Install()
    {
        const auto& cfg = Config::GetSingleton();
        if (!cfg.bAllowSprintWhileCasting) {
            logger::info("[SprintPatch] bAllowSprintWhileCasting=false – skipping hook install.");
            return;
        }

        auto& trampoline = SKSE::GetTrampoline();

        // Hook 1: Attack sprint-stop check (ID 41260 + 0x5D -> RVA 0x70508D)
        REL::Relocation<std::uintptr_t> hookAttack{ REL::ID(41260), 0x5D };
        detail::g_originalIsSprinting = reinterpret_cast<detail::IsSprinting_t>(
            trampoline.write_call<5>(hookAttack.address(), reinterpret_cast<std::uintptr_t>(detail::Hook_ShouldStopSprintOnAttack))
        );

        // Hook 2: Sprint button can-sprint check (ID 41358 + 0x188 -> RVA 0x709708)
        REL::Relocation<std::uintptr_t> hookSprintBtn{ REL::ID(41358), 0x188 };
        detail::g_originalCheckCanSprint = reinterpret_cast<detail::CheckCanSprint_t>(
            trampoline.write_call<5>(hookSprintBtn.address(), reinterpret_cast<std::uintptr_t>(detail::Hook_CheckCanSprint))
        );

        logger::info("[SprintPatch] Installed 5-byte call hooks at {:08X} (ID 41260+0x5D) and {:08X} (ID 41358+0x188).",
                     hookAttack.address(), hookSprintBtn.address());
    }
}
