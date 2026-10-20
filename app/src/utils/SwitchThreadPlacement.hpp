#pragma once

// Per-thread core placement for the Switch CPU.
//
// moonlight-common-c creates every worker thread through a plain pthread_create
// with default attributes: no affinity mask and no priority. The Switch applet
// only exposes a handful of cores, so those threads migrate freely and
// regularly land on the render loop's core, which shows up as frame-time
// jitter, audio underruns and bursty decode stalls.
//
// Placement is applied from *inside* the thread instead of from its creator.
// That keeps the change on our side of the submodule boundary: the streaming
// callbacks below are only ever invoked on the thread we want to move, so we
// never need a handle to a thread owned by moonlight-common-c.

namespace switch_thread {

#ifdef __SWITCH__

// Move the calling thread onto the ordinal-th core this process is allowed to
// use, then apply a priority. Safe to call repeatedly: the first successful
// call wins and later ones are no-ops. A failure (for example a core the applet
// does not expose) is ignored so a restrictive mask can never take the stream
// down.
//
// Returns true only for the call that actually performed the placement, so the
// caller can log it once.
bool placeCurrentThread(int coreOrdinal, int priority);

// libnx priorities: numerically lower runs first. The UI thread is 0x20, so
// audio sits just above it and decoding shares its level. The defaults from
// pthread_create are 0x2C.
constexpr int kPriorityAudio = 0x1E;
constexpr int kPriorityVideoDecode = 0x20;
constexpr int kPriorityBackground = 0x28;

// Core ordinals, resolved against the mask the applet actually granted rather
// than assumed to be 0/1/2. Ordinal 0 is reserved for the UI thread.
constexpr int kCoreAudio = 1;
constexpr int kCoreVideoDecode = 2;
constexpr int kCoreBackground = 2;

#else

inline bool placeCurrentThread(int, int) { return false; }

constexpr int kPriorityAudio = 0;
constexpr int kPriorityVideoDecode = 0;
constexpr int kPriorityBackground = 0;
constexpr int kCoreAudio = 0;
constexpr int kCoreVideoDecode = 0;
constexpr int kCoreBackground = 0;

#endif

} // namespace switch_thread
