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
**60 fps (one frame = 16.67 ms)**, and covers only the latency the client adds on top of the host.

### Client-side budget

| Stage | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|
| Input sampling wait — mean | 0.50 frame · 8.3 ms | 0.25 frame · **4.2 ms** | −4.2 ms |
| Input sampling wait — worst case | 1.00 frame · 16.7 ms | 0.50 frame · **8.3 ms** | −8.3 ms |
| Input send offset | after the render pass | before the render pass | − one draw pass |
| Video queue residency — steady | 1.00 frame · 16.7 ms | 0.00 frame · **0 ms** | −16.7 ms |
| Video queue residency — degraded | 1.00 frame · 16.7 ms | 1.00 frame · 16.7 ms | 0 (by design) |
| **Client-side added latency** | **1.50 frames · 25.0 ms** | **0.25 frames · 4.2 ms** | **−1.25 frames · −20.8 ms (−83%)** |
| Audio pool depth (worst case) | 5 × 12.5 ms · 62.5 ms | 2 × 12.5 ms · **25.0 ms** | −37.5 ms |

The last row is a separate path — it bounds lip-sync, not the frame path, so it is not part of the 25.0 → 4.2 ms
total.

### Where a frame goes

```
1.5.0    one frame (16.7 ms)
         +---------------------------+---------------------------+
         |  render + submit frame    |  sample input -> send     |
         +---------------------------+---------------------------+
                                     ^ a press landing here waits a full frame

1.5.11   one frame (16.7 ms)
         +-------------+-----------------------------------+-------------+
         | sample+send |        render + submit frame      | sample+send |
         +-------------+-----------------------------------+-------------+
         ^                                                 ^
         gap between samples is now half a frame, not a whole one
```

### Queue depth sensitivity

`D_target = min(configured depth − 1, 2)` in 1.5.0; 1.5.11 starts there and walks one frame down once the link
proves it can keep up.

| Frame queue setting | 1.5.0 target | 1.5.11 target (steady) | 1.5.0 total | 1.5.11 total |
|:---|---:|---:|---:|---:|
| 1 | 0 | 0 | 8.3 ms | **4.2 ms** |
| 2 (default) | 1 | 0 | 25.0 ms | **4.2 ms** |
| 3 | 2 | 1 | 41.7 ms | **20.9 ms** |

The gap widens with the setting: the deeper the configured queue, the more the fixed reserve costs in 1.5.0.

### Frame rate scaling

Both terms scale linearly with the frame period, so the gain doubles at 30 fps and halves at 120 fps.

| Refresh rate | Frame period | 1.5.0 | 1.5.11 | Change |
|:---|---:|---:|---:|---:|
| 30 fps | 33.3 ms | 50.0 ms | **8.3 ms** | −41.7 ms |
| 60 fps | 16.7 ms | 25.0 ms | **4.2 ms** | −20.8 ms |

The Switch outputs 60 Hz in both handheld and docked mode, which is what the client advertises to the host
(`clientRefreshRateX100 = 6000`).

### Audio path

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Wave buffer | 12.5 ms (600 samples @ 48 kHz) | 12.5 ms |
| Maximum pooled buffers | 5 | 2 |
| Worst-case buffered audio | 62.5 ms | **25.0 ms** |
| Backpressure | `write_audio()` blocks, pool stays full for the whole session | drop whole frames once pending audio exceeds 30 ms |
| Periodic correction | drop one buffer every 400 samples | removed — the cap makes it unnecessary |

### Pacing behaviour

The queue has to be able to *converge* on its target, not just be configured for it.

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| Occupancy gain | 0.01 per frame | **0.06 per frame** |
| Maximum correction | 0.08 frames (both directions) | **0.40** draining, 0.08 holding |
| Max drain rate @ 60 fps | 0.08 frame/draw ≈ 4.8 frames/s | **0.40 frame/draw ≈ 24 frames/s** |
| Catch-up bound | `min(credit, queue limit)` — a noisy cadence estimate could discard 5–13 frames in one burst | `min(credit, backlog + 1)` — zero backlog at target means no catch-up |
| Reserve handback | never | one frame after 5 s without an underrun; an underrun restores it and doubles the next window |

The asymmetric cap is deliberate: draining too much drops frames that are already late, while holding a frame
to rebuild reserve shows the same image twice, which is judder that buys nothing.

### How the numbers are derived

```
T_frame   = 1000 / refresh rate                  -> 16.67 ms at 60 Hz
T_sample  = T_frame / samples per frame / 2      -> mean wait for a button press
T_queue   = target buffered frames x T_frame     -> frames held before display
L_client  = T_sample + T_queue
```

```
1.5.0    16.67 / 1 / 2 + 1 x 16.67 =  8.33 + 16.67 = 25.0 ms
1.5.11   16.67 / 2 / 2 + 0 x 16.67 =  4.17 +  0.00 =  4.2 ms
```

1.5.11 takes its second sample as the last thing the draw callback does, right before the frame is presented;
keyboard diffing and the bottom-bar timer stay on the single frame-start call.

**Not counted:** network RTT, host capture and encode, Switch-side FFmpeg decode, display scanout, and audio
hardware latency. These are identical between the two versions and are deliberately excluded — this table
measures only what the client changed.

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
- SixAxis needs the host to see a DS4; Switch Pro Controller gyro axes are not passed through natively

## Credits

Upstream [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) and
[moonlight-stream](https://github.com/moonlight-stream). UI built on
[borealis](https://github.com/XITRIX/borealis), streaming protocol via
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c).

## License

GPLv3 — see [LICENSE](LICENSE).
