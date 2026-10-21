# Moonlight-Switch-Turbo

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | **한국어**

Nintendo Switch용 [Moonlight](https://github.com/moonlight-stream) 게임 스트리밍 클라이언트입니다.
[XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch)에서 갈라져 나왔으며, Switch만 지원합니다.
1.5.x 계열은 처음부터 한 가지에 집중해 왔습니다. 클라이언트 쪽에서 더해지는 지연을 줄이는 일입니다.

---

## 지연: 1.5.0 → 1.5.19

아래 표는 클라이언트가 자체적으로 더하는 지연만 60 fps 기준으로 코드상의 파라미터에서 계산한 값입니다.
네트워크 왕복 시간과 호스트 측 인코딩은 포함하지 않습니다. 1.5.14 이후 영상과 입력 경로에는 변화가 없습니다.
1.5.18은 오디오 풀에 버퍼를 하나 추가해 소리의 지연이 12.5 ms 늘어 37.5 ms가 되었습니다.

| | 1.5.0 | 1.5.19 | 변화 |
|:---|---:|---:|---:|
| **영상** —— 화면에 나타나기까지의 지연 | 1프레임 · 16.7 ms | **0프레임 · 0 ms** | −16.7 ms |
| **입력** —— 버튼을 누른 뒤 반영되기까지 | 8.3 ms | **4.2 ms** | −4.2 ms |
| **합계** | **25.0 ms** | **4.2 ms** | **−20.8 ms(−83%)** |
| **오디오** —— 영상 대비 소리의 지연 | 62.5 ms | **37.5 ms** | −25.0 ms |

항목별 산출 근거는 [docs/latency.zh-CN.md](docs/latency.zh-CN.md)(중국어)에 정리되어 있습니다.

---

## 버전 기록

| 버전 | 변경 내용 |
|:---|:---|
| 1.5.0 | 브랜치 시작점 |
| 1.5.1 | 프레임 큐 깊이 설정 가능(기본값 2) |
| 1.5.2 | 화면 렌더링 전에 입력을 먼저 전송 |
| 1.5.3 | 컨트롤러를 프레임당 두 번 샘플링 |
| 1.5.4 | 프레임 큐를 가변 깊이로. 클라이언트의 실제 주사율을 보고 |
| 1.5.5 | 종료 시 세션 정리, 절전 복귀 후 재구성 |
| 1.5.6 | SD 카드 로그가 스트리밍을 막지 않도록. 따라잡기 폐기에 상한 설정 |
| 1.5.7 | 프레임마다 발생하던 메모리 할당 제거. GPU 동기화를 핵심 경로에서 제외 |
| 1.5.8 | RCAS가 마지막 단계일 때 불필요한 복사 과정 생략 |
| 1.5.9 | 오디오 큐를 얕게. 디코딩 오류를 더 이상 삼키지 않음 |
| 1.5.10 | 효과가 없던 설정 항목 제거 |
| 1.5.11 | 비트레이트를 슬라이더에서 5 Mbps 단위 선택식으로 변경 |
| 1.5.12 | 홈 메뉴 제목을 Moonlight-Switch-Turbo로 변경 |
| 1.5.13 | 제목을 Moonlight-Turbo로 단축 |
| 1.5.14 | 디코딩 스레드를 렌더링 코어 밖으로 이동(1.5.17에서 철회). 효과 없던 디코딩 스레드 수 설정 숨김 |
| 1.5.15 | 두 번째 샘플링을 컨트롤러 전용으로. 헛돌던 하단 바 타이머 제거 |
| 1.5.16 | 컨트롤러 모드 추가: PlayStation(기본) / Xbox |
| 1.5.17 | 컨트롤러 패킷을 기본적으로 비신뢰 전송으로. 버튼 상태가 바뀔 때만 신뢰 전송. 디코딩 스레드 코어 고정 철회 |
| 1.5.18 | 터치 패널 활성화, 터치·마우스 이벤트 재전송. 오디오 풀 버퍼 1개 추가 및 드롭 집계 |
| 1.5.19 | 절전해도 본체가 멈추지 않습니다. 복귀 시 스트림을 재구성하지 않고 종료합니다(현재) |

## 설치

1. [Releases](https://github.com/mollyneko/Moonlight-Switch-Turbo/releases)에서 최신
   `Moonlight-Switch-Turbo-*.zip`을 내려받습니다
2. `Moonlight-Switch.nro`를 `sdcard:/switch/Moonlight-Switch/`에 압축 해제합니다
3. **Title Redirection**을 통해 hbmenu를 실행합니다(전체 메모리 권한 필요)

> [!IMPORTANT]
> 홈 메뉴 제목은 `Moonlight-Turbo`이지만 데이터 폴더는 `switch/Moonlight-Switch/` 그대로입니다. 기존 설정과
> 즐겨찾기는 이름 변경에 영향을 받지 않습니다.

> [!TIP]
> 1080p 고비트레이트에는 CPU/GPU 오버클럭이 필요합니다.
> [sys-clk](https://github.com/retronx-team/sys-clk) 또는
> [4IFIR](https://github.com/rashevskyv/4IFIR/blob/main/README_ENG.md)를 사용할 수 있습니다. 오버클럭에는
> 위험이 따르며, 책임은 사용자에게 있습니다.

## 조작

**마우스** —— 터치스크린으로 커서를 움직이고, 탭하면 왼쪽 클릭, 두 손가락으로 스크롤합니다. 터치 중에는
ZL/ZR이 좌우 버튼, 좌우 스틱이 스크롤 휠 역할을 합니다. USB 마우스도 지원합니다.

**키보드** —— 화면을 세 손가락으로 탭하면 소프트 키보드가 열립니다. USB 키보드는 바로 사용할 수 있습니다.

**컨트롤러** —— 기본값은 X360 배열(A/B와 X/Y 교체)이며 다시 매핑할 수 있습니다. 최대 5대까지, 한쪽 Joy-Con
단독 사용도 지원합니다. 설정의 **컨트롤러 모드**에서 호스트가 에뮬레이트할 패드를 고릅니다. *PlayStation*
(기본, 자이로와 가속도 센서 유지) 또는 *Xbox*(모션 없음. X360 패드에는 원래 없습니다). 전환은 스트리밍
중에도 즉시 적용됩니다.

**오버레이** —— 기본값은 `-`와 `+` 동시 누르기 또는 `ESC` 길게 누르기입니다. 조합과 누르는 시간 모두 설정할
수 있습니다.

**NSP 포워더** —— 먼저 대상 앱을 즐겨찾기에 추가한 뒤
[NSP Forwarder Generator](https://nsp-forwarder.vercel.app/moonlight)로 생성합니다. 직접 지정할 때는
`--ip` / `--appid` / `--appname`을 넘깁니다.

## 빌드

devkitPro의 devkitA64, libnx, Switch용 portlibs, 그리고 CMake 3.13+와 Ninja가 필요합니다.

```bash
git clone --recursive https://github.com/mollyneko/Moonlight-Switch-Turbo
cd Moonlight-Switch-Turbo
cmake -B build/switch -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -G Ninja
ninja -C build/switch Moonlight.nro
```

결과물은 `build/switch/Moonlight.nro`입니다. `-DUSE_DEKO3D=ON`은 deko3d 백엔드를 선택하며, 저지연 경로의
전제 조건입니다. `uam`을 사용할 수 없으면 설정 단계에서 실패하는 대신 `resources/shaders/`에 있는 미리
컴파일된 `.dksh` 파일을 복사해 계속 진행합니다.

## 알려진 제한

- Switch만 빌드 대상입니다. Android, iOS, PSV, 데스크톱 버전은 제거되었습니다
- 회선이 계속 흔들리면 가변 큐가 1프레임 예비분으로 돌아갑니다. 지연은 1.5.0 수준으로 돌아가지만 그 대신
  끊김을 막습니다. 큐를 깊게 설정할수록 이 하한이 올라갑니다(3으로 설정하면 정상 상태가 1프레임)
- 터치와 마우스는 두 번 샘플링 대상이 아니어서 평균 대기 시간이 8.3 ms 그대로입니다
- 이 빌드는 디코딩 스레드를 코어에 고정하지 않습니다. 고정하면 스트림이 시작 약 0.4초 뒤에 끊기므로,
  실기에서 실패하는 syscall을 특정할 때까지 꺼 둡니다
- 자이로는 PlayStation 모드에서만 사용할 수 있습니다. 호스트에서 컨트롤러를 DS4로 인식시켜야 하며,
  Pro 컨트롤러의 자이로 축은 그대로 전달되지 않습니다
- 스트리밍 중 절전하거나 HOME을 누르면 복귀하지 않고 스트림이 종료됩니다. 호스트 목록에서 다시
  연결하세요. 디코더와 GPU 상태를 절전을 넘겨 유지하는 것이 본체가 멈추던 원인이었습니다

## 크레딧

업스트림 [XITRIX/Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch)와
[moonlight-stream](https://github.com/moonlight-stream). UI는
[borealis](https://github.com/XITRIX/borealis), 스트리밍 프로토콜은
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)를 사용합니다.

## 라이선스

GPLv3 —— [LICENSE](LICENSE)를 참조하세요.
