# Moonlight-Switch-Turbo

**English** | [简体中文](README.zh-CN.md)

A Nintendo Switch client for [Moonlight](https://github.com/moonlight-stream) game streaming, forked from
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch). Switch only. The 1.5.x line cuts
client-side latency; 1.5.14 moves the decode threads off the render core.

---

## Latency: 1.5.0 → 1.5.16

Client-added latency at 60 fps, derived from code parameters. Network and host encode are excluded.
1.5.14 through 1.5.16 changed nothing on these paths.

| | 1.5.0 | 1.5.16 | Change |
|:---|---:|---:|---:|
| **Video** — age of the frame on screen | 1 frame · 16.7 ms | **0 frames · 0 ms** | −16.7 ms |
| **Input** — age of the button press | 8.3 ms | **4.2 ms** | −4.2 ms |
| **Total** | **25.0 ms** | **4.2 ms** | **−20.8 ms (−83%)** |
| **Audio** — lip-sync offset | 62.5 ms | **25.0 ms** | −37.5 ms |

Full derivation, per path: [docs/latency.md](docs/latency.md).

## 1.5.14 — thread placement

The 13 worker threads moonlight-common-c creates had no core mask and no priority, so VideoDec and AudioDec
competed with the render loop that `main.cpp` pins to core 0. They are now placed explicitly:

| Thread | Core | Priority |
|:---|:---|:---|
| AudioDec | 1 | `0x1E` |
| VideoDec | 2 | `0x20` |
| Render loop (unchanged) | 0 | `0x20` |

Both are set from inside their own decode callbacks, so `moonlight-common-c` stays an unmodified submodule.
Placement is idempotent, reads the mask the applet actually grants, and falls back silently if that core is not
available. The decoder-thread-count setting is hidden on Switch, where hardware decoding pins it to one thread.

This is a frame-pacing fix, not a latency cut — the numbers above do not move.

## 1.5.15 — input clean-up

The second gamepad sample of each frame used to run the whole `handleInput()` path again: mouse
polling, touch polling, the unified pad mapping, the scroll accumulator, the keyboard diff. None of
it can have changed in the microseconds since the first sample, and the comment at that call site had
always said only the pad was sampled. It now goes through `handleGamepadOnly()`, which re-reads the
pad and reuses the touch-derived special key — two HID round-trips and a full input derivation off
the path into present.

Also gone: a per-frame 3 s timer that only existed to call `setBottomBarStatus()`, which is empty
outside the SDL platforms, and a redundant `std::fill` in `mapController()` that also used `sizeof`
as an element count.

On the order of 20–40 µs per frame — measurable in a loop, not in a game.

## 1.5.16 — controller mode

A settings switch, **PlayStation** (default) or **Xbox**. It changes the type sent in the controller arrival
event, which is what Sunshine reads when deciding which virtual pad to create. The client used to report
`LI_CTYPE_UNKNOWN` and let the host infer from the declared capabilities — and because the Switch pad declares
a gyroscope, that inference lands on a DualShock.

Xbox mode drops the accelerometer and gyroscope from those capabilities and stops sending motion events, since
an X360 pad has neither. The switch takes effect mid-session: the arrival event is re-sent rather than waiting
for a reconnect.

Also fixed: the arrival event used to be sent from inside the "state changed" branch, so a pad that was
connected and then left alone was never announced at all.

---

## Version history

| Version | Change |
|:---|:---|
| 1.5.0 | Branch baseline |
| 1.5.1 | Frame queue depth made configurable (default 2) |
| 1.5.2 | Input sent before the frame is rendered |
| 1.5.3 | Gamepad sampled twice per frame |
| 1.5.4 | Adaptive queue depth, real client refresh rate |
| 1.5.5 | Session cleaned up on exit, rebuilt after a suspend |
| 1.5.6 | SD-card logging no longer stalls the stream |
| 1.5.7 | Per-frame allocations removed; GPU sync off the critical path |
| 1.5.8 | Redundant copy pass skipped when RCAS is last |
| 1.5.9 | Audio queue made shallow; decode errors no longer swallowed |
| 1.5.10 | Settings that could not do anything removed |
| 1.5.11 | Bitrate moved from a slider to a 5 Mbps ladder |
| 1.5.12 | Home menu title shows Moonlight-Switch-Turbo |
| 1.5.13 | Title shortened to Moonlight-Turbo |
| 1.5.14 | Decode threads moved off the render core; dead decoder setting hidden |
| 1.5.15 | Second gamepad sample made gamepad-only; dead bottom-bar timer removed |
| 1.5.16 | Controller mode switch: PlayStation (default) or Xbox (current) |

## Install

1. Download the latest `Moonlight-Switch-Turbo-*.zip` from [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases)
2. Extract `Moonlight-Switch.nro` to `sdcard:/switch/Moonlight-Switch/`
3. Launch hbmenu through **Title Redirection** (needs full RAM access)

> [!IMPORTANT]
> The home menu title is `Moonlight-Turbo`, but the data directory stays `switch/Moonlight-Switch/`. Settings
> and favourites are unaffected by the rename.

> [!TIP]
> 1080p at high bitrate needs a CPU/GPU overclock — [sys-clk](https://github.com/retronx-team/sys-clk) or
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md). Overclocking carries risk; that is on you.

## Controls

**Mouse** — touch screen to move, tap to click, two fingers to scroll. ZL/ZR act as buttons and the sticks as a
scroll wheel while touching. USB mice work.

**Keyboard** — three-finger tap for the on-screen keyboard; USB keyboards work directly.

**Gamepad** — X360 mapping by default (A/B and X/Y swapped), remappable. Up to 5 controllers, single Joy-Cons
included. Configure Sunshine to see a DS4 for gyro and accelerometer (player 1 only).

**Overlay** — `-` and `+` together, or hold `ESC`. Both the combo and the hold time are configurable.

**NSP forwarder** — add the target to Favourites, then generate one with the
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight). Manual args: `--ip` / `--appid` / `--appname`.

## Building

Requires devkitPro's devkitA64, libnx and the Switch portlibs, plus CMake 3.13+ and Ninja.

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

The artifact lands at `build/switch/Moonlight.nro`. `-DUSE_DEKO3D=ON` selects the deko3d backend, a
prerequisite for the low-latency path. If `uam` is unavailable, pre-compiled `.dksh` files are copied from
`resources/shaders/` instead of failing at configure time.

## Known limitations

- Switch is the only build target; Android, iOS, PSV and desktop are removed
- A jittery link makes the adaptive queue fall back to 1 frame of reserve — latency returns to the 1.5.0 figure
  in exchange for not stuttering, and a deeper configured queue raises that floor (setting 3 settles at 1 frame)
- Touch and mouse don't get the double sample, so they keep the 8.3 ms mean wait
- If the applet does not grant the requested core, decode threads stay unbound — streaming works, the jitter
  benefit does not apply
- SixAxis needs the host to see a DS4; Pro Controller gyro axes aren't passed through natively

## Credits

Upstream [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) and
[moonlight-stream](https://github.com/moonlight-stream). UI on [borealis](https://github.com/XITRIX/borealis),
protocol via [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c).

## License

GPLv3 — see [LICENSE](LICENSE).
