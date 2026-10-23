# RePicturer

<p align="center"><img src="repicturer-logo.png" width="160" alt="RePicturer logo"></p>

**RePicturer** is a free, open-source AI image upscaler and denoiser for **Windows and Linux**. It enlarges pictures (any scale,
or an exact width / height) and removes noise, JPEG artifacts and blur, using 16 AI models for anime and illustrations,
real-life photos and photorealistic 3D renders. It runs on any **Vulkan** GPU (NVIDIA, AMD, Intel) with no CUDA, Python or
installer needed: unzip and run. A CPU fallback is built in.

**Based on:** the GUI is rebuilt from **waifu2x-caffe** (lltcggie), and the converter (`repicturer-cli`) is built on
**waifu2x-ncnn-vulkan**, **realcugan-ncnn-vulkan** (nihui) and **Real-ESRGAN-ncnn-vulkan** (Xintao Wang), all running on Tencent's
**ncnn**. RePicturer is an independent project; it is not affiliated with these projects or the model authors.

## Download

Get `RePicturer-windows.zip` or `RePicturer-linux.zip` from the **Releases** page, unzip it anywhere and start
`RePicturer.exe` (Windows) or `./RePicturer` (Linux). The release downloads contain the program, the converter and all models.

## Features

* **16 models, 3 engines:** waifu2x, Real-CUGAN and Real-ESRGAN, plus the community photo models NMKD Siax / Superscale,
  Nomos8kSC and LSDIR Compact. The **?** button explains which model to use for which picture.
* **Any size:** a scale rate, a target width or height, or width x height.
* **Modes:** Denoise & Scale, Scale only, Denoise only, Scale & Auto Denoise (JPEG sources only), with denoise or detail levels.
* **Formats:** PNG, JPG, TIF, BMP, TGA, WEBP and GIF in; 12 formats out at 8/16/32 bit. Animated GIF / WebP stay animated.
* **Batch:** files and folders (with drag & drop), multi-GPU, no-overwrite, and a command line.
* **9 languages:** English, Chinese (Traditional / Simplified), Japanese, Korean, Portuguese (Brazil), Russian, Spanish, Vietnamese.

The converter behind the GUI (`repicturer-cli`, based on waifu2x-ncnn-vulkan) is ncnn rebuilt from source with the waifu2x-caffe features added, so every engine supports:

* any scale ratio, or a target width, height or width x height;
* denoise modes;
* 11 output formats at 8/16/32 bit;
* folders, recursion and no-overwrite.

```
RePicturer(.exe)              the program (GUI)
repicturer-cli(.exe)          converter used by the GUI (also usable from the command line)
models-*/                     16 models, see below
lang/                         9 languages
fonts/                        Noto Sans fonts for every UI language
crop_size_list.txt            split size presets (same file as waifu2x-caffe)
icon.png, install-desktop-entry.sh   (linux) application menu entry
```

## Models

The **?** button next to the model list in the program explains each model and lists which one to use for which kind of picture.

| Model | Engine | Native scale | Use it for |
|---|---|---|---|
| **Real-CUGAN SE** (default) | Real-CUGAN | 2x 3x 4x | Anime, manga, 2D illustration, stylized 3D: the best overall for 2D |
| Real-CUGAN Pro | Real-CUGAN | 2x 3x | Line art and comics: sharper |
| Real-CUGAN NoSE | Real-CUGAN | 2x | Clean 2D sources, no denoise |
| **NMKD Superscale** | Real-ESRGAN (ESRGAN) | 4x | Photos and photorealistic 3D renders: natural detail, a good first choice |
| NMKD Siax | Real-ESRGAN (ESRGAN) | 4x | Clean photos and renders: crisp textures (keeps noise), with a Detail level |
| Nomos8kSC | Real-ESRGAN (ESRGAN) | 4x | JPEG-damaged, web and social media photos |
| Real-ESRGAN General | Real-ESRGAN | 4x | Photos, noisy renders: fast, with adjustable denoise (Level 0-3) |
| LSDIR Compact | Real-ESRGAN (Compact) | 4x | Clean photos and renders, fast: Scale only keeps the most detail, Level 0-3 add more and more cleanup |
| RealESRNet x4plus | Real-ESRGAN | 4x | Clean photos and renders: faithful, does not invent texture |
| Real-ESRGAN x4plus | Real-ESRGAN | 2x (x2plus) 4x | Photos and realistic 3D: good, but its strong built-in denoise cannot be turned down and can wipe out detail |
| Real-ESRGAN x4plus anime | Real-ESRGAN | 4x | Strong cleanup of 2D art |
| Real-ESRGAN animevideo v3 | Real-ESRGAN | 2x 3x 4x | Anime video frames / screenshots, very fast |
| waifu2x CUnet | waifu2x | 2x | The most faithful to the original 2D art, softer |
| waifu2x UpResNet10 | waifu2x | 2x | Balanced 2D (converted from waifu2x-caffe) |
| waifu2x UpRGB | waifu2x | 2x | The fastest 2D model |
| waifu2x UpPhoto | waifu2x | 2x | Photos: light and fast, for weak GPUs or very large batches |

The outdated waifu2x-caffe vgg_7 models (RGB, Photo, Y, ukbench) are no longer shipped. Newer models beat them in both quality and speed. Any extra `models-*` folder you drop next to the program appears in the list.

**Denoise level by engine:**
* waifu2x: 0-3, each level stronger than the last.
* Real-CUGAN: 0 = conservative, 1-3 = denoise; SE has levels 1-2 at 2x only, Pro has 0 and 3.
* Real-ESRGAN General: 0-3 = denoise strength 25 / 50 / 75 / 100 % (Scale only = 0 %). The folder holds the official realesr-general-x4v3 and realesr-general-wdn-x4v3 networks blended at those strengths.
* LSDIR Compact: Scale only = no cleanup (4xLSDIRCompactN3); Level 0 = JPEG cleanup (C3), Level 1 = 75 % C3 + 25 % R3 (the author's own blend), Level 2 = 50 / 50, Level 3 = blur, noise and JPEG (R3).
* NMKD Siax: the box becomes **Detail level** (Faithful / Balanced / Detailed / Maximum = 40 / 60 / 80 / 100 % Siax, blended with ESRGAN RRDB_PSNR_x4, the soft model it was trained from). It is not a denoise level and works in every mode.
* The other Real-ESRGAN, NMKD and Nomos8kSC models: no levels, because the network removes noise, JPEG artifacts and blur by itself.

**Heavy models:** Real-ESRGAN x4plus, RealESRNet x4plus, Nomos8kSC and NMKD Siax / Superscale are large 4x networks. Thread(s) is locked at 1 for them (greyed out): each GPU converts one image at a time, so they do not run out of video memory.

**Model licenses:** waifu2x and Real-CUGAN are MIT, Real-ESRGAN (x4plus, RealESRNet, General, anime) is BSD-3-Clause, Nomos8kSC and LSDIR Compact are CC-BY-4.0 (by Helaman) and NMKD Siax / Superscale are WTFPL (by Nmkd). The community models were converted from PyTorch to ncnn for this program; see `THIRD_PARTY_LICENSES.md` and the `LICENSE.txt` in their folders.

## Hardware

* **GPU (default):** any Vulkan GPU, with no CUDA needed:
  * NVIDIA GTX 600 series and newer (GTX 1050 … RTX 50)
  * AMD GCN and newer (RX 470/570 … RX 9000)
  * Intel HD 500+, Iris Xe and Arc

  At startup ncnn checks which fp16/int8 features each device supports. The split (tile) size is chosen automatically from free video memory, so 2 GB cards work.
* **CPU:** used **only as a fallback** when no Vulkan GPU exists (or when you select it). All three engines work on any x86-64 Intel or AMD CPU, with SSE2/AVX/AVX2/AVX-512 chosen at runtime. It is 10-100x slower than a GPU.
* **Driver:** if *Check GPU* finds nothing, update the graphics driver. On Linux, install the Mesa Vulkan drivers (`mesa-vulkan-drivers`) or the NVIDIA driver, plus `libvulkan1`.

## The GUI (everything waifu2x-caffe-gui has, plus ncnn options)

* **Input/output:** input file, folder or multiple files, with drag & drop. **Browse** opens the program's own dark file explorer (it remembers the last folder), **Reset to Default** clears the paths and returns every setting except the language to the first-start state, after a confirmation, and the output name is filled in automatically (`name(RealCUGAN-SE)(noise_scale)(Level0)(x2.00).png`).
* **Formats:** input PNG, JPG, JPEG, TIF, TIFF, BMP, TGA, WEBP and GIF (fixed). Output .png .bmp .jpg .jp2 .sr .tif .hdr .exr .ppm .webp .tga .gif, with quality and 8/16/32-bit depth (webp quality 101 = lossless).
* **Animated GIF / WebP:** every frame is converted with the same model and settings, and the result stays animated, keeping frame timing and loop count. Output .gif or .webp keeps that format, so GIF can become animated WebP and vice versa. Any other output format saves the animation in its source format (GIF -> .gif, WebP -> .webp), with a note in the log. GIF output uses an optimized 256-colour palette per frame with transparency. The progress bar shows the frame being converted.
* **Modes:** Denoise & Scale, Scale only, Denoise only, and Scale & Auto Denoise (denoises only JPEG sources, detected from the file content), with denoise levels. Models whose cleanup is built in and cannot be turned off (Real-ESRGAN x4plus / x4plus anime / animevideo v3, RealESRNet, Nomos8kSC, NMKD) only allow **Scale only**, selected automatically; at 1.00x they clean up the image at its original size.
* **Scale:** `<Scale Rate>` (digits, up to 2 decimals), `<Width>`, `<Height>` (whole numbers) or `<Width>x<Height>` (e.g. 1920x1080). Each box only accepts its own format; anything else is blocked with a warning.
* **Processing:** TTA mode. Split size (0 = auto) and Thread(s) (replaces caffe's batch size), each with a hover tip and a **?** guide.
* **Device:** a Device box (Auto / each GPU / CPU) and a **Check GPU** button.
* **App Setting:** auto start, processor, boot with arguments, no overwrite, GPU, fixed folders. Also multi-GPU (`0,1`) with a split size and threads per GPU, load/save threads, the Real-CUGAN sync gap, and showing the converter log.
* **Other:** 9 UI languages (English, Chinese Traditional / Simplified, Japanese, Korean, Portuguese (Brazil), Russian, Spanish, Vietnamese), progress bar, time left, log, and `setting.ini` next to the program.
* **Window:** resizable and maximizable. The content scales up with the window (fonts, boxes, buttons) up to a 1360 px wide column, centred horizontally and pinned to the top. The log grows up to twice its height when there is room. The original size is the minimum.
* **Pass report:** the log shows which networks really ran for the first image (every image with the verbose log), e.g. `noise2_scale2.0x_model (x2) -> scale2.0x_model (x2) -> resize 800x600 to 2560x1920`. This shows that Denoise & Scale really did both.
* **Command line**, same as waifu2x-caffe-gui:

  ```
  RePicturer [files...] -o out -m mode -n level -s rate -w width -h height -e ext -q quality -d depth
                        -c split -b threads --gpu id -t 0/1 -y model --auto_start 0/1 --auto_exit 0/1 --no_overwrite 0/1 -j l:p:s
  ```

  `-y` takes a model type such as `realcugan-se`, `realesrgan-x4plus` or `cunet`.

## How any scale works

1. The factor is worked out from the rate, the target width, the target height, or for width x height the larger of the two.
2. The first pass applies the chosen denoise level. Further native passes (x2 / x3 / x4, picked by the smallest scale that reaches the target) run until the size is at least the target.
3. The result is resampled to the exact size with a high-quality cubic filter (in float when the output is 16/32 bit). Width x height is stretched to exactly W x H, as in waifu2x-caffe.

## Converter command line

```
repicturer-cli -i in -o out [-m model-folder] [-n level] [-s rate | -W width | -H height] [-M noise|scale|noise_scale|auto_scale]
                    [-f format] [-q quality] [-d 8|16|32] [-t tile] [-g gpu(s)] [-j l:p:s] [-x] [-c syncgap]
                    [-l png:jpg] [-r] [-k] [-L list.txt] [--list-gpu] [--progress] [-v]
```

The engine follows the model folder (`models-cunet`, `models-realcugan-se`, `models-realesrgan-x4plus`, …). Upstream waifu2x-ncnn-vulkan options keep working.

## Fixes compared with the official ncnn releases

* **CPU mode crashed in waifu2x-ncnn-vulkan 20250915.** It called `set_vulkan_device(NULL)` and lacked the Gemm layer that CPU deconvolution needs. Both are fixed, and the program falls back to the CPU when no GPU is found.
* **Real-ESRGAN gets a CPU path,** which the official realesrgan-ncnn-vulkan never had.
* **Robustness:** model loading errors are reported, destructors are null-safe, and the pixel order is the same on every OS.
* **MinGW builds** use `-Wa,-muse-unaligned-vector-move`, which prevents AVX/AVX-512 crashes on CPUs such as the i7-11800H and Ryzen 7000.

## Linux

* Extract `RePicturer-linux.zip` with `unzip` (it keeps the executable permissions; if your archive tool drops them, run `chmod +x RePicturer repicturer-cli install-desktop-entry.sh`) and run `./RePicturer`.
* `./install-desktop-entry.sh` adds the program to your application menu with its name and icon.
* It needs glibc 2.27+ (Ubuntu 18.04+, Debian 10+, Fedora, Arch …), X11 or XWayland, OpenGL 3.0, and a Vulkan driver.

## Building

The sources of every dependency are in `third_party/`: ncnn + glslang, zlib-ng, libwebp, OpenJPEG, Dear ImGui, GLFW, stb and the X11 headers.

Requirements: CMake 3.16+, Ninja (or make) and a C++17 compiler: MinGW-w64 GCC on Windows (for example w64devkit),
GCC or Clang on Linux. No Vulkan SDK, X11 or OpenGL development packages are needed.

**Windows** (MinGW-w64):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --target repicturer-cli repicturer
```

**Linux:**

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++"
cmake --build build --target repicturer-cli repicturer
```

**Linux, cross-compiled with Zig** (from Windows, macOS or Linux; set `ZIG` to the zig executable, targets glibc 2.27):

```
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake
cmake --build build-linux --target repicturer-cli repicturer
```

The programs are written to `build/bin`. To run them, put `resources/lang`, `resources/fonts`, `resources/crop_size_list.txt` and
the `models-*` folders next to them. The model files are not stored in the repository (about 400 MB): take the `models-*`
folders from a release download (see `resources/models/README.md`).

## Credits / licenses

Every bundled model and library allows free use, sharing and changes, also commercially. Their license texts ship in the `licenses/` folder.

* **waifu2x:** nagadomi, MIT
* **waifu2x-caffe:** lltcggie, MIT
* **waifu2x-ncnn-vulkan:** nihui, MIT
* **Real-CUGAN:** Bilibili AI Lab, MIT; ncnn port by nihui, MIT
* **Real-ESRGAN models** (x4plus, x2plus, x4plus anime, animevideo v3, RealESRNet, General): Xintao Wang / Tencent ARC, BSD-3; Real-ESRGAN-ncnn-vulkan engine code, MIT
* **NMKD Siax / Superscale:** Nmkd, WTFPL (fine-tuned from ESRGAN by Xintao Wang, Apache-2.0)
* **Nomos8kSC, LSDIR Compact:** Helaman (Philip Hofmann), CC-BY-4.0 (converted to ncnn; LSDIR Compact levels 1-2 are weight blends)
* **ESRGAN** (RRDB_PSNR_x4, used for the NMKD Siax detail levels): Xintao Wang, Apache-2.0
* **Libraries:** ncnn (Tencent, BSD-3), glslang, libwebp (BSD-3), OpenJPEG (BSD-2), zlib-ng (zlib), Dear ImGui (MIT), GLFW (zlib), stb (MIT / public domain), Noto Sans fonts (SIL OFL 1.1), Roboto (Apache-2.0)
* **Languages:** the translations start from the waifu2x-caffe language files (MIT) and were completed for RePicturer

See `THIRD_PARTY_LICENSES.md`.
