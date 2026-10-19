# Moonlight-Switch-Turbo

**English** | [简体中文](README.zh-CN.md)

A Nintendo Switch client for [Moonlight](https://github.com/moonlight-stream) game streaming, forked from
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch).

This branch keeps a single target — the Switch — and spends its 1.5.x line removing frames of latency from the
client path: input leaves before the frame is rendered, the gamepad is sampled twice per frame, the video queue
adapts down to zero frames of reserve, and the audio pool is capped at 25 ms.

---

## Latency: 1.5.0 → 1.5.11

1.5.11 is the version where the latency work landed in full. Everything below is derived from the code, at
**60 fps (one frame = 16.67 ms)**, and covers only the latency the client adds on top of the host. Each of the
three paths — video, audio, controller input — is broken into its sub-stages, because they fail and improve for
different reasons.

### At a glance

| Path | What the number bounds | 1.5.0 | 1.5.11 | Change |
|:---|:---|---:|---:|---:|
| **Video** — queue residency | age of the frame on screen | 1.00 frame · 16.7 ms | 0.00 frame · **0 ms** | −16.7 ms |
| **Input** — sampling wait (mean) | age of the button press | 0.50 frame · 8.3 ms | 0.25 frame · **4.2 ms** | −4.2 ms |
| **Video + input** | what the client adds end to end | 1.50 frame · **25.0 ms** | 0.25 frame · **4.2 ms** | **−20.8 ms (−83%)** |
| **Audio** — playout depth | lip-sync offset | 5 × 12.5 ms · **62.5 ms** | 2 × 12.5 ms · **25.0 ms** | −37.5 ms |

Audio is a separate path: it bounds how far the sound trails the picture, not how old the frame is, so it is not
part of the 25.0 → 4.2 ms total.

---

## 1. Video path

Decode → frame queue → post-process → present. Only two of those stages changed in a way that moves a number;
the rest changed in ways that keep the pipeline from stalling.

### 1.1 Queue residency — 16.7 → 0 ms

The queue exists to absorb network jitter. Every frame held in it is a frame of latency paid on every frame,
whether or not jitter occurs.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Target depth | `min(configured depth − 1, 2)`, fixed for the session | same value as a baseline, then adaptive |
| Default target (depth 2) | 1 frame · 16.7 ms | 1 frame for the first 5 s, then **0 frames** |
| Floor | the baseline itself | 0 frames when depth ≥ 2 |
| Hard capacity | `depth + 5` frames (`kBurstHeadroomFrames`) | same |
| Steady-state residency | 16.7 ms | **0 ms** |
| Residency after an underrun | 16.7 ms | 16.7 ms — the reserve comes back |

With a deeper configured queue the fixed reserve costs more, which is why 1.5.0 degrades faster than 1.5.11 as
the setting goes up:

| Frame queue setting | 1.5.0 target | 1.5.11 target (steady) | 1.5.0 residency | 1.5.11 residency |
|:---|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 ms | 0 ms |
| 2 (default) | 1 | 0 | 16.7 ms | **0 ms** |
| 3 | 2 | 1 | 33.3 ms | **16.7 ms** |

### 1.2 Pacing convergence — 3.8 s → 0.6 s

Being configured for a shallow queue is not the same as sitting at it. The pacing loop nudges the playout rate
toward the target every draw; if the correction is too weak, a burst leaves the queue deep and it stays deep for
the rest of the session.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Occupancy gain | 0.01 per frame | **0.06 per frame** |
| Correction at a 2-frame error | 0.02 frames/draw | **0.12 frames/draw** |
| Maximum drain | 0.08 frames/draw · 4.8 frames/s | **0.40 frames/draw · 24 frames/s** |
| Maximum hold | 0.08 frames/draw | 0.08 frames/draw (deliberately unchanged) |
| Time to remove 90% of a backlog | `ln(0.1)/ln(0.99)` = 229 draws · **3.8 s** | `ln(0.1)/ln(0.94)` = 37 draws · **0.62 s** |
| Catch-up bound | `min(credit, queue limit)` — a noisy cadence estimate could discard 5–13 frames in one burst | `min(credit, backlog + 1)` — zero backlog at target means no catch-up |

The asymmetry is intentional. Draining hard drops frames that are already late, which is the problem being
fixed. Holding a frame to rebuild reserve shows the same image twice, which is judder that buys nothing — so
that direction stays at the old, slow rate.

### 1.3 GPU submission and fence placement

The post-process chain reuses render targets and uniform buffers across frames, so before the next frame may
overwrite them, the previous chain has to be finished. Where that wait sits decides whether the CPU is parked
on the GPU.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Signal + wait | `queueSignalFence()` then `queueWaitFence()` back to back, inside the draw | signal in this draw, wait at the **top of the next draw** |
| What runs during the GPU work | nothing — the CPU is blocked | input sampling, overlay checks, UI pass of the previous frame |
| Cost on the input path | one post-process chain per frame (order of 1–2 ms at 1080p) | 0 |

The fence is only re-signalled after the deferred wait, so the signal cannot be lost across frames.

### 1.4 Post-processing chain

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Stages | EASU upscale → RCAS sharpen → copy into the framebuffer | EASU → RCAS **written straight into the framebuffer** when dithering is off |
| Extra full-resolution copy | yes, one per frame | none in the RCAS-only path |
| Approximate cost of that copy at 1080p | ~8.3 MB write + ~8.3 MB read ≈ 0.65 ms of memory bandwidth at 25.6 GB/s | 0 |

Dithering still forces the intermediate target, because that pass has to read the RCAS output. The copy is only
dropped when RCAS is the last stage.

### 1.5 Startup and underrun recovery

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Startup | fill the reserve, and rebuild the **whole** reserve after every ordinary miss | fill the reserve once, then hand frames back |
| Recovery cadence | freeze-and-catch-up cycles, each one a visible stutter | 5 s without an underrun hands back one frame; an underrun restores it and doubles the wait (capped at 12×, i.e. 60 s) |
| Resync after a suspension | large burst of overdue frames | draw interval > 250 ms resets the credit and skips the burst |

### 1.6 Decode — unchanged

For completeness, since it is often assumed to be part of the win: the decoder configuration is the same in
both versions — `AV_CODEC_FLAG_LOW_DELAY`, `AV_CODEC_FLAG2_FAST`, `SHOW_ALL`, `OUTPUT_CORRUPT`, and
`FF_THREAD_SLICE` with the configured thread count. Decode latency did not move between 1.5.0 and 1.5.11; what
moved is everything downstream of it.

### Video sub-total

| Sub-stage | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|
| 1.1 Queue residency | 16.7 ms | 0 ms | −16.7 ms |
| 1.2 Convergence (transient) | up to 3.8 s of elevated depth | up to 0.6 s | −3.2 s of tail |
| 1.3 GPU fence on the CPU path | 1–2 ms per frame | 0 ms | off the path |
| 1.4 Redundant copy pass | ~0.65 ms | 0 ms | −0.65 ms |
| 1.5 Startup rebuild | recurring | one-time | no periodic stutter |
| 1.6 Decode | unchanged | unchanged | 0 |
| **Display-residency path** | **16.7 ms** | **0 ms** | **−16.7 ms** |

---

## 2. Audio path

Opus decode → wave buffers → audren. This path does not gate frame age, but it gates lip-sync, and in 1.5.0 it
was pinned at its worst case for the entire session.

### 2.1 Pool depth — 62.5 → 25 ms

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Opus frame | 12.5 ms (600 samples @ 48 kHz) | same |
| Wave buffers allocated | 5 | 5 (unchanged) |
| Wave buffers allowed to queue | 5 — whatever the pool held | **2** (`kMaximumQueuedWaveBuffers`) |
| Worst-case buffered audio | 5 × 12.5 = **62.5 ms** | 2 × 12.5 = **25.0 ms** |
| Steady-state buffered audio | 62.5 ms, because the pool stayed full | ≤ 25.0 ms |

The allocation is untouched; 1.5.11 simply refuses to queue into more than two of them.

### 2.2 Backpressure policy — blocking → dropping

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| When the pool is full | `write_audio()` blocks until a wave buffer frees | whole frames are dropped once `LiGetPendingAudioDuration() > 30` ms |
| Effect on the pool | permanently full — the 0.5 s desync guard behind it was unreachable | bounded at 2 buffers |
| Consequence | audio late by 62.5 ms for the whole session | audio late by at most 25 ms; a dropped frame is a click, not accumulating lag |

This is the single change that made the pool depth real rather than nominal.

### 2.3 Drift correction — removed

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Mechanism | drop one buffer every 400 calls | removed |
| 0.5 s desync guard | present in code, never reached because the write blocked first | removed |
| Why | it was compensating for a backlog that the cap now prevents | — |

### 2.4 Frame and mix buffering

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Opus frame | 12.5 ms | 12.5 ms |
| audren mix buffers | 2 | 2 |
| Voice count | 24 | 24 |

Both are fixed costs and are not counted in the delta.

### Audio sub-total

| Sub-stage | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|
| 2.1 Queue depth | 62.5 ms | 25.0 ms | −37.5 ms |
| 2.2 Backpressure | pinned at the maximum | bounded, overflow dropped | keeps 2.1 from regressing |
| 2.3 Drift correction | periodic drop | none needed | −1 click per 400 frames |
| 2.4 Frame / mix | 12.5 ms / 2 buffers | unchanged | 0 |
| **Playout depth** | **62.5 ms** | **25.0 ms** | **−37.5 ms (−60%)** |

---

## 3. Controller input path

Poll → map → diff → UDP. The sub-stages below are the whole path; the 1.5.x line changed the first and the
third, and removed enough per-frame work that the poll itself is cheaper.

### 3.1 Sample cadence — 1/frame → 2/frame

A press can land anywhere inside a sampling interval. If sampling is uniform, the expected wait is half the
interval.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Samples per frame | 1 | **2** |
| Sampling interval | 16.7 ms | **8.3 ms** |
| Mean wait for a press | 8.3 ms | **4.2 ms** |
| Worst-case wait | 16.7 ms | **8.3 ms** |

The second sample covers the gamepad only. Keyboard diffing and the bottom-bar timer stay on the frame-start
call, so their behaviour is unchanged.

### 3.2 Sample position within the frame

Cadence is not the whole story — a packet that is sampled early but sent late is just as late.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Where the (only / first) sample runs | after the render pass is submitted | **before** the render pass |
| Extra wait before the packet leaves | the render pass duration | 0 |
| Second sample position | — | last work in the draw callback, immediately before present |

This is why sitting at 4.2 ms of sampling wait is the number for *press detected*, not for *packet sent*. The
send follows the sample by a few microseconds instead of by a whole render pass.

### 3.3 Transmit policy — diffed, unchanged

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Comparison | `GamepadState::is_equal` per controller | same |
| What is sent | only a changed state | only a changed state |
| Cost when idle | one comparison per controller per sample | two comparisons — still negligible |

Diffing means a press is transmitted on the first sample that observes it, which is exactly the sampling wait
above. It also means holding a button costs no bandwidth in either version.

### 3.4 Touch, mouse and keyboard sub-path

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Sampled at | frame start | frame start |
| Sampled by the second call | — | no |
| Mean wait | 8.3 ms | 8.3 ms (unchanged) |

Touch and mouse did not get faster, and this document does not claim they did. The double sample was applied
where it was cheap and safe: to the pad.

### 3.5 Per-frame CPU work removed

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Stick deadzone | `fsqrt_` bit-trick square root, then compare | squared-distance compare — exact, no root |
| Touch state buffer | `std::vector` constructed per call | static, cleared and reused |
| Controller snapshot | polled again by the overlay combo check | reused via `rawControllerState()` |
| Settings copy | `auto guideKeys` — a copy per call | `const auto&` |

Individually sub-millisecond; together they are what makes a second sample per frame affordable instead of
doubling the cost of the input path.

### Input sub-total

| Sub-stage | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|
| 3.1 Sampling wait (mean) | 8.3 ms | 4.2 ms | −4.2 ms |
| 3.1 Sampling wait (worst) | 16.7 ms | 8.3 ms | −8.3 ms |
| 3.2 Send offset | render pass duration | ~0 | −1 render pass |
| 3.3 Diff / transmit | on change | on change | 0 |
| 3.4 Touch / mouse | 8.3 ms | 8.3 ms | 0 |
| 3.5 Per-frame CPU | root + allocations | compare + reuse | sub-ms |
| **Press-to-packet (mean)** | **8.3 ms** | **4.2 ms** | **−4.2 ms (−50%)** |

---

## 4. The three paths on one timeline

```
one frame = 16.7 ms, 60 Hz
0                                                                    16.7 ms
|-------------------------------------------------------------------------|
1.5.0
 video  |---------------- render + submit ----------------|
        |=== queue: 1 frame held ===|                (16.7 ms of age)
 input                                                     [sample] -> send
         ^ press anywhere here waits up to a full frame

1.5.11
 input  [sample] -> send                                     [sample] -> send
 video  |---------- render + submit ----------|
        | queue floor: 0 frames |                          (0 ms of age)
         ^ press waits at most half a frame
```

### Combined client-side budget

| Term | 1.5.0 | 1.5.11 |
|:---|---:|---:|
| Video residency | 16.7 ms | 0 ms |
| Input sampling wait | 8.3 ms | 4.2 ms |
| **Total** | **25.0 ms** | **4.2 ms** |

At 30 fps the frame period doubles and both terms double with it; at 120 fps they halve. The Switch outputs
60 Hz in handheld and docked, which is what the client advertises (`clientRefreshRateX100 = 6000`).

| Refresh rate | Frame period | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|---:|
| 30 fps | 33.3 ms | 50.0 ms | **8.3 ms** | −41.7 ms |
| 60 fps | 16.7 ms | 25.0 ms | **4.2 ms** | −20.8 ms |

---

## 5. How the numbers are derived

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

1.5.11   video  0 x 16.67                   =  0.00
         input  16.67 / 2 / 2              =  4.17
         total                                4.17 ms
         audio  2 x 12.5                   = 25.00 ms
```

The values in the tables are rounded to one decimal; the 4.2 ms figure is 4.17.

**Not counted:** network RTT, host capture and encode, Switch-side FFmpeg decode, display scanout, audio
hardware latency, and the audren mix buffers. These are identical between the two versions and are excluded on
purpose — this document measures only what the client changed. The fence and copy-pass entries in §1.3 and
§1.4 are marked as estimates because their exact cost depends on the GPU clock and the resolution.

> [!NOTE]
> These are theoretical values derived from code parameters, not lab measurements. Real latency also depends
> on link jitter and the host encoder.
>
> As a reference point, one measured session at 4 ms RTT showed ~68 ms end-to-end, of which ~60 ms was queue
> backlog — precisely the part 1.5.11 attacks. That measurement predates the pacing fix, so it is an upper
> bound rather than a description of the current build.

---

## Version history

| Version | Change |
|:---|:---|
| 1.5.0 | Branch baseline |
| 1.5.1 | Video frame queue depth made configurable (default 2) |
| 1.5.2 | Input sent before the frame is rendered |
| 1.5.3 | Gamepad sampled twice per frame |
| 1.5.4 | Adaptive frame queue depth, real client refresh rate |
| 1.5.5 | Streaming session cleaned up on exit, rebuilt after a suspend |
| 1.5.6 | SD-card logging no longer stalls the stream; catch-up discard bounded |
| 1.5.7 | Per-frame allocations removed; GPU sync moved off the critical path |
| 1.5.8 | Redundant copy pass skipped when RCAS is the last post-processing stage |
| 1.5.9 | Audio queue made shallow; decode errors no longer swallowed |
| 1.5.10 | Settings that could not do anything removed, defaults tightened |
| 1.5.11 | Bitrate moved from a slider to a 5 Mbps ladder |
| 1.5.12 | Home menu title shows Moonlight-Switch-Turbo |
| 1.5.13 | Title shortened to Moonlight-Turbo (current) |

---

## Install

1. Download the latest `Moonlight-Switch-Turbo-*.zip` from [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases)
2. Extract `Moonlight-Switch.nro` to `sdcard:/switch/Moonlight-Switch/`
3. Launch hbmenu through **Title Redirection** (needs full RAM access)
4. Start Moonlight-Turbo

> [!IMPORTANT]
> The home menu title is `Moonlight-Turbo`, but the data directory stays `switch/Moonlight-Switch/`. Your
> settings and favourites live there and are unaffected by the rename.

> [!TIP]
> 1080p at high bitrate needs a CPU/GPU overclock — [sys-clk](https://github.com/retronx-team/sys-clk) or
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md). Overclocking carries risk; that is on you.

---

## Controls

**Mouse** — move the cursor with the touch screen, tap to left-click, scroll with two fingers. While touching,
ZL/ZR act as left/right buttons and the L/R sticks act as a scroll wheel. USB mice work too.

**Keyboard** — tap the screen with three fingers for the on-screen keyboard; USB keyboards work directly.

**Gamepad** — mapped as an X360 pad by default (A/B and X/Y swapped), remappable in settings. Up to 5
controllers including handheld mode, and single Joy-Cons are supported.

**SixAxis** — configure Sunshine to recognise the controller as a DS4 to get gyro and accelerometer. Player 1 only.

**In-game overlay** — press `-` and `+` together by default, or hold `ESC` on a keyboard. Both the combo and
the hold time are configurable.

**NSP forwarder** — start a stream directly with predefined arguments. Add the target app to Favourites first,
then generate one with the [NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight). Manually you
pass `--ip` / `--appid` / `--appname`, e.g. `--ip=192.168.1.101 --appid=1233211234 --appname=Steam`.

---

## Building

Requires devkitPro's devkitA64, libnx and the Switch portlibs, plus CMake 3.13+ and Ninja.

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

The artifact lands at `build/switch/Moonlight.nro`. `-DUSE_DEKO3D=ON` selects the deko3d backend, which is a
prerequisite for the low-latency path.

If `uam` (the deko3d shader compiler) is unavailable, the build copies the pre-compiled `.dksh` files from
`resources/shaders/` instead of failing at configure time.

---

## Known limitations

- Switch is the only build target; Android, iOS, PSV and desktop are removed
- The adaptive queue falls back to one frame of reserve when the link keeps jittering — latency returns to
  the 1.5.0 figure in exchange for not stuttering
- The deeper the configured queue, the higher the adaptive floor stays (setting 3 settles at 1 frame, not 0)
- Touch and mouse input did not get the double sample, so they keep the 8.3 ms mean wait
- SixAxis needs the host to see a DS4; Switch Pro Controller gyro axes are not passed through natively

## Credits

Upstream [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) and
[moonlight-stream](https://github.com/moonlight-stream). UI built on
[borealis](https://github.com/XITRIX/borealis), streaming protocol via
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c).

## License

GPLv3 — see [LICENSE](LICENSE).
