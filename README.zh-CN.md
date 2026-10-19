# Moonlight-Switch-Turbo

[English](README.md) | **简体中文**

Nintendo Switch 上的 [Moonlight](https://github.com/moonlight-stream) 游戏串流客户端，基于
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) 的 Turbo 分支。

这个分支只保留 Switch 一个目标平台，1.5.x 整条版本线都在做同一件事：把客户端链路里的延迟一帧一帧地拿掉 ——
输入在渲染之前发出、手柄每帧采样两次、视频队列自适应降到 0 帧驻留、音频缓冲压到 25 ms。

---

## 延迟：1.5.0 → 1.5.11

1.5.11 是这轮延迟优化收尾的版本。下面所有数字都从代码参数推导，按 **60 fps（1 帧 = 16.67 ms）** 计算，
只统计客户端在主机之外额外增加的那部分延迟。

### 客户端侧延迟构成

| 环节 | 1.5.0 | 1.5.11 | 变化 |
|:---|---:|---:|---:|
| 输入采样等待（均值） | 0.50 帧 · 8.3 ms | 0.25 帧 · **4.2 ms** | −4.2 ms |
| 输入采样等待（最差值） | 1.00 帧 · 16.7 ms | 0.50 帧 · **8.3 ms** | −8.3 ms |
| 输入发送位置 | 渲染之后 | 渲染之前 | 少等一个绘制回调 |
| 视频帧队列驻留（稳态） | 1.00 帧 · 16.7 ms | 0.00 帧 · **0 ms** | −16.7 ms |
| 视频帧队列驻留（劣化态） | 1.00 帧 · 16.7 ms | 1.00 帧 · 16.7 ms | 0（设计如此） |
| **客户端侧新增延迟** | **1.50 帧 · 25.0 ms** | **0.25 帧 · 4.2 ms** | **−1.25 帧 · −20.8 ms（−83%）** |
| 音频缓冲深度（最差） | 5 × 12.5 ms · 62.5 ms | 2 × 12.5 ms · **25.0 ms** | −37.5 ms |

最后一行是另一条路径 —— 它约束的是唇音同步，不参与画面链路，所以不计入 25.0 → 4.2 ms 的合计。

### 一帧里发生了什么

```
1.5.0    一帧（16.7 ms）
         +---------------------------+---------------------------+
         |    渲染并提交这一帧        |     采样输入 -> 发送       |
         +---------------------------+---------------------------+
                                     ^ 按在这里要等满一帧

1.5.11   一帧（16.7 ms）
         +-------------+-----------------------------------+-------------+
         | 采样+发送    |           渲染并提交这一帧          | 采样+发送    |
         +-------------+-----------------------------------+-------------+
         ^                                                 ^
         两个采样点把一帧切成两半，间隔从一整帧变成半帧
```

### 队列深度敏感性

1.5.0 的 `D_target = min(配置深度 − 1, 2)`；1.5.11 从这里起步，链路证明吃得下之后再往下走一帧。

| 帧队列设置 | 1.5.0 目标 | 1.5.11 目标（稳态） | 1.5.0 合计 | 1.5.11 合计 |
|:---|---:|---:|---:|---:|
| 1 | 0 | 0 | 8.3 ms | **4.2 ms** |
| 2（默认） | 1 | 0 | 25.0 ms | **4.2 ms** |
| 3 | 2 | 1 | 41.7 ms | **20.9 ms** |

设置越深差距越大 —— 固定驻留在 1.5.0 里是实打实的成本，在 1.5.11 里是可回收的。

### 帧率缩放

两项都随帧周期线性变化，30 fps 时收益翻倍，120 fps 时减半。

| 刷新率 | 帧周期 | 1.5.0 | 1.5.11 | 变化 |
|:---|---:|---:|---:|---:|
| 30 fps | 33.3 ms | 50.0 ms | **8.3 ms** | −41.7 ms |
| 60 fps | 16.7 ms | 25.0 ms | **4.2 ms** | −20.8 ms |

Switch 在掌机和底座模式下都是 60 Hz 输出，客户端也是这么告诉主机的（`clientRefreshRateX100 = 6000`）。

### 音频路径

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| 单个 wave buffer | 12.5 ms（48 kHz 下 600 采样） | 12.5 ms |
| 池中最多 buffer 数 | 5 | 2 |
| 最差缓冲深度 | 62.5 ms | **25.0 ms** |
| 背压处理 | `write_audio()` 阻塞，池子整场都是满的 | 待播音频超过 30 ms 就整帧丢弃 |
| 周期性纠偏 | 每 400 次采样丢一个 buffer | 已移除 —— 有了上限就不需要 |

### 节奏控制

队列不只是"配置成目标深度"，还得真的**收得住**。

| | 1.5.0 | 1.5.11 |
|:---|:---|:---|
| 占用增益 | 每帧 0.01 | **每帧 0.06** |
| 最大修正 | 0.08 帧（两个方向相同） | **0.40** 排空、0.08 蓄水 |
| 60 fps 下最大排空速率 | 0.08 帧/绘制 ≈ 4.8 帧/秒 | **0.40 帧/绘制 ≈ 24 帧/秒** |
| 追赶上限 | `min(credit, 队列上限)` —— 节奏估计抖一下就可能一次丢 5–13 帧 | `min(credit, backlog + 1)` —— 处在目标深度时没有积压可追，就不会丢 |
| 返还预留 | 从不 | 连续 5 秒不欠帧才还一帧；一旦欠帧立刻补回并把下次窗口翻倍 |

上限不对称是故意的：排太多会丢掉本来只是稍晚的帧，而为了攒预留去扣帧会把同一张画面显示两次 ——
那种卡顿什么也换不来。

### 数字怎么算的

```
T_frame  = 1000 / 刷新率                -> 60 Hz 下 16.67 ms
T_sample = T_frame / 每帧采样次数 / 2    -> 按键平均等待时间
T_queue  = 目标缓冲帧数 × T_frame        -> 送显前压着的帧
L_client = T_sample + T_queue
```

```
1.5.0    16.67 / 1 / 2 + 1 × 16.67 =  8.33 + 16.67 = 25.0 ms
1.5.11   16.67 / 2 / 2 + 0 × 16.67 =  4.17 +  0.00 =  4.2 ms
```

1.5.11 的第二次采样放在 `draw()` 回调的最后一步，也就是帧送显之前；键盘 diff 和底栏计时仍然只走帧首那次。

**未计入**：网络 RTT、主机采集与编码、Switch 端 FFmpeg 解码、显示扫描输出、音频硬件延迟。
这些在两个版本之间没有变化，所以刻意排除 —— 这张表只衡量客户端改动的部分。

> [!NOTE]
> 以上是从代码参数推导的**理论值**，不是实验室实测。实际延迟还取决于链路抖动和主机端编码器。
>
> 作为参照，一次 4 ms RTT 的实测会话端到端约 68 ms，其中约 60 ms 是队列积压 —— 正是 1.5.11 要修的部分。
> 那次测量早于节奏修正，所以它更接近上界，而不是当前版本的真实表现。

---

## 版本变化

| 版本 | 改动 |
|:---|:---|
| 1.5.0 | 分支基线 |
| 1.5.1 | 视频帧队列深度可配置（默认 2） |
| 1.5.2 | 输入在渲染之前发送 |
| 1.5.3 | 手柄每帧采样两次 |
| 1.5.4 | 自适应帧队列深度 + 真实客户端刷新率 |
| 1.5.5 | 退出时清理串流会话，挂起后可重建 |
| 1.5.6 | SD 卡日志不再阻塞串流；追赶丢弃有界 |
| 1.5.7 | 去掉每帧内存分配；GPU 同步移出关键路径 |
| 1.5.8 | RCAS 作为末级后处理时跳过冗余 copy pass |
| 1.5.9 | 音频队列压浅；解码错误不再被静默 |
| 1.5.10 | 移除无法生效的设置项，收紧默认值 |
| 1.5.11 | 码率改为 5 Mbps 档位阶梯，替换滑块 |
| 1.5.12 | 主菜单标题显示 Moonlight-Switch-Turbo |
| 1.5.13 | 标题缩短为 Moonlight-Turbo（当前版本） |

---

## 安装

1. 到 [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases) 下载最新的 `Moonlight-Switch-Turbo-*.zip`
2. 解压出 `Moonlight-Switch.nro`，放到 `sdcard:/switch/Moonlight-Switch/`
3. 通过 **Title Redirection** 启动 hbmenu（需要完整内存权限）
4. 启动 Moonlight-Turbo

> [!IMPORTANT]
> 主菜单标题是 `Moonlight-Turbo`，但数据目录仍然是 `switch/Moonlight-Switch/`。配置和 Favorites 都在那里，
> 不受改名影响。

> [!TIP]
> 1080p + 高码率需要给 CPU/GPU 超频，可以用 [sys-clk](https://github.com/retronx-team/sys-clk) 或
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md)。超频有风险，自行判断。

---

## 操作

**鼠标** —— 触摸屏移动光标、点击左键、双指滚动。触摸时 ZL / ZR 当作左右键，L / R 摇杆当作滚轮。USB 鼠标同样可用。

**键盘** —— 三指点击屏幕呼出屏幕键盘，USB 键盘直接可用。

**手柄** —— 默认按 X360 手柄映射（A/B、X/Y 交换），可在设置里改键。支持最多 5 个手柄（含掌机模式）和单只 Joy-Con。

**体感** —— Sunshine 端需要把控制器识别为 DS4 才能用陀螺仪和加速度计，只对 1P 生效。

**游戏内浮层** —— 默认同时按 `-` 和 `+`，或按住键盘 `ESC`。组合键和长按时间都可配置。

**NSP Forwarder** —— 支持带参数直接串流。先把目标应用加进 Favorites，再用
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight) 生成；手工制作时传
`--ip` / `--appid` / `--appname`，例如 `--ip=192.168.1.101 --appid=1233211234 --appname=Steam`。

---

## 构建

需要 devkitPro 的 devkitA64、libnx 和 Switch portlibs，以及 CMake 3.13+ 与 Ninja。

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

产物在 `build/switch/Moonlight.nro`。`-DUSE_DEKO3D=ON` 打开 deko3d 后端，这是低延迟路径的前提。

如果 `uam`（deko3d 着色器编译器）不可用，构建会直接复制 `resources/shaders/` 下预编译好的 `.dksh`，
不会在配置阶段中断。

---

## 已知限制

- 只构建 Switch 目标，Android / iOS / PSV / 桌面端已移除
- 链路持续抖动时自适应队列会退回 1 帧驻留，延迟回到 1.5.0 的水平，换取不卡顿
- 体感需要主机端按 DS4 识别，Switch Pro 手柄的原生陀螺仪轴不直接透传

## 致谢

上游 [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) 与
[moonlight-stream](https://github.com/moonlight-stream)。UI 基于 [borealis](https://github.com/XITRIX/borealis)，
串流协议层是 [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)。

## License

GPLv3，见 [LICENSE](LICENSE)。
