#pragma once

#include "PCH.h"

namespace CastSprint::Diagnostics
{
    std::string GetActorMovementTypeName(RE::Actor* a_actor);

    /// Called on kPreLoadGame / kPostLoadGame / kNewGame: the player's graph may have been
    /// recreated, so the animation-event sink must be (re)registered by OnFrame().
    void TryRegisterAnimEventSink();

    /// Kept for source compatibility with SprintPatch / MovementPatch (does nothing in v4).
    void LogCastingStateIfThrottled(RE::Actor* a_actor);

    /// Runs once per frame on the MAIN thread (hooked in Hooks.cpp).
    void OnFrame();

    /// Registers the vectored exception handler used for the hardware write-watch.
    void InstallHwWatch();
}
