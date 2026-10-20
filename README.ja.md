# Moonlight-Switch-Turbo

[English](README.md) | [简体中文](README.zh-CN.md) | **日本語** | [한국어](README.ko.md)

Nintendo Switch 向けの [Moonlight](https://github.com/moonlight-stream) ゲームストリーミングクライアントです。
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) から派生し、Switch のみを対象としています。
1.5.x 系は一貫して「クライアント側で積み上がる遅延を削る」ことに取り組んできました。1.5.14 からは、デコード
スレッドもレンダリングスレッドと同じコアを取り合わなくなりました。

---

## 遅延：1.5.0 → 1.5.18

下表はクライアント自身が追加する遅延のみを、60 fps を前提にコード上のパラメータから算出したものです。
ネットワークの往復時間とホスト側のエンコードは含みません。1.5.14 以降のバージョンでは、この経路に変更は
ありません。

| | 1.5.0 | 1.5.18 | 変化 |
|:---|---:|---:|---:|
| **映像** —— 画面に映るまでの遅れ | 1 フレーム · 16.7 ms | **0 フレーム · 0 ms** | −16.7 ms |
| **入力** —— ボタンを押してから反映されるまで | 8.3 ms | **4.2 ms** | −4.2 ms |
| **合計** | **25.0 ms** | **4.2 ms** | **−20.8 ms（−83%）** |
| **音声** —— 映像に対する音の遅れ | 62.5 ms | **25.0 ms** | −37.5 ms |

項目ごとの算出根拠は [docs/latency.zh-CN.md](docs/latency.zh-CN.md)（中国語）にまとめています。

---

## バージョン履歴

| バージョン | 変更内容 |
|:---|:---|
| 1.5.0 | ブランチの起点 |
| 1.5.1 | フレームキューの深さを設定可能に（既定値 2） |
| 1.5.2 | 描画前に先行して入力を送信 |
| 1.5.3 | コントローラーを 1 フレームに 2 回サンプリング |
| 1.5.4 | フレームキューを可変深度に。クライアントの実際のリフレッシュレートを通知 |
| 1.5.5 | 終了時にセッションを破棄し、スリープ復帰後に再構築 |
| 1.5.6 | SD カードへのログ出力がストリームを止めないように。追い付き時の破棄に上限 |
| 1.5.7 | フレームごとのメモリ確保を廃止。GPU 同期をクリティカルパスから除外 |
| 1.5.8 | RCAS が最終段のとき、不要なコピー処理を省略 |
| 1.5.9 | 音声キューを浅く。デコードエラーを握り潰さないように |
| 1.5.10 | 効果のない設定項目を削除 |
| 1.5.11 | ビットレートをスライダーから 5 Mbps 刻みの選択式に |
| 1.5.12 | ホームメニューのタイトルを Moonlight-Switch-Turbo に |
| 1.5.13 | タイトルを Moonlight-Turbo に短縮 |
| 1.5.14 | デコードスレッドをレンダリングコアから移動。無効なデコードスレッド数設定を非表示に |
| 1.5.15 | 2 回目のサンプリングをコントローラー限定に。空回りしていた下部バーのタイマーを削除 |
| 1.5.16 | コントローラーモードを追加：PlayStation（既定）/ Xbox |
| 1.5.17 | コントローラーのパケットを既定で非リライアブル送信に。ボタン状態が変化したときのみリライアブル |
| 1.5.18 | スリープ復帰時、フレームが止まったときだけストリームを再構築（現在） |

## インストール

1. [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases) から最新の
   `Moonlight-Switch-Turbo-*.zip` をダウンロードします
2. `Moonlight-Switch.nro` を `sdcard:/switch/Moonlight-Switch/` に展開します
3. **Title Redirection** 経由で hbmenu を起動します（フルメモリ権限が必要です）

> [!IMPORTANT]
> ホームメニューのタイトルは `Moonlight-Turbo` ですが、データフォルダーは `switch/Moonlight-Switch/` の
> ままです。既存の設定とお気に入りはそのまま引き継がれます。

> [!TIP]
> 1080p の高ビットレートには CPU / GPU のオーバークロックが必要です。
> [sys-clk](https://github.com/retronx-team/sys-clk) や
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md) が使えます。オーバークロックは
> 自己責任でお願いします。

## 操作

**マウス** —— タッチスクリーンでカーソルを動かし、タップで左クリック、2 本指でスクロールします。タッチ中は
ZL / ZR が左右ボタン、左右スティックがスクロールホイールとして働きます。USB マウスにも対応しています。

**キーボード** —— 画面を 3 本指でタップするとソフトキーボードが開きます。USB キーボードはそのまま使えます。

**コントローラー** —— 既定では X360 配列（A/B と X/Y を入れ替え）でマッピングされ、再割り当てできます。最大
5 台まで、片方だけの Joy-Con にも対応します。設定の**コントローラーモード**で、ホスト側がエミュレートする
パッドを選べます。*PlayStation*（既定、ジャイロと加速度センサーを維持）または *Xbox*（モーションなし。
X360 パッドには元からありません）。切り替えはストリーミング中でも即座に反映されます。

**オーバーレイ** —— 既定では `-` と `+` の同時押し、または `ESC` 長押しで開きます。組み合わせと長押し時間は
どちらも設定できます。

**NSP フォワーダー** —— 先に対象のアプリをお気に入りに追加し、
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight) で生成します。手動で指定する場合は
`--ip` / `--appid` / `--appname` を渡します。

## ビルド

devkitPro の devkitA64、libnx、Switch 向け portlibs、および CMake 3.13+ と Ninja が必要です。

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

生成物は `build/switch/Moonlight.nro` です。`-DUSE_DEKO3D=ON` で deko3d バックエンドを選択します。これは
低遅延経路の前提条件です。`uam` が使えない場合、設定段階で失敗するのではなく、`resources/shaders/` にある
ビルド済みの `.dksh` をコピーして続行します。

## 既知の制限

- Switch のみをビルド対象としています。Android、iOS、PSV、デスクトップ版は削除済みです
- 回線の揺らぎが続くと、可変キューは 1 フレームの予備に戻ります。遅延は 1.5.0 相当に戻りますが、その代わり
  にコマ落ちを防ぎます。キューを深く設定するほどこの下限は上がります（3 に設定すると定常状態は 1 フレーム）
- タッチとマウスは 2 回サンプリングの対象外で、平均待ち時間は 8.3 ms のままです
- applet がコアを許可していない場合、デコードスレッドはコア固定されません。ストリーミング自体は問題なく
  動作しますが、揺らぎの改善は得られません
- ジャイロは PlayStation モードでのみ使えます。ホスト側でコントローラーを DS4 として認識させる必要があり、
  Pro コントローラーのジャイロ軸はそのままでは転送されません

## クレジット

上流の [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch) と
[moonlight-stream](https://github.com/moonlight-stream)。UI は
[borealis](https://github.com/XITRIX/borealis)、ストリーミングプロトコルは
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) を使用しています。

## ライセンス

GPLv3 —— [LICENSE](LICENSE) を参照してください。
