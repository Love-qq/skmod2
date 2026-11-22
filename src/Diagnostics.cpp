// ---------------------------------------------------------------------------
// Diagnostics.cpp  -  DIAG BUILD v4
//
// Goal: find out WHO ends the player's sprint and WHO switches the movement type
// to NPCMagicCasting when a spell cast begins.
//
// Three independent sources of evidence, all written to CastSprint.log:
//   1. [Ev]       animation-graph events; for key events also the CALL STACK that
//                 produced the event (tells which code / which plugin sent it).
//   2. [Change]   per-frame watcher: any change of movement type, sprint flag,
//                 magic-caster states or cast-related graph variables, together
//                 with the last events leading up to it.
//   3. [HW.*]     hardware write-watch (debug registers, main thread only) on
//                 (a) HighProcessData::currentMovementType.typeName and
//                 (b) the player's ActorState1 flags (sprinting bit = bit 8).
//                 Logs the exact instruction (module+offset) and call stack that
//                 performed the write.
//
// Offsets in "SkyrimSE.exe+0x..." equal the RVA used by Address Library / IDA.
// ---------------------------------------------------------------------------
#include "PCH.h"
#include "Diagnostics.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <string>

namespace CastSprint::Diagnostics
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point g_t0 = Clock::now();

        long long NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_t0).count();
        }

        std::string Hex(std::uintptr_t v)
        {
            char b[32];
            std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
            return b;
        }

        // "SkyrimSE.exe+0x70508D" / "SomePlugin.dll+0x1234"
        std::string DescribeAddress(std::uintptr_t addr)
        {
            HMODULE mod = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(addr), &mod) && mod) {
                char path[MAX_PATH]{};
                GetModuleFileNameA(mod, path, MAX_PATH);
                const char* name = path;
                for (const char* p = path; *p; ++p) {
                    if (*p == '\\' || *p == '/') name = p + 1;
                }
                return std::string(name) + "+" + Hex(addr - reinterpret_cast<std::uintptr_t>(mod));
            }
            return Hex(addr);
        }

        std::string CaptureStack(unsigned skip, unsigned maxFrames)
        {
            void* frames[40]{};
            if (maxFrames > 40) maxFrames = 40;
            const USHORT n = RtlCaptureStackBackTrace(skip, maxFrames, frames, nullptr);
            std::string out;
            for (USHORT i = 0; i < n; ++i) {
                if (i) out += " <- ";
                out += DescribeAddress(reinterpret_cast<std::uintptr_t>(frames[i]));
            }
            return out;
        }

        // ── recent animation events ────────────────────────────────────────
        struct Ev { long long t; std::string tag; };
        std::deque<Ev> g_events;
        std::mutex     g_evMutex;

        void PushEvent(const std::string& tag)
        {
            std::lock_guard lk(g_evMutex);
            g_events.push_back({ NowMs(), tag });
            while (g_events.size() > 12) g_events.pop_front();
        }

        std::string RecentEvents()
        {
            std::lock_guard lk(g_evMutex);
            const auto now = NowMs();
            std::string out;
            for (const auto& e : g_events) {
                out += e.tag + "(-" + std::to_string(now - e.t) + "ms) ";
            }
            return out;
        }

        // ── snapshots ──────────────────────────────────────────────────────
        const char* const kBoolVars[] = {
            "IsSprinting", "bSprinting", "IsCasting", "bIsCasting", "bWantCastLeft", "bWantCastRight",
            "IsAttacking", "IsBlocking", "bAnimationDriven", "bMotionDriven", "IsSneaking"
        };
        const char* const kFloatVars[] = { "Speed", "SpeedSampled", "Direction", "TurnDelta" };
        const char* const kIntVars[]   = { "iSyncSprintState", "iLeftHandType", "iRightHandType", "iState" };

        std::string GraphVars(RE::PlayerCharacter* p)
        {
            std::string out;
            char buf[64];
            for (auto* n : kBoolVars) {
                bool v = false;
                if (p->GetGraphVariableBool(n, v)) { out += n; out += v ? "=1 " : "=0 "; }
            }
            for (auto* n : kFloatVars) {
                float v = 0.f;
                if (p->GetGraphVariableFloat(n, v)) { std::snprintf(buf, sizeof(buf), "%s=%.1f ", n, v); out += buf; }
            }
            for (auto* n : kIntVars) {
                std::int32_t v = 0;
                if (p->GetGraphVariableInt(n, v)) { std::snprintf(buf, sizeof(buf), "%s=%d ", n, v); out += buf; }
            }
            return out;
        }

        int CasterState(RE::PlayerCharacter* p, RE::MagicSystem::CastingSource src)
        {
            if (auto* c = p->GetMagicCaster(src)) {
                return static_cast<int>(c->state.get());
            }
            return -1;
        }

        struct Snap
        {
            std::string mt;
            bool        sprinting{ false };
            int         lState{ -1 };
            int         rState{ -1 };
            bool        wantL{ false };
            bool        wantR{ false };
            float       mtRunFwd{ 0.f };
            std::uint32_t state1{ 0 };
        };

        Snap TakeSnap(RE::PlayerCharacter* p)
        {
            Snap s;
            s.mt        = GetActorMovementTypeName(p);
            s.sprinting = p->AsActorState()->IsSprinting();
            s.lState    = CasterState(p, RE::MagicSystem::CastingSource::kLeftHand);
            s.rState    = CasterState(p, RE::MagicSystem::CastingSource::kRightHand);
            p->GetGraphVariableBool("bWantCastLeft", s.wantL);
            p->GetGraphVariableBool("bWantCastRight", s.wantR);
            if (p->currentProcess && p->currentProcess->high) {
                s.mtRunFwd = p->currentProcess->high->currentMovementType.defaultData
                                 .speeds[RE::Movement::SPEED_DIRECTIONS::kForward][RE::Movement::MaxSpeeds::kRun];
            }
            s.state1 = *reinterpret_cast<const std::uint32_t*>(&p->AsActorState()->actorState1);
            return s;
        }

        std::string SnapText(const Snap& s)
        {
            char b[256];
            std::snprintf(b, sizeof(b),
                          "MT=%s sprinting=%d casterL=%d casterR=%d wantL=%d wantR=%d mtRunFwd=%.1f state1=0x%08X",
                          s.mt.c_str(), s.sprinting, s.lState, s.rState, s.wantL, s.wantR, s.mtRunFwd, s.state1);
            return b;
        }

        // ── animation event sink ───────────────────────────────────────────
        bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
        bool Contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

        bool IsNoise(const std::string& t)
        {
            return StartsWith(t, "Foot") || StartsWith(t, "Collision") || Contains(t, "Bumper") ||
                   Contains(t, "AnimatedCameraDelta") || StartsWith(t, "Weapon_") || t.empty();
        }

        bool IsKey(const std::string& t)
        {
            return Contains(t, "print") || Contains(t, "Cast") || Contains(t, "ttack") || Contains(t, "Interrupt") ||
                   Contains(t, "SpellFire") || Contains(t, "SpellReady") || Contains(t, "weaponDraw") ||
                   StartsWith(t, "MRh_") || StartsWith(t, "MLh_") || StartsWith(t, "Magic_");
        }

        class AnimEventLogger : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
        {
        public:
            static AnimEventLogger* GetSingleton()
            {
                static AnimEventLogger instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
            {
                if (!a_event) return RE::BSEventNotifyControl::kContinue;

                const std::string tag     = a_event->tag.data() ? a_event->tag.c_str() : "";
                const std::string payload = a_event->payload.data() ? a_event->payload.c_str() : "";
                PushEvent(tag);
                if (IsNoise(tag)) return RE::BSEventNotifyControl::kContinue;

                logger::info("[Ev t={}ms thr={}] tag='{}' payload='{}'", NowMs(), GetCurrentThreadId(), tag, payload);

                if (IsKey(tag)) {
                    // Frames 0..1 are this function and the dispatcher; skip them.
                    logger::info("[Ev.stack] {} :: {}", tag, CaptureStack(1, 16));
                    if (auto* p = RE::PlayerCharacter::GetSingleton()) {
                        logger::info("[Ev.snap]  {} :: {} | vars: {}", tag, SnapText(TakeSnap(p)), GraphVars(p));
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        std::atomic<bool> g_sinkRegistered{ false };

        // ── hardware write-watch ───────────────────────────────────────────
        std::atomic<bool>          g_hwEnabled{ false };
        std::atomic<std::uintptr_t> g_mtAddr{ 0 };      // &currentMovementType.typeName
        std::atomic<std::uintptr_t> g_stateAddr{ 0 };   // &player->actorState1
        std::atomic<std::uintptr_t> g_lastNamePtr{ 0 };
        std::atomic<std::uint32_t>  g_lastState1{ 0 };
        std::atomic<std::uint32_t>  g_mtSameWrites{ 0 };
        std::atomic<DWORD>          g_mainThreadId{ 0 };

        std::string UnwindStack(const CONTEXT& src, int maxFrames)
        {
            CONTEXT ctx = src;
            std::string out;
            for (int i = 0; i < maxFrames; ++i) {
                const DWORD64 ip = ctx.Rip;
                if (!ip) break;
                if (i) out += " <- ";
                out += DescribeAddress(static_cast<std::uintptr_t>(ip));

                DWORD64 imageBase = 0;
                auto* fn = RtlLookupFunctionEntry(ip, &imageBase, nullptr);
                if (!fn) {
                    ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                    ctx.Rsp += 8;
                } else {
                    PVOID  handlerData = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ip, fn, &ctx, &handlerData, &establisher, nullptr);
                }
            }
            return out;
        }

        LONG CALLBACK HwVeh(PEXCEPTION_POINTERS ep)
        {
            if (!g_hwEnabled.load(std::memory_order_relaxed)) return EXCEPTION_CONTINUE_SEARCH;
            if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;

            CONTEXT* c = ep->ContextRecord;
            if (!(c->Dr6 & 0x3)) return EXCEPTION_CONTINUE_SEARCH;

            // The trap fires AFTER the writing instruction; Rip points at the next one.
            if (c->Dr6 & 0x1) {
                const auto addr = g_mtAddr.load();
                if (addr) {
                    const auto cur = *reinterpret_cast<const std::uintptr_t*>(addr);
                    if (cur != g_lastNamePtr.load()) {
                        g_lastNamePtr = cur;
                        logger::info("[HW.MT t={}ms thr={}{}] movement-type name pointer changed ({} same-value writes skipped) | "
                                     "after-write RIP={} | stack: {}",
                                     NowMs(), GetCurrentThreadId(),
                                     GetCurrentThreadId() == g_mainThreadId.load() ? " main" : "",
                                     g_mtSameWrites.exchange(0), DescribeAddress(static_cast<std::uintptr_t>(c->Rip)),
                                     UnwindStack(*c, 14));
                    } else {
                        ++g_mtSameWrites;
                    }
                }
            }
            if (c->Dr6 & 0x2) {
                const auto addr = g_stateAddr.load();
                if (addr) {
                    const auto cur = *reinterpret_cast<const std::uint32_t*>(addr);
                    const auto old = g_lastState1.load();
                    if (cur != old) {
                        const bool oldSpr = (old >> 8) & 1;
                        const bool newSpr = (cur >> 8) & 1;
                        g_lastState1 = cur;
                        // Log every sprint-bit flip in full; other flag changes only briefly.
                        if (oldSpr != newSpr) {
                            logger::info("[HW.SPRINT t={}ms thr={}] sprinting {} -> {} (state1 0x{:08X} -> 0x{:08X}) | "
                                         "after-write RIP={} | stack: {}",
                                         NowMs(), GetCurrentThreadId(), oldSpr, newSpr, old, cur,
                                         DescribeAddress(static_cast<std::uintptr_t>(c->Rip)), UnwindStack(*c, 14));
                        }
                    }
                }
            }
            c->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // Arm DR0/DR1 on the CURRENT (main) thread.
        void ArmHw(std::uintptr_t a0, std::uintptr_t a1)
        {
            static std::uintptr_t s0 = 0, s1 = 0;
            if (a0 == s0 && a1 == s1) return;

            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            HANDLE th = GetCurrentThread();
            if (!GetThreadContext(th, &ctx)) {
                logger::error("[HW] GetThreadContext failed ({})", GetLastError());
                s0 = a0; s1 = a1;  // do not retry every frame
                return;
            }
            ctx.Dr0 = a0;                       // 8 bytes, write
            ctx.Dr1 = a1;                       // 4 bytes, write
            ctx.Dr6 = 0;
            ctx.Dr7 = (ctx.Dr7 & ~0xFF000FULL)
                    | 0x1ULL | (0x1ULL << 16) | (0x2ULL << 18)    // DR0: enable, write, len 8
                    | 0x4ULL | (0x1ULL << 20) | (0x3ULL << 22);   // DR1: enable, write, len 4
            if (!SetThreadContext(th, &ctx)) {
                logger::error("[HW] SetThreadContext failed ({})", GetLastError());
            } else {
                logger::info("[HW] armed on thread {}: DR0={} (movement type name) DR1={} (ActorState1)",
                             GetCurrentThreadId(), Hex(a0), Hex(a1));
            }
            s0 = a0; s1 = a1;
        }
        // ── v5: keep the sprint alive through a cast ──────────────────────
        // The engine switches the movement type to NPCMagicCasting when a cast starts,
        // and that also clears the sprint flag. Keep a snapshot of the NPCSprinting
        // movement type taken on the last non-cast frame while sprinting. During a cast,
        // if the type has been switched away from NPCSprinting, restore the snapshot and
        // set the sprint bit back.
        using MovementTypeT = std::remove_cvref_t<decltype(std::declval<RE::HighProcessData&>().currentMovementType)>;
        std::optional<MovementTypeT> g_sprintSnapshot;
        std::uint64_t g_fixRestores = 0;

        void ApplyCastSprintFix(RE::PlayerCharacter* p)
        {
            if (!p->currentProcess || !p->currentProcess->high) return;
            auto& mt = p->currentProcess->high->currentMovementType;
            const char* cName = mt.typeName.c_str();
            const std::string name = cName ? cName : "";

            const bool casting = CasterState(p, RE::MagicSystem::CastingSource::kLeftHand) >= 1 ||
                                 CasterState(p, RE::MagicSystem::CastingSource::kRightHand) >= 1;

            if (!casting) {
                if (name == "NPCSprinting") {
                    g_sprintSnapshot = mt;
                } else {
                    g_sprintSnapshot.reset();   // not sprinting before this frame: nothing to restore
                }
                return;
            }

            if (!g_sprintSnapshot || name == "NPCSprinting") return;

            mt = *g_sprintSnapshot;
            ++g_fixRestores;

            // v6: the engine clears the sprint bit (ActorState1 bit 0x100) together with the
            // movement-type switch. Set it back while the restored sprint type is active.
            auto* st = reinterpret_cast<std::uint32_t*>(&p->AsActorState()->actorState1);
            const std::uint32_t before = *st;
            *st |= 0x100u;
            if (before != *st || g_fixRestores % 60 == 1) {
                logger::info("[Fix t={}ms] restored NPCSprinting during cast (was {}, restores={}) state1 0x{:08X} -> 0x{:08X}",
                             NowMs(), name, g_fixRestores, before, *st);
            }
        }
    }  // namespace

    // ── public API ─────────────────────────────────────────────────────────
    std::string GetActorMovementTypeName(RE::Actor* a_actor)
    {
        if (!a_actor) return "None";
        if (a_actor->currentProcess && a_actor->currentProcess->high) {
            const auto& n = a_actor->currentProcess->high->currentMovementType.typeName;
            if (!n.empty()) return n.c_str();
        }
        return "?";
    }

    void LogCastingStateIfThrottled(RE::Actor*) {}

    void TryRegisterAnimEventSink()
    {
        g_sinkRegistered = false;  // OnFrame() registers again as soon as the player has 3D
    }

    void InstallHwWatch()
    {
        if (std::filesystem::exists("Data/SKSE/Plugins/CastSprint_NoHW.txt")) {
            logger::warn("[HW] CastSprint_NoHW.txt present - hardware watch disabled.");
            return;
        }
        if (!AddVectoredExceptionHandler(1, HwVeh)) {
            logger::error("[HW] AddVectoredExceptionHandler failed.");
            return;
        }
        g_hwEnabled = true;
        logger::info("[HW] vectored handler installed.");
    }

    void OnFrame()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) return;

        if (!g_mainThreadId) g_mainThreadId = GetCurrentThreadId();

        // 1) (re)register the animation-event sink
        if (!g_sinkRegistered.load()) {
            if (player->AddAnimationGraphEventSink(AnimEventLogger::GetSingleton())) {
                g_sinkRegistered = true;
                logger::info("[Diag] animation-event sink registered (t={}ms).", NowMs());
            }
        }

        // 2) hardware watch on the main thread
        if (g_hwEnabled.load() && player->currentProcess && player->currentProcess->high) {
            const auto mtAddr    = reinterpret_cast<std::uintptr_t>(&player->currentProcess->high->currentMovementType.typeName);
            const auto stateAddr = reinterpret_cast<std::uintptr_t>(&player->AsActorState()->actorState1);
            if (mtAddr != g_mtAddr.load() || stateAddr != g_stateAddr.load()) {
                g_mtAddr        = mtAddr;
                g_stateAddr     = stateAddr;
                g_lastNamePtr   = *reinterpret_cast<const std::uintptr_t*>(mtAddr);
                g_lastState1    = *reinterpret_cast<const std::uint32_t*>(stateAddr);
                ArmHw(mtAddr, stateAddr);
            }
        }

        // 2b) v5 sprint-through-cast fix (main thread only)
        ApplyCastSprintFix(player);

        // 3) change detector
        static bool  s_have = false;
        static Snap  s_prev;
        static std::string s_prevVars;
        const Snap cur = TakeSnap(player);

        const bool changed = !s_have || cur.mt != s_prev.mt || cur.sprinting != s_prev.sprinting ||
                             cur.lState != s_prev.lState || cur.rState != s_prev.rState ||
                             cur.wantL != s_prev.wantL || cur.wantR != s_prev.wantR;
        if (changed) {
            if (s_have) {
                std::string what;
                if (cur.mt != s_prev.mt) what += "MT:" + s_prev.mt + "->" + cur.mt + " ";
                if (cur.sprinting != s_prev.sprinting) what += std::string("sprint:") + (s_prev.sprinting ? "1" : "0") + "->" + (cur.sprinting ? "1" : "0") + " ";
                if (cur.lState != s_prev.lState) what += "casterL:" + std::to_string(s_prev.lState) + "->" + std::to_string(cur.lState) + " ";
                if (cur.rState != s_prev.rState) what += "casterR:" + std::to_string(s_prev.rState) + "->" + std::to_string(cur.rState) + " ";
                if (cur.wantL != s_prev.wantL) what += std::string("wantL:") + (cur.wantL ? "1" : "0") + " ";
                if (cur.wantR != s_prev.wantR) what += std::string("wantR:") + (cur.wantR ? "1" : "0") + " ";
                logger::info("[Change t={}ms] {}| {} | recent: {}", NowMs(), what, SnapText(cur), RecentEvents());
                logger::info("[Change.vars] {}", GraphVars(player));
            } else {
                logger::info("[Change t={}ms] initial | {}", NowMs(), SnapText(cur));
            }
            s_prev = cur;
            s_have = true;
        }
    }
}
