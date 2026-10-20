# 并发与多线程优化调研（Switch / deko3d 路径）

调研对象：本仓库当前 `main`（1.5.13）在 `PLATFORM_SWITCH + BOREALIS_USE_DEKO3D` 下的运行时线程结构。
结论先行：**主线程的 CPU 不是瓶颈，GPU 提交与 vblank 才是。这里"加线程"几乎换不到帧率，真正的收益在调度（绑核/优先级）与 queue 分离。**

---

## 1. 现状：13 个 worker 线程，3 个核

视频/音频/控制全部由 moonlight-common-c 自己起线程，经 `PltCreateThread` 落到 `pthread_create`
（`extern/moonlight-common-c/src/PlatformStuff.c:382-401`，Switch 走 `#else` 分支）。

| 线程 | 创建点 | 职责 | 绑定核 | 优先级 |
|---|---|---|---|---|
| VideoRecv | `VideoStream.c:341` | UDP 收包 + depacketize | 无 | 默认 0x2C |
| VideoDec | `VideoStream.c:350` | FFmpeg → NVDEC 提交 → 入帧队列 | 无 | 默认 0x2C |
| VideoPing | `VideoStream.c:384` | 链路探测 | 无 | 默认 0x2C |
| AudioRecv | `AudioStream.c:449` | 音频 UDP 收包 | 无 | 默认 0x2C |
| AudioDec | `AudioStream.c:458` | Opus 解码 → audren | 无 | 默认 0x2C |
| AudioPing | `AudioStream.c:103` | 链路探测 | 无 | 默认 0x2C |
| ControlRecv | `ControlStream.c:1853` | 控制流 | 无 | 默认 0x2C |
| LossStats | `ControlStream.c:1937` | 丢包统计 | 无 | 默认 0x2C |
| ReqIdrFrame | `ControlStream.c:1964` | 请求关键帧 | 无 | 默认 0x2C |
| CtrlAsyncCb | `ControlStream.c:1995` | 异步回调派发 | 无 | 默认 0x2C |
| InvRefFrames | `ControlStream.c:2032` | 参考帧失效 | 无 | 默认 0x2C |
| InputSend | `InputStream.c:665` | 输入包发送 | 无 | 默认 0x2C |
| AsyncTerm | `Connection.c:171` | 异步终止 | 无 | 默认 0x2C |
| 主线程 | borealis loop | UI + 视频 deko3d 提交 + present | **core 0** | **0x20** |
| FileLogger | `FileLogger.cpp:270` | 日志写盘 | 无 | 默认 0x2C |

主线程的两处特权来自 `app/src/main.cpp`：

```cpp
preferSwitchCore(0);                        // :102 绑到第一个允许核
svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);  // :106 高于所有 worker(0x2C)
```

**问题**：13 个线程 + 主线程在 3 个可用核（applet 模式 core 0/1/2）上自由迁移。libnx 的 pthread 默认不设亲和性，
所以 VideoDec / AudioDec 随时可能被调度到 core 0 与主线程抢核。视频解码和音频喂缓冲都是**延迟敏感、周期性唤醒**的负载，
一旦被主线程或彼此抢占，表现就是掉帧 / 爆音，而 CPU 占用率看起来并不高。

---

## 2. 主线程每帧到底在等什么

`streaming_view.cpp:332` → `MoonlightSession::draw`（`:429`）→ `DKVideoRenderer::draw`（`DKVideoRenderer.cpp:1111`），
一帧内的实际顺序：

```
queueWaitFence(&upscalingFence)   // :1120 等上一帧的后处理链
checkAndInitialize / updateRenderState / updateFrameMapping  // :1126-1138
queue.submitCommands(cmdlist)     // :1145 视频绘制
submitUpscalingPresentPass()      // :1153 RCAS / upscaling，主线程录制+提交
vctx->queueFlush()                // :1154
vctx->queueSignalFence(...)       // :1160
... 回到 borealis：beginFrame → acquireImage → submit UI → endFrame → presentImage
```

几个已经做过的优化（说明作者已扫过一轮，别再重复提）：

- `queueWaitFence` 被刻意挪到**下一次 draw 的顶部**（`:1114-1123` 注释写得很清楚），让上一帧的 GPU 后处理与这一帧的 CPU 工作重叠。
- 静态绘制命令只录一次（`recordStaticCommands`），仅在尺寸/色彩/开关变化时重录（`updateRenderState:977-994`）。
- 视频纹理是**零拷贝**：直接 `dk::MemBlockMaker{...}.setStorage(cpuAddr)` 映射 NVTEGRA 内存（`updateFrameMapping:1035-1040`），没有 CPU 拷帧。
- 帧队列带自适应深度与节奏控制（`AVFrameHolder.cpp:122-183`）。

所以主线程的时间主要花在：**presentImage 的 vblank 节流**、**一次 RCAS/upscaling 的 GPU 链**、以及少量的 NVG UI 遍历。
这些都是串行不可避免的部分，把它们拆到别的线程，主线程并不会变快——它本来就在等 GPU。

---

## 3. 候选优化点（按 ROI 排序）

### P0 — 绑核 + 优先级分级（建议先做，零风险）

**做法**：给 `PltCreateThread` 加一个 `__SWITCH__` 专用分支（或在 app 侧提供 `PltSetThreadAttributes` 钩子），按线程名绑定：

| 线程 | 核 | 优先级 | 理由 |
|---|---|---|---|
| AudioDec / AudioRecv | 1 | 0x1E | 欠载即爆音，最紧 |
| VideoDec | 2 | 0x20 | 与主线程同级，避免被 UI 饿死 |
| VideoRecv | 1 | 0x22 | 收包延迟直接进延迟基线 |
| InputSend | 1 | 0x24 | 输入上行要早发 |
| Ping / LossStats / CtrlAsyncCb 等 | 2 | 0x2C | 可容忍抖动 |
| 主线程 | 0 | 0x20 | 保持现状 |

**收益**：中等且**确定的**。消除 3 核上的跨核迁移与 L2 争用；对 stutter 类问题往往比任何代码重构都有效。
**风险**：低。需处理 `svcSetThreadCoreMask` 返回 `EINVAL`（目标核不在允许掩码内）时回退到不绑。
**注意**：`extern/moonlight-common-c` 是 vendored 源码，改动要加 `#ifdef __SWITCH__` 隔离，避免影响其它平台构建。

### P1 — 视频后处理与 UI 分离到独立 `dk::Queue`（收益中，风险中高）

**现状**：`DKVideoRenderer` 直接复用 borealis 的全局队列（`:372 queue = vctx->getQueue()`，底层是 `switch_video.hpp:87`
唯一的 `dk::UniqueQueue`）。UI 命令、视频映射、RCAS/upscaling、present 全在一条提交通道上。
**做法**：给 post-process 建第二个 queue + 独立 fence；主线程在 present 前只等一个 fence，UI 提交可与 GPU 后处理并行。
**收益**：只在开启 RCAS / upscaling 时有意义（默认路径没有后处理，直接走 `queue.submitCommands(cmdlist)`）。
**风险**：高。swapchain 的 `presentImage` 必须留在主 queue；跨 queue 的 image 所有权要显式 barrier；
且 NVDEC 写 surface 与 GPU 读 surface 之间没有 fence 可用（外部引擎），surface 生命周期只能靠队列深度兜（见 §4）。
**建议**：先加埋点量出 `queueWaitFence` 的实际等待时长，>2ms 再动这个。

### P2 — telemetry 格式化移出主线程（收益低，但几乎零成本）

**现状**：`MoonlightSession::draw` 每秒调一次 `log_telemetry()`（`:447-450`），快照 + 大量字符串格式化在主线程完成；
写盘已经是 `FileLogger` 的独立线程（`FileLogger.cpp:270`）。
**做法**：把统计结构体整体丢给 FileLogger 线程，格式化在那边做。
**收益**：每秒一次、微秒级，除了 `write_log` 打开 + LOG_DEBUG 的场景，基本无感。**可做可不做。**

### P3 — 帧队列无锁化（不建议）

`AVFrameQueue` 是标准 SPSC（VideoDec 生产、主线程消费），理论上可以换 atomic ring buffer。
但实测口径下每帧只有 2 次短临界区（120 次/秒），`std::mutex` 无竞争时约 20ns 级，**收益远小于重写风险**。
`pop()` 内还有自适应节奏计算，挪出锁外反而要额外同步。**明确不建议。**

### P4 — 输入采样独立线程（不建议）

每帧 2 次 `handleInput()`（`streaming_view.cpp:322` 与 draw 末尾）刻意拆成前后两半，目的是缩短按键到发包的间隔。
加线程只会引入跨线程传递延迟，与现有设计意图相反。

### P5 — 多帧并发解码（不可行）

单个 `AVCodecContext` 的 `send_packet` / `receive_frame` 不能并发调用；同一路流内部有 GOP 依赖；
Switch 只有 1 个 NVDEC 引擎。**线程数增加了，吞吐不会变。**

---

## 4. 一条容易忽略的硬约束：surface 池深度

`extra_hw_frames = AVFrameQueue::capacityFor(frames_queue_size)`
（`FFmpegVideoDecoder.cpp:133-134`），而 `capacityFor(n) = max(n,1) + 5`（`AVFrameHolder.cpp:14,66-68`）。
默认 `frames_queue_size = 2`（`Settings.hpp:363`）→ **NVTEGRA surface 池 = 7 帧**（1080p NV12 约 22MB）。

任何"加深流水线"的想法都要连带抬高这个值，否则就是源码注释里警告的那句：
池子比队列浅时，突发会让解码器拿不到输出 surface，**FFmpeg 静默丢帧且不报错**。
换句话说，多线程 pipeline 的深度上限由显存和延迟共同决定，不是随便加线程就能变深。

---

## 5. 明确不要做的三件事

1. **`thread_count > 1`**：`m_use_decoder_threads` 带 `!m_hw_decode_active` 前置条件（`FFmpegVideoDecoder.cpp:615-616`），
   而 Switch 上硬解是强制的（`Settings.hpp:286-291` + `FFmpegVideoDecoderDekoHelpers.cpp:13-16`）。
   强行打开只能退回软解，1080p60 HEVC 在 4×A57 上跑不动。
2. **把 present 搬到渲染线程**：swapchain 与 `dk::Queue` 绑定在主线程（`switch_video.cpp:221-233`），跨线程 present 会直接破坏图像所有权。
3. **多线程解码同一路流**：见 P5。

---

## 6. 怎么验证（而不是凭感觉）

- `Settings` 的 `write_log` 打开后，`log_telemetry()` 已经把 decode / render / queue / pacing 分段打出来了，
  先看 `decoding_time` 与 `rendering_time` 谁占大头，再决定投入方向。
- 建议补两个埋点：① `queueWaitFence` 返回前的实际阻塞时长；② 每帧 UI 提交（borealis `renderCmdlist`）到 present 的间隔。
  有了这两个数字，P1 才值得动手。
- 绑核效果用 `svcGetThreadCoreMask` 在关键线程启动后打印一次，确认生效；再用掉帧计数（`getFrameDropStat` 系列）做前后对比。
