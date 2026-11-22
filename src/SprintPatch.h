#pragma once

// ---------------------------------------------------------------------------
// SprintPatch.h  –  Prevents sprint cancellation when a spell starts charging
// ---------------------------------------------------------------------------

namespace CastSprint::SprintPatch
{
    /// Install all hooks related to sprint-cancellation prevention.
    void Install();
}
