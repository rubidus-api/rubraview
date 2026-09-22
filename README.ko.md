# Rubraview

**Rubraview v0.0.9**(최신 판) 내려받기 — [rubraview-v0.0.9.zip (Windows 11 x64)](https://github.com/rubidus-api/rubraview/releases/download/v0.0.9/rubraview-v0.0.9.zip) · [모든 판](https://github.com/rubidus-api/rubraview/releases)

[English](README.md) · **한국어**

순수 C23 과 WinAPI 로 지은, 가볍고 빠른 윈도우 멀티미디어 뷰어이자 동영상 재생기이며 일괄 이미지 처리기입니다.

압축을 풀고 `rubraview-v0.0.9.exe` 를 실행하면 됩니다. 설치 프로그램도, 옆에 둬야 하는 파일도 없습니다. 예외는 FFmpeg 의 DLL 하나뿐이고, 그것도 Media Foundation 이 못 여는 형식에만 필요합니다.

> 정본은 영문 README 이고, 이 문서는 그 번역입니다.

## 무엇인가

군더더기 없이 빠른 도구를 목표로 합니다. 반응이 빠른 보기, 원본을 건드리지 않는 이미지 보정, 많은 파일을 한 번에 처리하는 일괄 작업을 한 프로그램에 담았습니다.

- **GPU 로 그리는 화면**: Direct2D(D2D1)와 Direct3D 11 로 60~144 FPS 의 부드러운 이동과 소수점 단위 확대.
- **딸린 것 없는 이미지 해독**: JPEG, PNG, GIF, WebP, TIFF, BMP, ICO 를 윈도우의 WIC 로 그대로 엽니다.
- **동영상 재생**: 분리된 PAL 을 통한 FFmpeg(`libavcodec`/`libavformat`) 연결로 재생, 한 프레임씩 이동, 정지 화면 저장.
- **보정과 후처리**: 노출·대비·채도·감마·언샤프 마스크·가우시안 흐림을 실시간으로, 리샘플링은 Nearest·Bilinear·Bicubic·Lanczos-3.
- **일괄 처리**: 여러 스레드로 도는 변환·크기 조정기. 창에서도, 창 없이 명령줄에서도 씁니다.
- **순수 C23**: 벤더링한 `proven_c_lib` 의 아레나·문자열 조각·동적 배열 위에서, 힙 파편화 없이.

## 무엇으로 지었나

- **언어**: 순수 ISO C23 (`-std=c23`)
- **화면**: Win32 GUI, Direct2D 1.1+, DirectWrite, DXGI
- **코덱**: Windows Imaging Component(WIC), FFmpeg(`libav*` 동적 연결)
- **바탕 라이브러리**: `proven_c_lib`(메모리 아레나, `u8str`, 동적 배열)
- **이식성**: 핵심 알고리즘은 리눅스에서 그대로 시험하고, 윈도우 실행 파일은 리눅스 빌드 기계에서 MinGW-w64 로 교차 빌드합니다.

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
