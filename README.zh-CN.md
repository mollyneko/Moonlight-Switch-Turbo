# Moonlight-Switch-Turbo

[English](README.md) | **简体中文**

面向 Nintendo Switch 的 [Moonlight](https://github.com/moonlight-stream) 串流客户端，fork 自
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch)。只保留 Switch 一个目标平台。
1.5.x 整条版本线都在削客户端这一侧的延迟，1.5.14 收尾把解码线程从渲染核心上挪开。

---

## 延迟：1.5.0 → 1.5.15

客户端额外增加的延迟，按 60 fps 计算，从代码参数推导，不含网络与主机编码。1.5.14 和 1.5.15 没有改动这条路径。

| | 1.5.0 | 1.5.15 | 变化 |
|:---|---:|---:|---:|
| **视频** —— 画面年龄 | 1 帧 · 16.7 ms | **0 帧 · 0 ms** | −16.7 ms |
| **输入** —— 按键年龄 | 8.3 ms | **4.2 ms** | −4.2 ms |
| **合计** | **25.0 ms** | **4.2 ms** | **−20.8 ms（−83%）** |
| **音频** —— 声画偏移 | 62.5 ms | **25.0 ms** | −37.5 ms |

逐项推导见 [docs/latency.zh-CN.md](docs/latency.zh-CN.md)。

## 改了什么

**视频** —— 帧队列先按 1.5.0 的深度启动，连续流畅 5 秒后把预留还给 0 帧，一旦欠载立刻还回去（1.5.0 是每次
丢失都重建整个预留，形成可见的冻结—追赶循环）。节奏收敛削掉积压要 0.6 秒而不是 3.8 秒。GPU fence 改成下一帧
开头才等待，后处理跑的时候 CPU 去采样输入、画 UI。关闭抖动时 RCAS 直接写 framebuffer，每帧省掉一次全分辨率
拷贝。解码配置没动。

**音频** —— 5 块 wave buffer 里只允许 2 块入队，阻塞写换成超过 30 ms 待播就整帧丢弃，上限才从名义值变成实际
值。那个每 400 次调用丢一块的漂移修正已删除，因为已经没有积压需要修正。

**输入** —— 手柄每帧采样两次（间隔 8.3 ms），并且第一次采样从渲染 pass **之后**挪到**之前**，包不用再等一整个
渲染 pass。触摸和鼠标仍是单次采样。

## 1.5.14 —— 线程放置

moonlight-common-c 创建的 13 个工作线程没有核掩码、也没有优先级，VideoDec 和 AudioDec 一直在跟 `main.cpp`
绑在 core 0 的渲染主循环抢时间片。现在显式放置：

| 线程 | 核心 | 优先级 |
|:---|:---|:---|
| AudioDec | 1 | `0x1E` |
| VideoDec | 2 | `0x20` |
| 渲染主循环（未动） | 0 | `0x20` |

绑核在各自的解码回调内部完成，所以 `moonlight-common-c` 保持未修改的 submodule 状态。放置是幂等的，从 applet
实际授权的掩码里取核，够不到就静默回退。解码线程数设置在 Switch 上被隐藏 —— 硬件解码把它固定为单线程。

这一版修的是帧节奏，不是延迟，上面的数字不会变。

## 1.5.15 —— 输入路径清理

每帧的第二次手柄采样此前走的是完整 `handleInput()`：鼠标轮询、触摸轮询、统一手柄映射、滚动累积、键盘差分
全部重做一遍。这些在第一次采样之后几微秒内不可能发生变化，而那处调用的注释一直写着「只采样手柄」。现在
改走 `handleGamepadOnly()`，只重读手柄并复用第一次算好的 specialKey —— 从通往 present 的路径上拿掉两次
HID 往返和一整轮输入推导。

一并移除的还有：每帧创建一个 3 秒定时器去调用 `setBottomBarStatus()`（该函数在 SDL 以外的平台是空的），
以及 `mapController()` 里一次多余的 `std::fill`（那行还把 `sizeof` 当元素个数用）。

量级在每帧 20–40 µs —— 在循环里能量出来，在游戏里感觉不到。

---

## 版本历史

| 版本 | 改动 |
|:---|:---|
| 1.5.0 | 分支基线 |
| 1.5.1 | 帧队列深度可配置（默认 2） |
| 1.5.2 | 输入在渲染之前发出 |
| 1.5.3 | 手柄每帧采样两次 |
| 1.5.4 | 自适应队列深度、真实客户端刷新率 |
| 1.5.5 | 退出时清理会话，挂起后重建 |
| 1.5.6 | SD 卡日志不再阻塞串流 |
| 1.5.7 | 去掉逐帧分配；GPU 同步移出关键路径 |
| 1.5.8 | RCAS 为最后一级时跳过冗余拷贝 |
| 1.5.9 | 音频队列变浅；解码错误不再被吞掉 |
| 1.5.10 | 移除无效设置 |
| 1.5.11 | 码率从滑杆改为 5 Mbps 档位 |
| 1.5.12 | 主菜单标题显示 Moonlight-Switch-Turbo |
| 1.5.13 | 标题缩短为 Moonlight-Turbo |
| 1.5.14 | 解码线程移出渲染核；隐藏无效的解码线程数设置 |
| 1.5.15 | 第二次手柄采样改为只采样手柄；移除空转的底栏定时器（当前） |

## 安装

1. 从 [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases) 下载最新的
   `Moonlight-Switch-Turbo-*.zip`
2. 把 `Moonlight-Switch.nro` 解压到 `sdcard:/switch/Moonlight-Switch/`
3. 通过 **Title Redirection** 启动 hbmenu（需要完整内存权限）

> [!IMPORTANT]
> 主菜单标题是 `Moonlight-Turbo`，但数据目录仍然是 `switch/Moonlight-Switch/`，设置和收藏不受改名影响。

> [!TIP]
> 高码率 1080p 需要 CPU/GPU 超频 —— [sys-clk](https://github.com/retronx-team/sys-clk) 或
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md)。超频有风险，后果自负。

## 操作

**鼠标** —— 触摸屏移动光标，轻点左键，双指滚动。触摸期间 ZL/ZR 充当左右键、左右摇杆充当滚轮。支持 USB 鼠标。

**键盘** —— 三指轻点呼出软键盘；USB 键盘直接可用。

**手柄** —— 默认 X360 映射（A/B、X/Y 互换），可重映射。最多 5 个控制器，支持单只 Joy-Con。想要陀螺仪就在
Sunshine 里识别为 DS4（仅玩家 1）。

**覆盖层** —— 同时按 `-` 和 `+`，或按住 `ESC`。组合键与按住时长都可配置。

**NSP 前向器** —— 先把目标加入收藏，再用
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight) 生成。手动参数：`--ip` / `--appid` /
`--appname`。

## 构建

需要 devkitPro 的 devkitA64、libnx 与 Switch portlibs，以及 CMake 3.13+ 和 Ninja。

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

产物在 `build/switch/Moonlight.nro`。`-DUSE_DEKO3D=ON` 选择 deko3d 后端，这是低延迟路径的前提。如果 `uam`
不可用，构建会从 `resources/shaders/` 复制预编译好的 `.dksh`，而不是在配置阶段报错。

## 已知限制

- 只构建 Switch；Android、iOS、PSV 和桌面端已移除
- 链路持续抖动时，自适应队列退回 1 帧预留 —— 延迟回到 1.5.0 的水平换取不卡顿；配置的队列越深，这个下界
  越高（设为 3 时稳态是 1 帧）
- 触摸和鼠标没有双采样，平均等待仍是 8.3 ms
- applet 没授权到目标核时，解码线程保持不绑 —— 串流照常工作，只是没有抖动收益
- 六轴需要主机把手柄识别为 DS4；Pro 手柄的陀螺仪轴不会原生透传

## 致谢

上游 [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) 与
[moonlight-stream](https://github.com/moonlight-stream)。UI 基于
[borealis](https://github.com/XITRIX/borealis)，串流协议基于
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)。

## 许可证

GPLv3 —— 见 [LICENSE](LICENSE)。
