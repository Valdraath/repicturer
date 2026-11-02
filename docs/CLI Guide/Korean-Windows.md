# RePicturer 명령줄 사용 안내 (Windows)

`repicturer-cli.exe`는 RePicturer 창 뒤에서 동작하는 변환 프로그램입니다. 직접 실행하거나 스크립트, 배치 파일에서 호출해
프로그램을 열지 않고도 이미지를 변환할 수 있습니다. 창에서 할 수 있는 일은 모두 할 수 있습니다: AI 모델,
'크기 조절 - 비 AI', 환경설정의 모든 항목, 폴더와 파일 목록. 이 안내는 **Windows**용입니다. Linux용은
[Korean-Linux.md](Korean-Linux.md)를 보세요.

## 목차

1. 시작하기
2. 네 가지 모드
3. 예시
4. 모든 옵션
5. 모드별로 쓰는 옵션
6. 폴더, 파일 목록, 배치 파일
7. 종료 코드와 메시지
8. 이전 옵션 이름
9. 문제 해결

## 1. 시작하기

1. RePicturer 폴더(`RePicturer.exe`와 `repicturer-cli.exe`가 있는 폴더)를 엽니다.
2. 파일 탐색기의 주소 표시줄을 클릭하고 `cmd` 또는 `powershell`을 입력한 뒤 Enter를 누릅니다. 그 폴더에서 명령 창이 열립니다.
3. 첫 변환을 실행합니다:

   ```
   repicturer-cli.exe -i photo.jpg -o photo-2x.png
   ```

   **PowerShell**에서는 이름 앞에 `.\`를 붙입니다:

   ```
   .\repicturer-cli.exe -i photo.jpg -o photo-2x.png
   ```

4. 모든 옵션 보기:

   ```
   repicturer-cli.exe -h
   ```

다른 옵션이 없으면 **노이즈 제거 및 확대 - AI**, 2배, 노이즈 제거 레벨 0, 모델 `models-cunet`으로 첫 번째 GPU에서 실행합니다.

**공백이 있는 경로**는 따옴표로 묶습니다: `-i "C:\My Pictures\cat.png"`. 모델은 폴더 이름만 써도 됩니다
(`-m models-realcugan-se`): 명령 창이 어느 폴더에 있든 `repicturer-cli.exe` 옆에서 찾습니다.

## 2. 네 가지 모드

| 창의 모드 | `-M` 값 | 하는 일 |
|---|---|---|
| 노이즈 제거 및 확대 - AI | `denoise-scale` | AI 모델로 노이즈 / JPEG 압축 흔적을 없애고 확대 |
| 확대 - AI | `scale` | AI 모델로 확대, 노이즈 제거 없음 |
| 노이즈 제거 - AI | `denoise` | 노이즈 제거, 크기는 그대로 |
| 크기 조절 - 비 AI | `non-ai-scale` | 기존 필터(Lanczos, Bicubic, Jinc 등)로 크기 조절, 모델 없음 |

`-M`이 없으면 waifu2x-ncnn-vulkan과 같이 다른 옵션에 따라 모드가 정해집니다: `-n -1`은 확대만, `-s 1`은 노이즈 제거만,
그 밖에는 노이즈 제거 및 확대입니다.

## 3. 예시

**애니메이션 이미지, 2배, Real-CUGAN SE로 가볍게 노이즈 제거:**

```
repicturer-cli.exe -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**사진을 정확히 너비 3840픽셀로(높이는 비율에 맞춤), NMKD Superscale:**

```
repicturer-cli.exe -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**정확히 1920 x 1080 (waifu2x-caffe처럼 그 크기로 늘림):**

```
repicturer-cli.exe -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**JPEG 압축 흔적만 없애고 크기는 그대로:**

```
repicturer-cli.exe -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25배, 변환 후 축소를 더 선명하게(Lanczos 4탭 + 안티 링잉):**

```
repicturer-cli.exe -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**픽셀 아트, 4배, AI 없이(또렷한 픽셀):**

```
repicturer-cli.exe -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**사진을 AI 없이 1.5배, Lanczos 3탭 + 안티 링잉:**

```
repicturer-cli.exe -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**이미지를 절반 크기로, 선형광에서 축소:**

```
repicturer-cli.exe -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**색 띠가 있는 게임 배경(하늘에 계단), AI 2배, AI 전에 컬러 디밴딩:**

```
repicturer-cli.exe -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**부드러운 그라데이션이 있는 사진 축소: 선형광 + 오차 확산 디더링:**

```
repicturer-cli.exe -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**폴더 전체(하위 폴더 포함)를 WebP로, 이미 변환한 이미지는 건너뛰기:**

```
repicturer-cli.exe -i D:\Pictures -o D:\Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**CPU로 실행(GPU 없음), 코어 8개 사용:**

```
repicturer-cli.exe -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. 모든 옵션

### 입력과 출력 (모든 모드)

| 옵션 | 의미 |
|---|---|
| `-i`, `--input path` | 입력 이미지 또는 폴더 |
| `-o`, `--output path` | 출력 이미지 또는 폴더 |
| `-r`, `--recursive` | 하위 폴더도 변환, 폴더 구조 유지 |
| `-l`, `--extensions list` | 폴더에서 읽을 확장자, 예: `png:jpg:jpeg:webp` (기본값: 폴더의 모든 파일. 다른 파일도 있으면 지정하세요) |
| `-L`, `--file-list file` | UTF-8 텍스트 파일, 한 줄에 `입력<TAB>출력` 한 쌍 (6장 참고) |
| `-k`, `--no-overwrite` | 출력 파일이 이미 있는 이미지는 건너뜀(기존 파일 유지). 창의 '기존 파일 덮어쓰지 않기' |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (기본값: 출력 파일의 확장자, 없으면 png). 움직이는 GIF / WebP는 그대로 움직임 |
| `-q`, `--quality q` | jpg 0-100 (기본값 100), webp 1-100 또는 101 = 무손실 (기본값 101), tga 0 / 1 = RLE 끄기 / 켜기 |

### 모드와 크기 (모든 모드)

| 옵션 | 의미 |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`, `scale`, `denoise` 또는 `non-ai-scale` (2장) |
| `-s`, `--scale r` | 배율, 임의의 양수: `2`, `1.5`, `0.5` (기본값 2) |
| `-W`, `--width w` | 출력 너비. `-H`가 없으면 높이는 비율에 맞춤 |
| `-H`, `--height h` | 출력 높이. `-W`가 없으면 너비는 비율에 맞춤 |
| `-W`와 `-H`를 함께 | 정확히 그 너비 x 높이 |

### AI 모드 (`denoise-scale`, `scale`, `denoise`)

| 옵션 | 의미 |
|---|---|
| `-m`, `--model path` | 모델 폴더 (기본값 `models-cunet`). 엔진은 폴더로 정해집니다: `models-cunet`, `models-upconv_7_*`, `models-upresnet10`은 waifu2x, `models-realcugan-se / -pro / -nose`는 Real-CUGAN, `models-realesrgan-*`, `models-realesr-*`, `models-realesrnet-x4plus`, `models-nomos8ksc`, `models-lsdir-compact`, `models-nmkd-siax / -superscale`는 Real-ESRGAN |
| `-n`, `--noise-level n` | 노이즈 제거 레벨 `-1`(없음), `0`-`3` (기본값 0). Real-CUGAN: 0 = 보수적. Real-ESRGAN General: 노이즈 제거 강도. LSDIR Compact: 정리 강도. NMKD Siax: 디테일 레벨 0-3. 다른 Real-ESRGAN 모델은 스스로 정리합니다 |
| `-t`, `--split-size t` | `0` = 자동(기본값), `1` = 분할 안 함(이미지 전체를 타일 1개로), `2` 이상 = 그 크기의 타일. GPU가 여러 개면 GPU마다 값 하나 (`0,0`) |
| `-c`, `--sync-gap n` | Real-CUGAN의 Sync gap `0`-`3` (기본값 3, 0 = 끄기) |

AI 전에는 `--deband`로 원본의 색 띠를 매끄럽게 할 수 있습니다(창의 '환경설정 > AI 변환 전 설정'). AI 후에는 이미지가
**변환 후** 필터로 정확한 출력 크기(예: 2.25배, 또는 너비 x 높이)로 축소됩니다: 아래의 `-F`, `-T`, `-a`, `--dither`,
`--linear-light`입니다(창의 '환경설정 > AI 변환 후 설정'). 기본값은 Bicubic Mitchell, 안티 링잉 없음, 디더링 없음, 선형광
없음입니다. 모델 그대로의 2x / 3x / 4x는 그대로 저장됩니다.

### 비 AI 필터, 디밴딩, 디더링 (`non-ai-scale`, 그리고 AI 모드에서 AI의 앞뒤)

| 옵션 | 의미 |
|---|---|
| `-F`, `--filter name` | `nearest`, `bilinear`, `mitchell` (기본값), `catrom`, `lanczos`, `spline36`, `jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (기본값) / `4` / `8`, Jinc `3` (기본값) / `4` / `8`. 다른 필터는 무시 |
| `-a`, `--anti-ringing a` | `off` (기본값), `relaxed`, `strict`: `catrom`, `lanczos`, `spline36`, `jinc`의 헤일로를 줄임 |
| `--linear-light` | 선형광에서 크기 조절. 이미지가 작아질 때만 사용(어느 변도 커지지 않고 적어도 한 변이 작아질 때) |
| `--dither d` | `off` (기본값), `ordered`, `error-diffusion`: 결과를 8비트 색으로 반올림하는 방식. 디더링은 부드러운 그라데이션에 새 띠가 생기지 않게 함. 크기를 조절할 때 사용 |
| `--deband d` | `off` (기본값), `low`, `medium`, `high`: **컬러 디밴딩**. AI나 비 AI 필터 전에 이미지에 이미 있는 색 띠(하늘, 그림자, 부드러운 그라데이션)를 매끄럽게 함. 평평한 부분만 바뀜 |

| 필터 | 특징 |
|---|---|
| `nearest` | 최근접 이웃: 각진 또렷한 픽셀, 흐림 없음. 2x / 3x / 4x 픽셀 아트용 |
| `bilinear` | 빠르고 부드러움, 약간 흐림, 헤일로 없음 |
| `mitchell` | Bicubic Mitchell: 부드럽고 매끄러움, 헤일로 거의 없음. 무난한 선택 |
| `catrom` | Bicubic Catmull-Rom: Mitchell보다 선명, 약간의 헤일로 |
| `lanczos` | 선명하고 세부를 살림, 헤일로 있음(탭이 많을수록 선명하고 헤일로도 많음) |
| `spline36` | Catmull-Rom과 Lanczos의 중간: 선명, 깔끔한 가장자리, 헤일로 적음 |
| `jinc` | 원형 필터: 선명, 매끄러운 대각선, 계단 현상 적음. 꽤 느림 |

### 성능 (모든 모드)

| 옵션 | 의미 |
|---|---|
| `-g`, `--gpu id` | 사용할 GPU: `0`, `1`… 또는 CPU는 `-1` (기본값: 첫 번째 GPU). GPU 여러 개: `0,1` |
| `-j`, `--threads l:p:s` | 읽기 : 처리 : 저장 스레드 수 (기본값 `1:2:2`). 처리 = GPU마다 동시에 처리할 이미지 수(1-32), `-g -1`이면 CPU 코어 수. GPU 2개: `1:2,2:2` |
| `--vram-percent p` | 변환 시작 시 남은 GPU 메모리의 최대 p %를 사용 (20-80, 기본값 60) |
| `--ram-percent p` | 변환 시작 시 남은 RAM의 최대 p %를 사용 (20-80, 기본값 60) |

### 기타

| 옵션 | 의미 |
|---|---|
| `-h`, `--help` | 도움말 표시 |
| `-v`, `--verbose` | 모든 단계 표시(실행한 네트워크, 메모리, 장치) |
| `--list-gpu` | Vulkan GPU 목록을 표시하고 종료 |
| `--progress` | 기계가 읽는 진행 상황 출력(창이 사용) |

## 5. 모드별로 쓰는 옵션

| 창의 설정 | AI 모드 | 크기 조절 - 비 AI | 옵션 |
|---|---|---|---|
| 입력 / 출력 경로, 사용자 지정 폴더 | ✓ | ✓ | `-i` `-o` (`-r`, `-l`, `-L`) |
| 출력 확장자 / 출력 품질 | ✓ | ✓ | `-f` `-q` |
| 모드 | ✓ | ✓ | `-M` |
| 노이즈 제거 레벨 / 디테일 레벨 | ✓ | – | `-n` |
| 확대: 배율, 너비, 높이, 너비x높이 | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| 모델 | ✓ | – | `-m` |
| 필터, 탭 수, 안티 링잉 (메인 창) | – | ✓ | `-F` `-T` `-a` |
| 처리 장치 | ✓ | ✓ | `-g` |
| 분할 크기 | ✓ | – | `-t` |
| 스레드 수 | ✓ | ✓ | `-j` |
| 환경설정: Real-CUGAN의 Sync gap | ✓ | – | `-c` |
| 환경설정: AI 변환 전 설정 (컬러 디밴딩) | ✓ | – | `--deband` |
| 환경설정: AI 변환 후 설정 | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| 환경설정: 비 AI 필터 설정 | – | ✓ | `--deband` `--dither` `--linear-light` |
| 환경설정: 기존 파일 덮어쓰지 않기 | ✓ | ✓ | `-k` |
| 환경설정: VRAM / RAM 한도 | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`, `-T`, `-a`, `--dither`, `--deband`, `--linear-light`는 `non-ai-scale`에서는 비 AI 설정이고, AI 모드에서는 변환 전 /
AI 변환 후 설정입니다. 한 번 실행할 때 모드는 하나이므로 옵션 한 벌이면 충분합니다.

## 6. 폴더, 파일 목록, 배치 파일

**폴더:** `-i`에 폴더, `-o`에 저장할 폴더를 지정합니다. 각 이미지는 같은 이름에 출력 형식의 확장자(`-f`, 기본값 png)가
붙습니다. `-r`을 붙이면 하위 폴더도 같은 구조로 변환됩니다.

**파일 목록**(`-L`)으로 원하는 이미지를 원하는 이름으로 변환할 수 있습니다. 각 줄은 `입력`, TAB, `출력`입니다:

```
D:\in\a.png	D:\out\a-2x.png
D:\in\b.jpg	D:\out\b-2x.webp
```

```
repicturer-cli.exe -L list.txt -m models-realcugan-se -s 2
```

**배치 파일**(`convert.bat`): 끌어다 놓은 이미지를 모두 변환해 원본 옆에 저장합니다:

```bat
@echo off
cd /d "%~dp0"
for %%F in (%*) do (
    repicturer-cli.exe -i "%%~F" -o "%%~dpnF-2x.png" -m models-realcugan-se -s 2
)
pause
```

RePicturer 폴더에 저장하고 파일 탐색기에서 이미지를 끌어다 놓으세요.

**PowerShell**: 폴더의 모든 JPG를 '크기 조절 - 비 AI'로 처리:

```powershell
Get-ChildItem D:\in -Filter *.jpg | ForEach-Object {
    .\repicturer-cli.exe -i $_.FullName -o ("D:\out\" + $_.BaseName + ".png") -M non-ai-scale -F lanczos -s 2
}
```

(이미지가 많으면 폴더를 지정해 한 번에 실행하는 편이 빠릅니다: 모델을 한 번만 불러옵니다.)

## 7. 종료 코드와 메시지

| 종료 코드 (`%ERRORLEVEL%`) | 의미 |
|---|---|
| `0` | 모든 이미지를 변환함 |
| `1` | 일부 이미지가 실패함(실패한 이미지가 모두 표시됨) |
| `-1` | 변환이 시작되지 않음: 잘못된 옵션, 모델이나 입력이 없음 |

GPU에서 실패한 이미지(메모리 부족, 드라이버 문제)는 최대 2번 다시 시도합니다. 먼저 타일 2개로, 다음에 타일 1개로.
그래도 실패하면 메시지를 표시하고 변환을 멈춥니다. '크기 조절 - 비 AI'에서는 GPU가 처리하지 못한 이미지를 대신 CPU로
처리하며, 결과는 같습니다.

## 8. 이전 옵션 이름

waifu2x-ncnn-vulkan과 이전 RePicturer의 옵션 이름도 그대로 쓸 수 있어, 예전 스크립트도 계속 동작합니다:

| 이전 이름 | v3.0부터의 이름 |
|---|---|
| `--noise_level` | `--noise-level` |
| `--scale_ratio` | `--scale` |
| `--scale_width`, `--scale_height` | `--width`, `--height` |
| `--tile_size` | `--split-size` |
| `--model_dir` | `--model` |
| `--jobs` | `--threads` |
| `--output_quality` | `--quality` |
| `--input_extension_list` | `--extensions` |
| `--no_overwrite`, `--file_list` | `--no-overwrite`, `--file-list` |
| `--syncgap` | `--sync-gap` |
| `-M noise_scale` / `noise` / `resize` | `-M denoise-scale` / `denoise` / `non-ai-scale` |

## 9. 문제 해결

* **"내부 또는 외부 명령... 이 아닙니다"**: 명령 창이 RePicturer 폴더에 있지 않습니다. `cd`로 이동하거나 전체 경로를
  쓰세요: `"C:\Tools\RePicturer\repicturer-cli.exe"`.
* **PowerShell에서 명령을 찾을 수 없다고 나옴**: `.\repicturer-cli.exe`로 쓰세요.
* **"no vulkan device found, falling back to cpu"**: 그래픽 드라이버(NVIDIA, AMD, Intel)를 업데이트하세요. `--list-gpu`로
  변환 프로그램이 인식한 GPU를 볼 수 있습니다.
* **"GPU error (probably out of GPU memory)"**: 분할 크기를 줄이거나(`-t 200`, `-t 100`), 스레드를 줄이거나(`-j 1:1:2`),
  `--vram-percent`를 올리세요.
* **글자가 깨져 보임**: 변환 프로그램은 UTF-8로 출력합니다. 예전 콘솔에서는 먼저 `chcp 65001`을 실행하세요.
