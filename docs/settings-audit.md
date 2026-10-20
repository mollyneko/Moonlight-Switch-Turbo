# Settings 有效性审计与绑核收益预估（Switch / deko3d）

审计口径：`PLATFORM_SWITCH + BOREALIS_USE_DEKO3D + ENABLE_UPSCALING=ON`
（后者已确认，见 `build/switch/CMakeCache.txt:344`）。

---

## 一、结论

**Switch 上"改了完全没反应"的设置只有 1 项：解码线程数。**

其余设置要么真实生效，要么已经按平台条件隐藏（分辨率 1440p、帧率 120、HDR、硬件解码开关、音频后端）。
所以"设置大面积无效"这个担心可以排除，需要处理的只是一个漏网的条目。

---

## 二、无效项：解码线程数

| | |
|---|---|
| 界面位置 | `settings_tab.cpp:296-315`，无任何平台条件 |
| 取值 | 0 / 2 / 3 / 4，默认 4（`Settings.hpp:359`） |
| 实际效果 | **无**。最终恒定 `thread_count = 1` |
| 原因 | `m_use_decoder_threads = !m_hw_decode_active && setting > 1 && slice_support`（`FFmpegVideoDecoder.cpp:615-616`）；Switch 上硬解是强制的（`Settings.hpp:286-291` → `FFmpegVideoDecoderDekoHelpers.cpp:13-16`），第一项恒为 false |
| 用户感知 | 无论选 0 还是 4，解码路径、CPU 占用、帧率、延迟都不变 |

同一份代码在桌面端是有效的（Linux 软解回退时 `m_hw_decode_active` 会变 false），所以**不能删代码，只能按平台隐藏**。

改法（与同文件里已有的 1440p / 120fps 处理一致）：

```cpp
#if !defined(__SWITCH__)
    decoder->setText("settings/decoder_threads"_i18n);
    ... 现有初始化与订阅 ...
#else
    // 硬件解码固定单线程，本项不适用
    decoder->removeFromSuperView(true);
#endif
```

更友好的做法是保留一行只读信息（"硬件解码：固定单线程"），避免用户以为功能缺失。

---

## 三、已正确按平台隐藏的项（无需改动）

| 设置 | 隐藏位置 | 原因 |
|---|---|---|
| 1440p 分辨率 | `settings_tab.cpp:91-94,230-232` | Switch 最高 1080p 输出 |
| 2.0x 原生缩放 | `:113-116,124-126,136-138` | 2.0x 已超 1080p |
| 120 FPS | `:251-254,270-272,288-290` | Switch 按 60Hz 呈现 |
| 请求 HDR | `:345-347` + `Settings.hpp:140-146` | `SUPPORT_HDR` 只在 macOS/Metal 下定义 |
| 硬件解码开关 | `:360-365` | 非 Linux 桌面平台一律硬解 |
| 音频后端 | `:389-397` | Switch 只链接 Audren |

---

## 四、真实生效的项（抽查确认使用点）

| 设置 | 生效路径 |
|---|---|
| 分辨率 / 原生缩放 | `MoonlightSession.cpp:259,270` → 实际串流分辨率 |
| 帧率、码率、编码格式 | 传给 `LiStartConnection` 的协商参数 |
| 帧缓冲 1/2/3 | `AVFrameHolder::prepare` → `AVFrameQueue::configure`，并联动 `extra_hw_frames`（`FFmpegVideoDecoder.cpp:133-134`） |
| RCAS / 锐化强度 / dithering / upscaling | `DKVideoRenderer::updateRenderState` 每帧检查，实时生效（`Settings.hpp:149-233`） |
| SOPS、在 PC 上播放音频 | `GameStreamClient.cpp:745-746`，服务端行为，Switch 侧可调 |
| 音量 / 音量放大 | `AudrenAudioRenderer::applyVolume`，逐样本缩放 |
| 震动强度、死区、摇杆映射、键盘类型/语言、Overlay 组合键、系统按钮映射 | 输入与 UI 路径 |

两个日志开关都是有效的，但名字容易误解：

- `writeLog` → `brls::Application::enableDebuggingView()`（`main.cpp:191`）= **屏幕上的调试信息叠加**，不写文件
- `logToFile` → `FileLog::setEnabled()`（`main.cpp:149`）= 写 `log.log` 到 SD 卡

---

## 五、两个"不是无效、但默认值值得重新考虑"的点

1. **RCAS 默认开启**（`Settings.hpp:357-358`：`m_enable_rcas = true`，强度 20%）。
   它会每帧多一个全屏 pass 和一条 fence 链。追求最低延迟/最低功耗时，默认 OFF 更合理，
   由用户按画质需求开启。这是**产品默认值**问题，不是 bug。

2. **`frames_queue_size` 默认 2** → surface 池只有 7 帧（`capacityFor(n)=n+5`）。
   队列深度是延迟与抗抖动的直接权衡，默认值偏"低延迟"取向，符合本 fork 定位，保持即可。

---

## 六、绑核后的预期收益（定性与可测，不给虚假百分比）

先说清楚能预期什么、不能预期什么：

**能预期的**

| 现象 | 机制 | 怎么验证 |
|---|---|---|
| 帧时间抖动 / 1% low 改善 | 主线程独占 core 0，不再被 13 个 worker 抢占与跨核迁移打断 | log 里的 `empty_queue`、`pacing_skip` 增量下降 |
| 音频爆音、断音减少 | AudioDec/AudioRecv 独占一核 + 更高优先级，不再被解码突发或 UI 重绘饿死 | 主观听感；audren 欠载计数（需自加埋点） |
| 解码突发时的掉帧减少 | VideoDec 固定 core 2，解码延迟可预测 | `overflow_drop`、`getFrameDropStat` 下降 |
| Overlay 呼出更跟手 | 主线程响应不被 worker 长临界区拖慢 | 主观 + 主线程帧时间分布 |

**不会预期的**

- **平均 FPS 不会提升**。present 被 60Hz vblank 节流，本来就封顶，绑核不会让它变快。
- **平均 CPU 占用不会明显下降**，总工作量不变，只是分布更合理。
- **画质不变**。

**必须注意的风险**

1. applet 模式下可用核可能少于 3 个（core 0 常被系统预留）。`svcSetThreadCoreMask` 失败要回退到"不绑"，不能直接崩。
2. 如果目标核正被系统占用，绑死可能比不绑更差。建议先在 2 个核上验证，再逐步收紧。
3. 音频线程优先级不要低于 `0x1E`（数值越小越高），否则可能反过来饿死主线程的音频混音路径。

**建议的落地顺序**

1. 先只绑 **AudioDec + VideoDec**（收益最直接、风险最小），观察 1–2 天。
2. 再绑 VideoRecv / InputSend。
3. 杂项线程（Ping、LossStats、CtrlAsyncCb 等）最后处理，它们对抖动不敏感。

---

## 七、审计过程中确认的两个事实（供后续参考）

- `SUPPORT_UPSCALING` 在本仓库的 Switch 构建里**是开启的**，所以 RCAS/upscaling/dithering 三个设置是真实可用的，
  `DKVideoRenderer` 里的 `submitUpscalingPresentPass` 也确实参与编译（不是死代码）。
- `decoder_threads` 这个值在 Switch 上虽然无效，但仍会被读进 `m_decoder_threads_setting` 并赋给 `thread_count`，
  随后被 `configure_decoder_context` 的 `enable_decoder_threads=false` 分支覆盖为 1 —— 也就是说它连日志里的
  "requested_threads" 都会显示成 1，无法从日志看出用户设置过什么。
