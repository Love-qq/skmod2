// ---------------------------------------------------------------------------
// Hooks.cpp  -  DIAG BUILD v4
//
// v4 is observation-only: NO gameplay hooks are installed (the sprint / movement
// patches were found to sit on the wrong functions).  The only hook is a harmless
// per-frame callback at the end of Main::Update (ID 35565 + 0x748, SE 1.5.97),
// which is a `call` to an empty `ret` stub.
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "Hooks.h"
#include "Diagnostics.h"

namespace CastSprint
{
    namespace
    {
        using Stub_t = void (*)();
        Stub_t g_origFrameStub{ nullptr };

        void FrameThunk()
        {
            if (g_origFrameStub) {
                g_origFrameStub();
            }
            Diagnostics::OnFrame();
        }
    }

    void InstallHooks()
    {
        logger::info("[Hooks] v4: observation only, no gameplay hooks.");

        auto& trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> frameSite{ REL::ID(35565), 0x748 };

        // Sanity check: the site must be a 5-byte relative CALL (E8).
        const auto opcode = *reinterpret_cast<const std::uint8_t*>(frameSite.address());
        if (opcode != 0xE8) {
            logger::error("[Hooks] Frame hook site {:X} is not a CALL (opcode {:02X}) - frame watcher disabled.",
                          frameSite.address(), opcode);
        } else {
            g_origFrameStub = reinterpret_cast<Stub_t>(
                trampoline.write_call<5>(frameSite.address(), reinterpret_cast<std::uintptr_t>(&FrameThunk)));
            logger::info("[Hooks] Per-frame hook installed at {:X} (ID 35565+0x748).", frameSite.address());
        }

        Diagnostics::InstallHwWatch();
        logger::info("[Hooks] Done.");
    }
}
