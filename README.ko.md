# Rubraview

**Rubraview v0.0.16**(최신 판) 내려받기 — [rubraview-v0.0.16.zip (Windows 11 x64)](https://github.com/rubidus-api/rubraview/releases/download/v0.0.16/rubraview-v0.0.16.zip) · [모든 판](https://github.com/rubidus-api/rubraview/releases)

[English](README.md) · **한국어**

순수 C23 과 WinAPI 로 지은, 가볍고 빠른 윈도우 멀티미디어 뷰어이자 동영상 재생기이며 일괄 이미지 처리기입니다.

압축을 풀고 `rubraview-v0.0.16.exe` 를 실행하면 됩니다. 설치 프로그램도, 옆에 둬야 하는 파일도 없습니다. 예외는 FFmpeg 의 DLL 하나뿐이고, 그것도 Media Foundation 이 못 여는 형식에만 필요합니다.

> 정본은 영문 README 이고, 이 문서는 그 번역입니다.

## 무엇인가

군더더기 없이 빠른 도구를 목표로 합니다. 반응이 빠른 보기, 원본을 건드리지 않는 이미지 보정, 많은 파일을 한 번에 처리하는 일괄 작업을 한 프로그램에 담았습니다.

- **GPU 로 그리는 화면**: Direct2D(D2D1)와 Direct3D 11 로 60~144 FPS 의 부드러운 이동과 소수점 단위 확대.
- **딸린 것 없는 이미지 해독**: JPEG, PNG, GIF, WebP, TIFF, BMP, ICO 를 윈도우의 WIC 로 그대로 엽니다.
- **동영상 재생**: 분리된 PAL 을 통한 FFmpeg(`libavcodec`/`libavformat`) 연결로 재생, 한 프레임씩 이동, 정지 화면 저장.
- **보정과 후처리**: 노출·대비·채도·감마·언샤프 마스크·가우시안 흐림을 실시간으로, 리샘플링은 Nearest·Bilinear·Bicubic·Lanczos-3.
- **일괄 처리**: 여러 스레드로 도는 변환·크기 조정기. 창에서도, 창 없이 명령줄에서도 씁니다.
- **순수 C23**: 벤더링한 `proven_c_lib` 의 아레나·문자열 조각·동적 배열 위에서, 힙 파편화 없이.

## 단축키

`F1` 을 누르면 같은 목록이 따로 창으로 뜨고, 보는 동안 열어 둘 수 있습니다.
여기 있는 키는 모두 설정 창(`F10`)의 Keys 쪽에서 바꿀 수 있습니다. 아래 표는
프로그램에 들어 있는 키 설정에서 그대로 만들어지므로 실제와 어긋나지 않습니다.

<!-- keys:begin — generated from src/core/default_keymap.c by scripts/check-actions.py --write; do not edit -->

**어디서나**

| 키 | 하는 일 |
|---|---|
| `F`, `F11`, `Alt+Enter` | 전체 화면 |
| `Tab` | 메뉴 상자 열기/닫기 |
| `T` | 도구 상자 열기/닫기 |
| `Shift+T` | 도구 상자 고정 |
| `Ctrl+T` | 도구 상자를 따로 창으로/되돌리기 |
| `Ctrl+Shift+T` | 항상 맨 위 |
| `O`, `Ctrl+O` | 파일 열기 |
| `Ctrl+Shift+O` | 폴더 열기 |
| `F4` | 필름스트립 |
| `I` | 정보 막대 |
| `E` | 사진 보정 |
| `Ctrl+E` | 내보내기 |
| `Ctrl+Shift+S` | 다른 이름으로 저장 |
| `Ctrl+B` | 여러 파일 변환 |
| `F10`, `Ctrl+,` | 설정 |
| `F1` | 이 도움말(별도 창) |
| `G` | 400% 넘으면 픽셀 격자 |
| `Esc` | 끝내기 |
| `Delete` | 휴지통으로 |
| `Shift+Delete` | 완전 삭제(확인함) |
| `Ctrl+Z` | 이동·복사·이름 바꾸기 되돌리기 |
| `F2` | 이름 바꾸기 |
| `Ctrl+Left` | 창 좁게 |
| `Ctrl+Right` | 창 넓게 |
| `Ctrl+Up` | 창 낮게 |
| `Ctrl+Down` | 창 높게 |
| `Alt+Left` | 창 왼쪽으로 |
| `Alt+Right` | 창 오른쪽으로 |
| `Alt+Up` | 창 위로 |
| `Alt+Down` | 창 아래로 |

**장 넘기기**

| 키 | 하는 일 |
|---|---|
| `PageDown`, `Space`, `Enter` | 다음 장 |
| `PageUp`, `Backspace`, `Shift+Space` | 이전 장 |
| `Home`, `Ctrl+Home` | 첫 장 |
| `End`, `Ctrl+End` | 마지막 장 |
| `Shift+Right`, `Ctrl+PageDown` | 열 장 뒤로 |
| `Shift+Left`, `Ctrl+PageUp` | 열 장 앞으로 |
| `Ctrl+Backspace` | 상위 폴더로 |
| `B` | 한 장/두 장/책 |
| `M` | 왼→오 / 오→왼(만화) |
| `Shift+B` | 펼침면 자동 인식 |
| `Ctrl+]` | 폴더의 다음 압축 |
| `Ctrl+[` | 폴더의 이전 압축 |

**보기**

| 키 | 하는 일 |
|---|---|
| `1` | 창에 맞춤 |
| `2` | 너비에 맞춤 |
| `3` | 높이에 맞춤 |
| `4`, `0`, `Ctrl+0` | 실제 크기(1:1) |
| `5` | 똑똑한 맞춤(줄이기만) |
| `Ctrl+1` | 창 가득 늘이기 |
| `L` | 다음 파일에도 이 맞춤 유지 |
| `+` | 확대 |
| `-` | 축소 |
| `R` | 시계 방향 회전 |
| `Shift+R` | 반시계 방향 회전 |
| `H` | 좌우 뒤집기 |
| `V` | 위아래 뒤집기 |
| `N` | 도트 그림용 또렷한 확대 |

**슬라이드 쇼 중**

| 키 | 하는 일 |
|---|---|
| `S`, `F5` | 슬라이드 쇼 시작/정지 |
| `]` | 느리게(0.5초) |
| `[` | 빠르게(0.5초) |
| `Shift+]` | 느리게(0.1초) |
| `Shift+[` | 빠르게(0.1초) |

**영상·음악·움직이는 그림이 떠 있을 때**

| 키 | 하는 일 |
|---|---|
| `Space` | 재생/일시정지 |
| `.` | 한 프레임 앞으로 |
| `,` | 한 프레임 뒤로 |
| `Ctrl+]` | 빠르게(0.25배씩) |
| `Ctrl+[` | 느리게(0.25배씩) |
| `Right` | 5초 뒤로 |
| `Left` | 5초 앞으로 |
| `Up` | 소리 5% 크게 |
| `Down` | 소리 5% 작게 |
| `Shift+M` | 음소거/해제 |
| `[` | 여기서부터 반복(A) |
| `]` | 여기까지 반복(B) |
| `\` | 반복 끄기 |
| `Ctrl+\` | 보통 속도 |
| `Z` | 자막 0.5초 빠르게 |
| `X` | 자막 0.5초 늦게 |
| `A` | 다음 소리 트랙 |
| `C` | 다음 자막 |

**여러 쪽이 든 TIFF·ICO**

| 키 | 하는 일 |
|---|---|
| `.` | 파일 안 다음 쪽 |
| `,` | 파일 안 이전 쪽 |

<!-- keys:end -->

## 무엇으로 지었나

- **언어**: 순수 ISO C23 (`-std=c23`)
- **화면**: Win32 GUI, Direct2D 1.1+, DirectWrite, DXGI
- **코덱**: Windows Imaging Component(WIC), FFmpeg(`libav*` 동적 연결)
- **바탕 라이브러리**: `proven_c_lib`(메모리 아레나, `u8str`, 동적 배열)
- **이식성**: 핵심 알고리즘은 리눅스에서 그대로 시험하고, 윈도우 실행 파일은 리눅스 빌드 기계에서 MinGW-w64 로 교차 빌드합니다.

## FFmpeg (선택)

대부분의 파일은 윈도우 자체 디코더로 재생됩니다. FFmpeg 은 그것으로 못 여는
형식에만 필요하고, **`ffmpeg.exe` 가 아니라 DLL 다섯 개**입니다.

```
avcodec-63.dll  avformat-63.dll  avutil-61.dll  swscale-10.dll  swresample-7.dll
```

[BtbN 의 윈도우 빌드](https://github.com/BtbN/FFmpeg-Builds/releases) 에서
`ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip` 을 받으세요. 반드시 **9.0**
이어야 하고 이름에 **shared** 가 있어야 합니다. `shared` 가 없는 판에는 실행
파일만 있고 DLL 이 없으며, 다른 판은 DLL 이름의 번호가 달라 루브라뷰가 쓰지
않습니다. 압축 속 `bin` 폴더의 다섯 개를 `rubraview-v0.0.16.exe` 옆에 복사하고
루브라뷰를 다시 실행하면 됩니다. `lib` 폴더의 `.dll.a`, `.lib` 는 FFmpeg 을
링크해 빌드할 때 쓰는 것이라 실행에는 쓰지 않습니다 — 그냥 두세요.

설정(`F10`) › Video 에서 인식 여부를 보여 주고, 같은 안내를 그 쪽 아래에도
적어 두었습니다.

## 문서

- [RFC-0001: 구조와 멀티미디어 파이프라인](docs/rfc/rfc-0001-rubraview-architecture.md)
- [RFC 목록](docs/rfc/rfc-0000-index.md)
- [사용 설명서](docs/manual/)
- [에이전트 작업 지침](docs/agents/)

## 빌드와 시험

### 리눅스 호스트(단위 시험)
```sh
make test
```

### 윈도우 실행 파일(교차 빌드)
```sh
make win64
```

## 라이선스

MIT License. [LICENSE](LICENSE) 를 보세요.
