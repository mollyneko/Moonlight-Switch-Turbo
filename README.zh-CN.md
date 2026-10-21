# Moonlight-Switch-Turbo

[English](README.md) | **简体中文** | [日本語](README.ja.md) | [한국어](README.ko.md)

面向 Nintendo Switch 的 [Moonlight](https://github.com/moonlight-stream) 串流客户端，从
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) 分支而来，只保留 Switch 一个平台。
1.5.x 系列始终围绕同一件事：把客户端自身引入的延迟降到最低。

---

## 延迟：1.5.0 → 1.5.19

下表只统计客户端自身引入的延迟，按 60 fps 计算，数值由代码参数推导得出，不含网络往返与主机编码。
1.5.14 之后的几个版本没有改动视频与输入这条路径。1.5.18 给音频池多加了一条缓冲：声画偏移多 12.5 ms，
换来整整一帧的余量来压住爆音。

| | 1.5.0 | 1.5.19 | 变化 |
|:---|---:|---:|---:|
| **视频** —— 屏幕画面滞后 | 1 帧 · 16.7 ms | **0 帧 · 0 ms** | −16.7 ms |
| **输入** —— 按键响应滞后 | 8.3 ms | **4.2 ms** | −4.2 ms |
| **合计** | **25.0 ms** | **4.2 ms** | **−20.8 ms（−83%）** |
| **音频** —— 声音相对画面滞后 | 62.5 ms | **37.5 ms** | −25.0 ms |

逐项推导见 [docs/latency.zh-CN.md](docs/latency.zh-CN.md)。

---

## 版本历史

| 版本 | 改动 |
|:---|:---|
| 1.5.0 | 分支起点 |
| 1.5.1 | 帧队列深度可配置（默认 2 帧） |
| 1.5.2 | 输入先于画面渲染发出 |
| 1.5.3 | 手柄每帧采样两次 |
| 1.5.4 | 帧队列改为自适应深度；按客户端真实刷新率上报 |
| 1.5.5 | 退出时清理会话；系统挂起后重建 |
| 1.5.6 | 写 SD 卡日志不再阻塞串流；追赶丢帧设有上限 |
| 1.5.7 | 移除逐帧内存分配；GPU 同步移出关键路径 |
| 1.5.8 | RCAS 作为最后一级时，跳过多余的一次拷贝 |
| 1.5.9 | 音频队列改浅；解码错误不再被静默丢弃 |
| 1.5.10 | 移除不起作用的设置项 |
| 1.5.11 | 码率从滑杆改为 5 Mbps 一档的阶梯 |
| 1.5.12 | 主菜单标题改为 Moonlight-Switch-Turbo |
| 1.5.13 | 标题缩短为 Moonlight-Turbo |
| 1.5.14 | 解码线程移出渲染核心（1.5.17 已撤回）；隐藏不起作用的解码线程数设置 |
| 1.5.15 | 第二次手柄采样只采手柄；移除空转的底栏定时器 |
| 1.5.16 | 新增手柄模式：PlayStation（默认）/ Xbox |
| 1.5.17 | 手柄包默认走非可靠传输，按键状态变化时才转可靠；撤回解码线程绑核 |
| 1.5.18 | 启用触摸面板、触摸与鼠标事件重新发往主机；音频池多一条缓冲并统计丢帧 |
| 1.5.19 | 休眠不再冻死整机：唤醒时直接结束串流，不在原进程里重建会话（当前） |

## 安装

1. 从 [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases) 下载最新的
   `Moonlight-Switch-Turbo-*.zip`
2. 将 `Moonlight-Switch.nro` 解压到 `sdcard:/switch/Moonlight-Switch/`
3. 通过 **Title Redirection** 启动 hbmenu（需要完整内存权限）

> [!IMPORTANT]
> 主菜单标题显示为 `Moonlight-Turbo`，但数据目录仍是 `switch/Moonlight-Switch/`，原有设置与收藏不受影响。

> [!TIP]
> 1080p 高码率需要 CPU/GPU 超频，可用 [sys-clk](https://github.com/retronx-team/sys-clk) 或
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md)。超频有风险，请自行承担。

## 操作

**鼠标** —— 触摸屏移动光标，轻点即左键点击，双指滑动滚动。触摸期间 ZL/ZR 相当于左右键，左右摇杆相当于
滚轮。也支持 USB 鼠标。

**键盘** —— 三指轻点屏幕呼出软键盘；USB 键盘可直接使用。

**手柄** —— 默认按 X360 布局映射（A/B 与 X/Y 互换），可自行重映射。最多支持 5 个控制器，含单只 Joy-Con。
设置里的**手柄模式**决定主机模拟哪种手柄：*PlayStation*（默认，保留陀螺仪与加速度计）或 *Xbox*（没有
六轴，X360 手柄本就没有）。切换在串流过程中即刻生效。

**覆盖层** —— 默认同时按 `-` 和 `+`，或按住 `ESC`；组合键与按住时长均可配置。

**NSP 前向器** —— 先将目标应用加入收藏，再用
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight) 生成。手动传参：`--ip` / `--appid` /
`--appname`。

## 构建

需要 devkitPro 的 devkitA64、libnx 与 Switch portlibs，以及 CMake 3.13+ 与 Ninja。

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

产物位于 `build/switch/Moonlight.nro`。`-DUSE_DEKO3D=ON` 指定 deko3d 后端，是低延迟路径的前提。若 `uam`
不可用，构建会改用 `resources/shaders/` 里预编译好的 `.dksh`，而不会在配置阶段直接报错。

## 已知限制

- 仅构建 Switch 版本，Android、iOS、PSV 与桌面端均已移除
- 链路持续抖动时，自适应队列会退回 1 帧预留：延迟回到 1.5.0 的水平，换来不卡顿。配置的队列越深，
  这个下界越高（设为 3 时稳态为 1 帧）
- 触摸与鼠标没有双采样，平均等待仍是 8.3 ms
- 本构建不绑核：解码线程绑核会让串流在启动约 0.4 秒后直接断开，因此在真机上定位到具体失败的 syscall
  之前，绑核保持关闭
- 六轴仅在 PlayStation 模式下可用，且需要主机把手柄识别为 DS4；Pro 手柄的陀螺仪轴不会原生透传
- 串流中休眠或按 HOME 会直接结束串流，唤醒后不会接着放，需要从主机列表重新连接。带着解码器与 GPU
  状态跨过一次休眠，正是冻死整机的原因

## 致谢

上游 [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) 与
[moonlight-stream](https://github.com/moonlight-stream)。UI 基于
[borealis](https://github.com/XITRIX/borealis)，串流协议基于
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)。

## 许可证

GPLv3 —— 见 [LICENSE](LICENSE)。
