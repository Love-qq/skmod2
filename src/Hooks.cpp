// ---------------------------------------------------------------------------
// Hooks.cpp  -  CastSprint v9
//
// 1) Per-frame callback (diagnostics + sprint fix), at ID 35565 + 0x748.
// 2) v9 jump gate: inside JumpHandler::ProcessButton (SE 1.5.97, RVA 0x7090A0) two
//    calls return "busy" and cancel the jump while casting:
//       RVA 0x709183 -> 0x608800 (busy check)
//       RVA 0x70919F -> 0x63C330 (flag 0x100 check)
//    Both are wrapped: the original result is kept, except inside a cast window,
//    where a "busy" answer is turned into "allowed".
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "Hooks.h"
#include "Diagnostics.h"

namespace CastSprint
{
    namespace
    {
        using Stub_t  = void (*)();
        using Check_t = bool (*)(void*, std::uint32_t);

        Stub_t  g_origFrameStub{ nullptr };
        Check_t g_origJumpBusy{ nullptr };
        Check_t g_origJumpFlag{ nullptr };
        std::uint64_t g_jumpAllowed{ 0 };

        void FrameThunk()
        {
            if (g_origFrameStub) {
                g_origFrameStub();
            }
            Diagnostics::OnFrame();
        }

        bool Allow(bool original, const char* which)
        {
            if (original && Diagnostics::CastWindowActive()) {
                ++g_jumpAllowed;
                if (g_jumpAllowed <= 20 || g_jumpAllowed % 50 == 0) {
                    logger::info("[Jump] {} said busy during cast - allowed (count={})", which, g_jumpAllowed);
                }
                return false;
            }
            return original;
        }

        bool JumpBusyThunk(void* a, std::uint32_t b)
        {
            return Allow(g_origJumpBusy(a, b), "busy-check");
        }

        bool JumpFlagThunk(void* a, std::uint32_t b)
        {
            return Allow(g_origJumpFlag(a, b), "flag-0x100");
        }

        bool IsCallTo(std::uintptr_t site, std::uintptr_t target)
        {
            if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) return false;
            const auto rel = *reinterpret_cast<const std::int32_t*>(site + 1);
            return site + 5 + rel == target;
        }
    }

    void InstallHooks()
    {
        logger::info("[Hooks] v9: frame callback + jump gate.");

        auto& trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> frameSite{ REL::ID(35565), 0x748 };

        if (*reinterpret_cast<const std::uint8_t*>(frameSite.address()) == 0xE8) {
            g_origFrameStub = reinterpret_cast<Stub_t>(
                trampoline.write_call<5>(frameSite.address(), reinterpret_cast<std::uintptr_t>(&FrameThunk)));
            logger::info("[Hooks] Per-frame hook installed at {:X}.", frameSite.address());
        } else {
            logger::error("[Hooks] Frame site {:X} is not a CALL - frame watcher disabled.", frameSite.address());
        }

        const auto base = REL::Module::get().base();
        const auto busySite = base + 0x709183;
        const auto flagSite = base + 0x70919F;
        const auto busyTarget = base + 0x608800;
        const auto flagTarget = base + 0x63C330;

        if (IsCallTo(busySite, busyTarget) && IsCallTo(flagSite, flagTarget)) {
            g_origJumpBusy = reinterpret_cast<Check_t>(
                trampoline.write_call<5>(busySite, reinterpret_cast<std::uintptr_t>(&JumpBusyThunk)));
            g_origJumpFlag = reinterpret_cast<Check_t>(
                trampoline.write_call<5>(flagSite, reinterpret_cast<std::uintptr_t>(&JumpFlagThunk)));
            logger::info("[Hooks] Jump gate installed at {:X} and {:X}.", busySite, flagSite);
        } else {
            logger::error("[Hooks] Jump gate sites do not match the expected CALL targets - jump gate disabled.");
        }

        Diagnostics::InstallHwWatch();
        logger::info("[Hooks] Done.");
    }
}
