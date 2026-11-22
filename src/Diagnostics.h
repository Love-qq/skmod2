#pragma once

#include "PCH.h"

namespace CastSprint::Diagnostics
{
    std::string GetActorMovementTypeName(RE::Actor* a_actor);
    void TryRegisterAnimEventSink();
    void LogCastingStateIfThrottled(RE::Actor* a_actor);
}
