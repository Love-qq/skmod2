// ---------------------------------------------------------------------------
// Hooks.cpp  –  Aggregates all hook installation
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "Hooks.h"
#include "SprintPatch.h"
#include "MovementPatch.h"

namespace CastSprint
{
    void InstallHooks()
    {
        logger::info("[Hooks] Installing sprint patch...");
        SprintPatch::Install();

        logger::info("[Hooks] Installing movement patch...");
        MovementPatch::Install();

        logger::info("[Hooks] All hooks installed.");
    }
}
