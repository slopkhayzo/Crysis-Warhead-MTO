// A frame-rate log for play sessions: see frame_log.h.

#include "frame_log.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "calltrace.h"
#include "config.h"
#include "diag.h"
#include "flush_thread.h"
#include "hooks.h"
#include "log.h"
#include "scan.h"

namespace optiperf {
namespace {

using UpdateFn = bool (*)(void* self, int updateFlags, int pauseMode);
constexpr unsigned kUpdateSlot = 3;  // ISystem::Update(int updateFlags, int nPauseMode) (Mod SDK; the frame phases' too)
// (the renderer's log, a diagnostic: ISystem::GetIConsole, IConsole::ExecuteString(command, silent, defer))
constexpr unsigned kSlotGetIConsole = 29, kSlotExecuteString = 31;
using GetConsoleFn = void* (*)(void* system);
using ExecuteFn = void (*)(void* console, const char* command, bool silent, bool defer);
constexpr int kBins = 4000;          // frame times in 0.05 ms bins, to 200 ms; longer ones in the last
constexpr double kBinMs = 0.05;
constexpr double kStallMs = 1000.0;  // a longer frame is a load or the exit: counted apart

struct State {
    bool installed = false;
    UpdateFn update = nullptr;
    DWORD thread = 0;  // the main thread (the first caller)
    double tickMs = 0;
    LARGE_INTEGER start{}, last{}, secondStart{};
    bool started = false;
    // this second
    uint32_t secondFrames = 0, secondPaused = 0;
    double secondWorst = 0;
    // the session's frames, paused ones apart
    uint64_t frames = 0, paused = 0, stalls = 0;
    double totalMs = 0, worstMs = 0, below100Ms = 0, below60Ms = 0;
    uint32_t bins[kBins + 1] = {};
    FILE* csv = nullptr;
    CRITICAL_SECTION lock;  // (the reports read the session's numbers from the reporter thread)
    // the renderer's log (RLogAt): "r_Log <level>" at that counted frame's start, "r_Log 0" rlogFrames later
    int64_t rlogAt = -1;
    int rlogFrames = 2, rlogLevel = 4;
    uint64_t rlogOnAt = 0;
    bool rlogOn = false, rlogDone = false;
};
State g;

void EndSecond(const LARGE_INTEGER& now) {
    const double seconds = double(now.QuadPart - g.secondStart.QuadPart) * g.tickMs / 1000.0;
    if (g.csv) {
        std::fprintf(g.csv, "%.1f,%u,%.1f,%.2f,%u\n", double(now.QuadPart - g.start.QuadPart) * g.tickMs / 1000.0,
                     g.secondFrames, seconds > 0 ? double(g.secondFrames) / seconds : 0.0, g.secondWorst, g.secondPaused);
        std::fflush(g.csv);
    }
    g.secondStart = now;
    g.secondFrames = g.secondPaused = 0;
    g.secondWorst = 0;
}

void Count(const LARGE_INTEGER& now, bool paused) {
    if (!g.started) {
        g.started = true;
        g.start = g.last = g.secondStart = now;
        return;
    }
    const double ms = double(now.QuadPart - g.last.QuadPart) * g.tickMs;
    g.last = now;
    ++g.secondFrames;
    if (ms > g.secondWorst) g.secondWorst = ms;
    if (paused || ms > kStallMs) {
        if (paused) ++g.secondPaused;
        EnterCriticalSection(&g.lock);
        ++(paused ? g.paused : g.stalls);
        LeaveCriticalSection(&g.lock);
    } else {
        int bin = int(ms / kBinMs);
        if (bin > kBins) bin = kBins;
        EnterCriticalSection(&g.lock);
        ++g.frames;
        g.totalMs += ms;
        if (ms > g.worstMs) g.worstMs = ms;
        if (ms > 10.0) g.below100Ms += ms;
        if (ms > 1000.0 / 60.0) g.below60Ms += ms;
        ++g.bins[bin];
        LeaveCriticalSection(&g.lock);
    }
    if (double(now.QuadPart - g.secondStart.QuadPart) * g.tickMs >= 1000.0) EndSecond(now);
}

#ifdef _WIN64
// The renderer's own log for a few frames (RLogAt): the console's command at the frame's start; the renderer opens and
// closes Direct3DLog.txt at its next BeginFrame. (The frames counted, so the same demo frame give or take a few.)
void RLog(void* system) {
    const char* command = nullptr;
    char on[32];
    if (!g.rlogOn && g.frames >= uint64_t(g.rlogAt)) {
        std::snprintf(on, sizeof on, "r_Log %d", g.rlogLevel);
        command = on;
        g.rlogOn = true;
        g.rlogOnAt = g.frames;
    } else if (g.rlogOn && !g.rlogDone && g.frames >= g.rlogOnAt + uint64_t(g.rlogFrames)) {
        command = "r_Log 0";
        g.rlogDone = true;
    }
    if (!command) return;
    void* const console = reinterpret_cast<GetConsoleFn>((*static_cast<void***>(system))[kSlotGetIConsole])(system);
    if (!console) return;
    reinterpret_cast<ExecuteFn>((*static_cast<void***>(console))[kSlotExecuteString])(console, command, true, false);
    Log("frame log: \"%s\" at the start of counted frame %llu", command, static_cast<unsigned long long>(g.frames));
}

// The post effects' watch (PostWatch, a diagnostic): the 3D engine's SetPostEffectParam (C3DEngine's forwarder to the
// renderer's CREPostProcess, slot 22) hooked, its calls for a parameter whose name contains the watched text logged with
// the frame, the thread, whether a flush ran beside the main thread then (the render queue takes such a call) and the
// caller; and CryVision's parameters read at each frame's start through the post effects' manager's lookup, logged when
// they change (by anyone: the game's calls, the renderer's own).
using SetPostFn = void (*)(void* engine, const char* name, float value, bool force);
using GetByNameFn = char* (*)(void* manager, const char* name);
// C3DEngine::SetPostEffectParam: if (name && m_pREPostProcess) m_pREPostProcess->vf22(name, value, force)
constexpr char kSetPostCode[] =
    "48 83 EC 28 48 85 D2 74 ?? 48 83 B9 ?? ?? ?? ?? 00 74 ?? 48 8B 89 ?? ?? ?? ?? 48 8B 01 FF 90 B0 00 00 00 48 83 C4 "
    "28 C3";
// the glow pass's end: lea rdx, "Glow_Active"; lea rcx, manager; call lookup; ...; movss xmm1, 1.0; call [rdx+0x10]
constexpr char kGlowActiveCode[] =
    "48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C0 48 8B 10 48 8B C8 F3 0F 10 0D ?? ?? ?? ?? FF 52 10";
constexpr unsigned kMaxWatchLines = 400;
struct WatchedParam {
    const char* name;
    char kind;  // b, i, f: CParamBool, CParamInt, CParamFloat (their value at +8)
    char* param;
    double last;
    bool seen;
};
struct Watch {
    std::string text;
    SetPostFn setPost = nullptr;
    GetByNameFn lookup = nullptr;
    char* manager = nullptr;
    WatchedParam params[3] = {{"CryVision_Active", 'b', nullptr, 0, false},
                              {"CryVision_Amount", 'f', nullptr, 0, false},
                              {"CryVision_Type", 'i', nullptr, 0, false}};
    unsigned lines = 0;
};
Watch w;

void SetPostHook(void* engine, const char* name, float value, bool force) {
    if (name && std::strstr(name, w.text.c_str()) && w.lines < kMaxWatchLines) {
        ++w.lines;
        char where[160];
        DescribeAddress(_ReturnAddress(), where, sizeof where);
        const DWORD me = GetCurrentThreadId();
        Log("post watch: frame %llu: SetPostEffectParam(\"%s\", %g, %d) on %s (thread %lu)%s, called from %s",
            static_cast<unsigned long long>(g.frames), name, double(value), int(force),
            me == g.thread ? "the main thread" : OnFlushThread() ? "the flush's thread" : "another thread", me,
            FlushThread_Beside() ? " beside the flush (taken)" : "", where);
    }
    w.setPost(engine, name, value, force);
}

void WatchValues() {
    if (!w.lookup) return;
    for (WatchedParam& q : w.params) {
        if (!q.param) q.param = w.lookup(w.manager, q.name);
        if (!q.param) continue;
        const char* const v = q.param + 8;
        double now = 0;
        if (q.kind == 'b')
            now = double(*reinterpret_cast<const uint8_t*>(v));
        else if (q.kind == 'i')
            now = double(*reinterpret_cast<const int32_t*>(v));
        else
            now = double(*reinterpret_cast<const float*>(v));
        if ((!q.seen || now != q.last) && w.lines < kMaxWatchLines) {
            ++w.lines;
            Log("post watch: frame %llu: %s = %g at the frame's start%s", static_cast<unsigned long long>(g.frames),
                q.name, now, q.seen ? "" : " (first read)");
        }
        q.last = now;
        q.seen = true;
    }
}

void WatchOnModule(const wchar_t* baseName, char* base) {
    if (_wcsicmp(baseName, L"Cry3DEngine.dll") == 0 && !w.setPost) {
        const std::vector<char*> at = FindInCode(base, ParsePattern(kSetPostCode), 2);
        if (at.size() != 1 ||
            !InstallHook(at[0], reinterpret_cast<void*>(&SetPostHook), reinterpret_cast<void**>(&w.setPost),
                         "C3DEngine::SetPostEffectParam")) {
            Log("Cry3DEngine.dll: post watch: SetPostEffectParam not hooked (%zu matches)", at.size());
            return;
        }
        Log("Cry3DEngine.dll: post watch: SetPostEffectParam +0x%X hooked (\"%s\")", unsigned(at[0] - base),
            w.text.c_str());
    } else if (_wcsicmp(baseName, L"CryRenderD3D10.dll") == 0 && !w.lookup) {
        const std::vector<char*> at = FindInCode(base, ParsePattern(kGlowActiveCode), 4);
        if (at.empty()) {
            Log("CryRenderD3D10.dll: post watch: the post effects' lookup not found: no values watched");
            return;
        }
        w.manager = at[0] + 14 + int32_t(ReadU32(at[0] + 10));
        w.lookup = reinterpret_cast<GetByNameFn>(RelTarget(at[0] + 14));
        Log("CryRenderD3D10.dll: post watch: the post effects' manager +0x%X, its lookup +0x%X", unsigned(w.manager - base),
            unsigned(reinterpret_cast<char*>(w.lookup) - base));
    }
}

#endif

bool UpdateHook(void* self, int updateFlags, int pauseMode) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const DWORD me = GetCurrentThreadId();
    if (!g.thread) g.thread = me;
    if (me == g.thread) {
        Count(now, pauseMode != 0);
#ifdef _WIN64
        if (g.rlogAt >= 0 && !g.rlogDone) RLog(self);
        if (!w.text.empty()) WatchValues();
#endif
    }
    return g.update(self, updateFlags, pauseMode);
}

// the frame time at which `share` of the counted frames are shorter (from the bins; under the lock)
double Percentile(double share) {
    const uint64_t want = uint64_t(double(g.frames) * share);
    uint64_t seen = 0;
    for (int i = 0; i <= kBins; ++i) {
        seen += g.bins[i];
        if (seen > want) return (double(i) + 0.5) * kBinMs;
    }
    return g.worstMs;
}

// the function behind a vtable entry of a module that may not be relocated yet (the loader's notification can come first)
char* Entry(char* base, void* entry) {
    char* p = static_cast<char*>(CallTrace_Original(entry));
    if (IsInCode(base, p, 16)) return p;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    p = base + (reinterpret_cast<uintptr_t>(entry) - uintptr_t(nt->OptionalHeader.ImageBase));
    return IsInCode(base, p, 16) ? p : nullptr;
}

}  // namespace

void FrameLog_OnModuleLoaded(const wchar_t* baseName, char* base, size_t) {
#ifdef _WIN64
    // (the post effects' watch, a diagnostic; the 3D engine and the renderer load after CrySystem)
    if (!w.text.empty() && (_wcsicmp(baseName, L"Cry3DEngine.dll") == 0 || _wcsicmp(baseName, L"CryRenderD3D10.dll") == 0))
        return WatchOnModule(baseName, base);
#endif
    if (_wcsicmp(baseName, L"CrySystem.dll") != 0 || g.installed) return;
    const std::wstring v = ConfigString(L"Debug", L"FrameLog", L"OPTIPERF_FRAME_LOG", L"off");
    if (_wcsicmp(v.c_str(), L"on") != 0 && v != L"1") return;
    g.installed = true;
    void** const vt = FindRttiVtable(base, ".?AVCSystem@@");
    char* const update = vt ? Entry(base, vt[kUpdateSlot]) : nullptr;
    if (!update) {
        Log("CrySystem.dll: frame log off: CSystem::Update not found");
        return;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g.tickMs = 1000.0 / double(f.QuadPart);
    InitializeCriticalSection(&g.lock);
    std::wstring path = LogPath() ? LogPath() : L"";
    const size_t slash = path.find_last_of(L"\\/");
    path = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1) + L"optiperf_frames.csv";
    if (!path.empty() && _wfopen_s(&g.csv, path.c_str(), L"w") == 0 && g.csv)
        std::fprintf(g.csv, "seconds,frames,fps,longest_ms,paused_frames\n");
#ifdef _WIN64
    // (two diagnostics, not in the shipped optiperf.ini; x64: the console's and the 3D engine's methods called as x64
    // calls)
    const std::wstring at = ConfigString(L"Debug", L"RLogAt", L"OPTIPERF_RLOG_AT", L"");
    if (!at.empty()) {
        g.rlogAt = _wtoi64(at.c_str());
        const std::wstring n = ConfigString(L"Debug", L"RLogFrames", L"OPTIPERF_RLOG_FRAMES", L"2");
        const std::wstring level = ConfigString(L"Debug", L"RLogLevel", L"OPTIPERF_RLOG_LEVEL", L"4");
        g.rlogFrames = (std::max)(1, _wtoi(n.c_str()));
        g.rlogLevel = (std::max)(1, (std::min)(4, _wtoi(level.c_str())));
    }
    const std::wstring watch = ConfigString(L"Debug", L"PostWatch", L"OPTIPERF_POST_WATCH", L"");
    for (const wchar_t c : watch) w.text.push_back(char(c));  // (ASCII: a parameter's name)
#endif
    if (!InstallHook(update, reinterpret_cast<void*>(&UpdateHook), reinterpret_cast<void**>(&g.update), "CSystem::Update")) {
        Log("CrySystem.dll: frame log off: could not hook CSystem::Update");
        return;
    }
    Log("CrySystem.dll: frame log on: CSystem::Update +0x%X; a line a second in %ls", unsigned(update - base),
        g.csv ? path.c_str() : L"(no file: the log is off)");
    if (g.rlogAt >= 0)
        Log("CrySystem.dll: the renderer's log (r_Log %d) for %d frames from counted frame %lld (Direct3DLog.txt)",
            g.rlogLevel, g.rlogFrames, static_cast<long long>(g.rlogAt));
}

void FrameLog_Report(const char* when) {
    if (!g.update) return;
    EnterCriticalSection(&g.lock);
    const uint64_t frames = g.frames, paused = g.paused, stalls = g.stalls;
    const double total = g.totalMs, worst = g.worstMs, b100 = g.below100Ms, b60 = g.below60Ms;
    const double p50 = frames ? Percentile(0.5) : 0, p99 = frames ? Percentile(0.99) : 0, p999 = frames ? Percentile(0.999) : 0;
    LeaveCriticalSection(&g.lock);
    if (!frames) return;
    Log("frames [%s]: %llu counted (not counted: %llu paused, %llu over 1 s: loads, the exit), %.1f fps on average | frame "
        "times: median %.2f ms, 99th %.2f ms (1 %% low %.0f fps), 99.9th %.2f ms (0.1 %% low %.0f fps), longest %.1f ms | time "
        "below 100 fps %.1f %%, below 60 fps %.1f %%",
        when, static_cast<unsigned long long>(frames), static_cast<unsigned long long>(paused),
        static_cast<unsigned long long>(stalls), 1000.0 * double(frames) / total,
        p50, p99, 1000.0 / p99, p999, 1000.0 / p999, worst, 100.0 * b100 / total, 100.0 * b60 / total);
}

}  // namespace optiperf
