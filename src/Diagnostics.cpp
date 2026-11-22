#include "PCH.h"
#include "Diagnostics.h"
#include <chrono>
#include <atomic>

namespace CastSprint::Diagnostics
{
    std::string GetActorMovementTypeName(RE::Actor* a_actor)
    {
        if (!a_actor) return "None";
        std::string result;
        if (a_actor->currentProcess && a_actor->currentProcess->high) {
            const auto& typeName = a_actor->currentProcess->high->currentMovementType.typeName;
            if (!typeName.empty()) {
                result = typeName.c_str();
            }
        }
        if (result.empty()) {
            auto* state = a_actor->AsActorState();
            if (state) {
                if (state->IsSprinting()) result = "Sprinting";
                else if (state->actorState1.running) result = "Running";
                else if (state->IsWalking()) result = "Walking";
                else if (state->IsSneaking()) result = "Sneaking";
                else result = "Default";
            } else {
                result = "Unknown";
            }
        }
        return result;
    }

    class AnimEventLogger : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static AnimEventLogger* GetSingleton()
        {
            static AnimEventLogger instance;
            return &instance;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
        {
            if (a_event) {
                const char* tag = a_event->tag.data() ? a_event->tag.c_str() : "";
                const char* payload = a_event->payload.data() ? a_event->payload.c_str() : "";
                logger::trace("[AnimEvent] tag='{}' payload='{}'", tag, payload);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void TryRegisterAnimEventSink()
    {
        static std::atomic<bool> s_alreadyAttempted{ false };
        if (s_alreadyAttempted.exchange(true)) {
            return; // Register only once
        }

        auto* taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([]() {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || !player->Is3DLoaded()) {
                    logger::warn("[Diagnostics] Player 3D not loaded when attempting to register AnimEvent sink. Will not retry.");
                    return;
                }
                if (player->AddAnimationGraphEventSink(AnimEventLogger::GetSingleton())) {
                    logger::info("[Diagnostics] Successfully registered AnimationGraphEvent sink on PlayerCharacter.");
                } else {
                    logger::warn("[Diagnostics] AddAnimationGraphEventSink returned false. Will not retry.");
                }
            });
        }
    }

    void LogCastingStateIfThrottled(RE::Actor* a_actor)
    {
        if (!a_actor || !a_actor->IsPlayerRef()) return;

        auto* player = static_cast<RE::PlayerCharacter*>(a_actor);

        // Only log if the player is actually charging or casting a spell
        bool isCastingAny = false;
        for (auto source : { RE::MagicSystem::CastingSource::kLeftHand,
                             RE::MagicSystem::CastingSource::kRightHand,
                             RE::MagicSystem::CastingSource::kOther,
                             RE::MagicSystem::CastingSource::kInstant }) {
            if (auto* caster = player->GetMagicCaster(source)) {
                auto st = caster->state.get();
                if (st == RE::MagicCaster::State::kCharging ||
                    st == RE::MagicCaster::State::kCasting) {
                    isCastingAny = true;
                    break;
                }
            }
        }

        if (!isCastingAny) {
            return;
        }

        // Rate limit: at most once per second
        static auto s_lastLogTime = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - s_lastLogTime < std::chrono::seconds(1)) {
            return;
        }
        s_lastLogTime = now;

        auto* state = player->AsActorState();
        bool isSprinting = state ? state->IsSprinting() : false;
        std::string movType = GetActorMovementTypeName(player);

        float speed = 0.0f;
        player->GetGraphVariableFloat("Speed", speed);

        bool graphIsCasting = false;
        player->GetGraphVariableBool("IsCasting", graphIsCasting);

        bool bWantCastLeft = false;
        player->GetGraphVariableBool("bWantCastLeft", bWantCastLeft);

        bool bWantCastRight = false;
        player->GetGraphVariableBool("bWantCastRight", bWantCastRight);

        logger::trace("[PeriodicDiag] IsSprinting: {}, IsCasting: {}, MovType: {}, Graph: [Speed: {:.2f}, IsCasting: {}, bWantCastLeft: {}, bWantCastRight: {}]",
                      isSprinting, isCastingAny, movType, speed, graphIsCasting, bWantCastLeft, bWantCastRight);
    }
}
