# Latency: 1.5.0 → 1.5.15

Detailed derivation behind the table in the [README](README.md). Three paths — video, audio, controller input —
each with the code parameters it rests on.


The figures are derived from the code at **60 fps (one frame = 16.7 ms)** and cover only the latency the client
adds on top of the host.

> 1.5.12 and 1.5.13 changed nothing but the title shown in the home menu, and 1.5.14 changed how threads are
> placed rather than how many frames are held. These numbers therefore describe the shipped build.

### At a glance

| Path | What the number bounds | 1.5.0 | 1.5.15 | Change |
|:---|:---|---:|---:|---:|
| **Video** — queue residency | age of the frame on screen | 1.00 frame · 16.7 ms | 0.00 frame · **0 ms** | −16.7 ms |
| **Input** — sampling wait (mean) | age of the button press | 0.50 frame · 8.3 ms | 0.25 frame · **4.2 ms** | −4.2 ms |
| **Video + input** | what the client adds end to end | 1.50 frame · **25.0 ms** | 0.25 frame · **4.2 ms** | **−20.8 ms (−83%)** |
| **Audio** — playout depth | lip-sync offset | 5 × 12.5 ms · **62.5 ms** | 2 × 12.5 ms · **25.0 ms** | −37.5 ms |

Audio is a separate path: it bounds how far the sound trails the picture, not how old the frame is, so it is not
part of the 25.0 → 4.2 ms total.

---

## The three paths

### Video path — decode → frame queue → post-process → present

| Change | 1.5.0 | 1.5.15 | Effect |
|:---|:---|:---|:---|
| Queue residency | fixed reserve `min(depth − 1, 2)` for the whole session — 1 frame at the default | adaptive: starts at the same value, released to **0 frames** after 5 s of clean play | −16.7 ms of display age |
| Reserve recovery | the whole reserve is rebuilt after every ordinary miss, in freeze-and-catch-up cycles | one frame handed back per 5 s clean; an underrun restores it and doubles the wait (capped at 60 s) | no periodic stutter |
| Pacing convergence | 0.01 gain, 0.08 frames/draw drain cap — 229 draws (3.8 s) to shed a backlog | 0.06 gain, 0.40 frames/draw — 37 draws (0.62 s) | 3.8 s → 0.6 s tail |
| Catch-up bound | `min(credit, queue limit)` — a noisy cadence estimate could discard 5–13 frames at once | `min(credit, backlog + 1)` — at target there is no backlog, so no catch-up | burst drops bounded |
| GPU fence | signal then wait back to back, inside the draw; the CPU is parked on the GPU | signal in this draw, wait at the **top of the next** — input sampling and the UI pass run during the GPU work | 1–2 ms/frame off the input path |
| Post-processing | EASU → RCAS → copy into the framebuffer | EASU → RCAS **written straight into the framebuffer** when dithering is off | ~0.65 ms/frame of bandwidth saved at 1080p |
| Decode | `LOW_DELAY` + `FAST` + `SHOW_ALL` + `OUTPUT_CORRUPT`, `FF_THREAD_SLICE` | identical | 0 |

Only two of these move a number; the rest keep the pipeline from stalling. Residency still depends on what the
frame queue setting is, and a deeper queue costs more:

| Frame queue setting | 1.5.0 residency | 1.5.15 residency (steady) |
|:---|---:|---:|
| 1 | 0 ms | 0 ms |
| 2 (default) | 16.7 ms | **0 ms** |
| 3 | 33.3 ms | **16.7 ms** |

The hard capacity stays `depth + 5` frames (`kBurstHeadroomFrames`) — the reserve NVDEC needs to avoid dropping
surfaces silently during a burst.

### Audio path — Opus decode → wave buffers → audren

| Change | 1.5.0 | 1.5.15 | Effect |
|:---|:---|:---|:---|
| Pool cap | 5 wave buffers, whatever the pool held | **2** (`kMaximumQueuedWaveBuffers`); the allocation stays at 5 | 62.5 → 25.0 ms |
| Backpressure | `write_audio()` blocks when the pool is full, so it was permanently full | whole frames dropped once `LiGetPendingAudioDuration() > 30` ms | the cap is real, not nominal |
| Drift correction | drop a buffer every 400 calls, behind a 0.5 s desync guard that the blocking write made unreachable | both removed — nothing left to correct | no periodic click |
| Frame and mix | 12.5 ms Opus frame, 2 mix buffers, 24 voices | identical | fixed cost, not counted |

A dropped frame is a single click; the old behaviour was 62.5 ms of lip-sync error for the entire session.

### Input path — poll → map → diff → UDP

| Change | 1.5.0 | 1.5.15 | Effect |
|:---|:---|:---|:---|
| Sample rate | 1 per frame — 16.7 ms interval | **2 per frame** (gamepad only) — 8.3 ms interval | 8.3 → 4.2 ms mean wait |
| Sample position | after the render pass is submitted, so the packet waited a whole pass | **before** it; the second sample is the last work before present | send delay ≈ 0 |
| Transmit | diffed per controller, only a changed state is sent | identical | 0 |
| Touch, mouse, keyboard | sampled at frame start | same — the second sample covers the pad only | 8.3 ms, unchanged |
| Per-frame CPU | `fsqrt_` bit-trick root, `std::vector` per call, pad re-polled by the overlay check, settings copied | squared-distance compare, static buffer, `rawControllerState()`, `const auto&` | what makes the second sample affordable |

The 4.2 ms figure bounds *press detected*, not *packet sent* — the send now trails the sample by microseconds
instead of by a render pass.

### Frame-time stability — 1.5.14

This release changes no number above. What it changes is variance.

moonlight-common-c creates its 13 worker threads with a plain `pthread_create` and no platform setup, so on
Switch they inherited no core mask and no priority — all of them ran at the default `0x2C` alongside the render
loop, which `main.cpp` pins to core 0 at `0x20`. VideoDec and AudioDec are periodic, latency-sensitive loads;
landing on the render core shows up as jittered frame times and audio crackle while total CPU use still looks
low. 1.5.14 places them explicitly:

| Thread | Core | Priority |
|:---|:---|:---|
| AudioDec | 1 | `0x1E` (one step above the render loop) |
| VideoDec | 2 | `0x20` |
| Render loop (unchanged) | 0 | `0x20` |

Both are applied from inside the decode callbacks — `submit_decode_unit` and `decode_and_play_sample` only ever
run on their respective thread — so `moonlight-common-c` stays an unmodified submodule. The placement is
idempotent (one syscall on the first frame), reads the core mask the applet actually grants instead of assuming
three cores are available, and falls back silently if the mask does not reach the requested core.

The **decoder thread count** setting is hidden on this build: hardware decoding pins the decoder to one thread,
so the option had no effect on Switch. It remains available on desktop builds, where software decode uses it.

Expect better frame pacing and a higher 1% low, and fewer audio underruns. Do not expect a lower average
latency — the client-side budget in this document does not move.

---

## On one timeline

```
one frame = 16.7 ms, 60 Hz
0                                                                    16.7 ms
|-------------------------------------------------------------------------|
1.5.0
 video  |---------------- render + submit ----------------|
        |=== queue: 1 frame held ===|                (16.7 ms of age)
 input                                                     [sample] -> send
         ^ press anywhere here waits up to a full frame

1.5.15
 input  [sample] -> send                                     [sample] -> send
 video  |---------- render + submit ----------|
        | queue floor: 0 frames |                          (0 ms of age)
         ^ press waits at most half a frame
```

### Combined client-side budget

| Term | 1.5.0 | 1.5.15 |
|:---|---:|---:|
| Video residency | 16.7 ms | 0 ms |
| Input sampling wait | 8.3 ms | 4.2 ms |
| **Total** | **25.0 ms** | **4.2 ms** |

At 30 fps the frame period doubles and both terms double with it; at 120 fps they halve. The Switch outputs
60 Hz in handheld and docked, which is what the client advertises (`clientRefreshRateX100 = 6000`).

| Refresh rate | Frame period | 1.5.0 | 1.5.15 | Change |
|:---|---:|---:|---:|---:|
| 30 fps | 33.3 ms | 50.0 ms | **8.3 ms** | −41.7 ms |
| 60 fps | 16.7 ms | 25.0 ms | **4.2 ms** | −20.8 ms |

---

## How the numbers are derived

```
T_frame      = 1000 / refresh rate                  -> 16.67 ms at 60 Hz
T_sample     = T_frame / samples per frame / 2      -> mean wait for a press
T_video      = target buffered frames x T_frame     -> frames held before display
T_input      = T_sample
T_client     = T_video + T_input

T_audio      = max queued wave buffers x 12.5 ms    -> lip-sync offset, separate path
```

```
1.5.0    video  1 x 16.67                   = 16.67
         input  16.67 / 1 / 2              =  8.33
         total                               25.00 ms
         audio  5 x 12.5                   = 62.50 ms

1.5.15   video  0 x 16.67                   =  0.00
         input  16.67 / 2 / 2              =  4.17
         total                                4.17 ms
         audio  2 x 12.5                   = 25.00 ms
```

The values in the tables are rounded to one decimal; the 4.2 ms figure is 4.17.

**Not counted:** network RTT, host capture and encode, Switch-side FFmpeg decode, display scanout, audio
hardware latency, and the audren mix buffers. These are identical between the two versions and are excluded on
purpose — this document measures only what the client changed. The fence and copy-pass entries in the video
table are marked as estimates because their exact cost depends on the GPU clock and the resolution.

> [!NOTE]
> These are theoretical values derived from code parameters, not lab measurements. Real latency also depends
> on link jitter and the host encoder.
>
> As a reference point, one measured session at 4 ms RTT showed ~68 ms end-to-end, of which ~60 ms was queue
> backlog — precisely the part this line attacks. That measurement predates the pacing fix, so it is an upper
> bound rather than a description of the current build.

---
