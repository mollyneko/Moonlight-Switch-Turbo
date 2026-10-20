#include "SwitchThreadPlacement.hpp"

#ifdef __SWITCH__

#include <switch.h>

namespace switch_thread {
namespace {

// Diagnostics switch. Pinning is the only change on the streaming path that
// reaches into the kernel, and it is the reason 1.5.14 through 1.5.17 died at
// stream start on real hardware: with the placement call skipped the stream
// runs normally. Until the exact failing syscall has been isolated on a
// console, this stays off.
constexpr bool kPinningEnabled = false;

// One placement per thread, ever. Every caller lives on a long-lived streaming
// thread, so a thread-local flag is all that is needed to make this idempotent.
thread_local bool t_placed = false;

} // namespace

bool placeCurrentThread(int coreOrdinal, int priority) {
    if (!kPinningEnabled)
        return false;

    if (t_placed)
        return false;

    s32 currentCore = -1;
    u64 affinityMask = 0;
    if (R_FAILED(svcGetThreadCoreMask(&currentCore, &affinityMask, CUR_THREAD_HANDLE)))
        return false;

    // Pick the ordinal-th core out of the mask the applet actually granted. If
    // the mask is shorter than requested, fall back to the last allowed core
    // rather than refusing to place the thread at all.
    s32 targetCore = -1;
    s32 lastAllowedCore = -1;
    int seen = 0;
    for (s32 core = 0; core < 4; core++) {
        if ((affinityMask & (1ULL << core)) == 0)
            continue;

        lastAllowedCore = core;
        if (seen == coreOrdinal) {
            targetCore = core;
            break;
        }
        seen++;
    }

    if (targetCore < 0)
        targetCore = lastAllowedCore;

    if (targetCore >= 0 && targetCore != currentCore) {
        // Passing the granted mask back unchanged keeps the thread legal; only
        // the ideal core changes. A rejected request means the mask the applet
        // handed us never allowed that core, so leave the thread where it is.
        if (R_FAILED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, targetCore,
                                          affinityMask)))
            return false;
    }

    // The priority is a nice-to-have; the core placement is the point. Report
    // failure instead of ignoring the result, so a rejected value shows up in
    // the log as an unpinned thread rather than as a silent no-op.
    if (priority > 0) {
        if (R_FAILED(svcSetThreadPriority(CUR_THREAD_HANDLE, priority)))
            return false;
    }

    t_placed = true;
    return true;
}

} // namespace switch_thread

#endif
