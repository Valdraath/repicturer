# RePicturer command line guide (Linux)

`repicturer-cli` is the converter behind the RePicturer window. You can also run it yourself, in a script or a batch
file, to convert pictures without opening the program. It does everything the window does: the AI models, Non-AI Scale,
every option of Preferences, folders and file lists. This guide is for **Linux**; the Windows one is
[English-Windows.md](English-Windows.md).

## Contents

1. [Getting started](#1-getting-started)
2. [The four modes](#2-the-four-modes)
3. [Examples](#3-examples)
4. [All options](#4-all-options)
5. [Which options each mode uses](#5-which-options-each-mode-uses)
6. [Folders, file lists and scripts](#6-folders-file-lists-and-scripts)
7. [Exit codes and messages](#7-exit-codes-and-messages)
8. [Older option names](#8-older-option-names)
9. [Troubleshooting](#9-troubleshooting)

## 1. Getting started

1. Unzip `RePicturer-linux.zip` and open a terminal in the folder:

   ```
   cd ~/RePicturer-linux
   ```

2. Make the converter executable once (some downloads drop the permission):

   ```
   chmod +x repicturer-cli
   ```

3. Run a first conversion:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. See every option:

   ```
   ./repicturer-cli -h
   ```

Without other options this is **AI Denoise & Scale**, 2x, denoise level 0, model `models-cunet`, on the first GPU.

**Paths with spaces** need quotes: `-i "$HOME/My Pictures/cat.png"`. A model can be given by its folder name only
(`-m models-realcugan-se`): it is found next to `repicturer-cli`, whatever folder the terminal is in.

**Run it from anywhere:** add the folder to your PATH (for example in `~/.bashrc`:
`export PATH="$HOME/RePicturer-linux:$PATH"`) and call `repicturer-cli` without `./`.

The GPU is used through **Vulkan**: install `libvulkan1` and the Mesa Vulkan drivers or the NVIDIA driver (see the
README). Without them the converter runs on the CPU, much slower.

## 2. The four modes

| Mode in the window | `-M` value | What it does |
|---|---|---|
| AI Denoise & Scale | `denoise-scale` | removes noise / JPEG artifacts and enlarges, with an AI model |
| AI Scale Only | `scale` | enlarges with an AI model, no denoise |
| AI Denoise Only | `denoise` | removes noise, keeps the size |
| Non-AI Scale | `non-ai-scale` | resizes with a classic filter (Lanczos, Bicubic, Jinc ...), no model |

Without `-M` the mode follows the other options like in waifu2x-ncnn-vulkan: `-n -1` means scale only, `-s 1` means
denoise only, anything else is denoise & scale.

## 3. Examples

**Anime picture, 2x, Real-CUGAN SE with a light denoise:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**Photo to exactly 3840 pixels wide (the height follows), NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**Exactly 1920 x 1080 (stretched to that size, like waifu2x-caffe):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**Only remove JPEG artifacts, keep the size:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25x with a sharper post-conversion resize (Lanczos 4-tap with anti-ringing):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**Pixel art, 4x, no AI (sharp pixels):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**Photo, Non-AI 1.5x with Lanczos 3-tap and anti-ringing:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**Shrink a picture to half size, in linear light:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**A banded game background (sky with steps), AI 2x, with Color Debanding before AI Conversion:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**Shrink a photo with smooth gradients: linear light and error diffusion dithering:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**A whole folder, sub folders too, to WebP, skipping pictures already done:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**On the CPU (no GPU), with 8 cores:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. All options

### Input and output (every mode)

| Option | Meaning |
|---|---|
| `-i`, `--input path` | input picture or folder |
| `-o`, `--output path` | output picture or folder |
| `-r`, `--recursive` | also convert sub folders, keeping the folder structure |
| `-l`, `--extensions list` | extensions to read from a folder, e.g. `png:jpg:jpeg:webp` (default: every file in it; set it when the folder holds other files too) |
| `-L`, `--file-list file` | a UTF-8 text file, one `input<TAB>output` pair per line (see section 6) |
| `-k`, `--no-overwrite` | skip pictures whose output file already exists (it is kept); the window's *Don't overwrite existing file(s)* |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (default: the output file's extension, else png). Animated GIF / WebP stay animated |
| `-q`, `--quality q` | jpg 0-100 (default 100), webp 1-100 or 101 = lossless (default 101), tga 0 / 1 = RLE off / on |

### Mode and size (every mode)

| Option | Meaning |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`, `scale`, `denoise` or `non-ai-scale` (section 2) |
| `-s`, `--scale r` | scale rate, any positive number: `2`, `1.5`, `0.5` (default 2) |
| `-W`, `--width w` | output width; the height follows the aspect ratio unless `-H` is set |
| `-H`, `--height h` | output height; the width follows the aspect ratio unless `-W` is set |
| `-W` and `-H` together | exactly that width x height |

### AI modes (`denoise-scale`, `scale`, `denoise`)

| Option | Meaning |
|---|---|
| `-m`, `--model path` | model folder (default `models-cunet`). The engine follows the folder: `models-cunet`, `models-upconv_7_*`, `models-upresnet10` are waifu2x; `models-realcugan-se / -pro / -nose` are Real-CUGAN; `models-realesrgan-*`, `models-realesr-*`, `models-realesrnet-x4plus`, `models-nomos8ksc`, `models-lsdir-compact`, `models-nmkd-siax / -superscale` are Real-ESRGAN |
| `-n`, `--noise-level n` | denoise level `-1` (none), `0`-`3` (default 0). Real-CUGAN: 0 = conservative. Real-ESRGAN General: denoise strength; LSDIR Compact: cleanup level; NMKD Siax: the detail level 0-3; the other Real-ESRGAN models clean up by themselves |
| `-t`, `--split-size t` | `0` = automatic (default), `1` = no split (the whole picture as 1 tile), `2`-`512` = tiles of that size; one value per GPU with several GPUs (`0,0`) |
| `-c`, `--sync-gap n` | Real-CUGAN sync gap `0`-`3` (default 3, 0 = off) |

Before AI Conversion, `--deband` can smooth the banding of the source (the window's Preferences > Pre-AI Conversion Settings).
After the AI, the picture is shrunk to the exact output size (for example 2.25x, or a width x height) with the
**post-conversion** filter: `-F`, `-T`, `-a`, `--dither` and `--linear-light` below (the window's Preferences >
Post-AI Conversion Settings). The default is Bicubic Mitchell, no anti-ringing, no dithering, no linear light. An exact
2x / 3x / 4x of the model is saved as it is.

### Non-AI filter, debanding and dithering (`non-ai-scale`, and around the AI in the AI modes)

| Option | Meaning |
|---|---|
| `-F`, `--filter name` | `nearest`, `bilinear`, `mitchell` (default), `catrom`, `lanczos`, `spline36`, `jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (default) / `4` / `8`; Jinc `3` (default) / `4` / `8`; the other filters ignore it |
| `-a`, `--anti-ringing a` | `off` (default), `relaxed`, `strict`: reduces the halos of `catrom`, `lanczos`, `spline36` and `jinc` |
| `--linear-light` | scale in linear light; used only when the picture gets smaller (no side bigger, at least one smaller) |
| `--dither d` | `off` (default), `ordered`, `error-diffusion`: how the result is rounded to 8-bit colour. Dithering keeps smooth gradients free of new bands; used when the picture is resized |
| `--deband d` | `off` (default), `low`, `medium`, `high`: **Color Debanding**, smooths away banding already in the picture (skies, shadows, soft gradients) before AI Conversion or the Non-AI filter. Only flat areas change |

| Filter | Character |
|---|---|
| `nearest` | Nearest Neighbor: hard, blocky pixels, no blur. Pixel art at 2x / 3x / 4x |
| `bilinear` | fast and soft, a little blurry, no halos |
| `mitchell` | Bicubic Mitchell: soft and smooth, almost no halos. A safe choice |
| `catrom` | Bicubic Catmull-Rom: sharper than Mitchell, slight halos |
| `lanczos` | sharp, keeps fine detail, some halos (more taps = sharper, more halos) |
| `spline36` | between Catmull-Rom and Lanczos: sharp, clean edges, few halos |
| `jinc` | a round filter: sharp, smooth diagonal edges, few jaggies; quite slow |

### Performance (every mode)

| Option | Meaning |
|---|---|
| `-g`, `--gpu id` | the GPU to use: `0`, `1` ... or `-1` for the CPU (default: the first GPU). Several GPUs: `0,1` |
| `-j`, `--threads l:p:s` | threads for load : process : save (default `1:2:2`). Process = pictures at the same time on each GPU (1-20), or the CPU cores with `-g -1`. Two GPUs: `1:2,2:2` |
| `--vram-percent p` | use at most p % (20-80, default 60) of the GPU memory that is free when the conversion starts |
| `--ram-percent p` | use at most p % (20-80, default 60) of the RAM that is free when the conversion starts |

### Other

| Option | Meaning |
|---|---|
| `-h`, `--help` | show the help |
| `-v`, `--verbose` | print every step (the networks that ran, the memory, the device) |
| `--list-gpu` | list the Vulkan GPUs and exit |
| `--progress` | machine readable progress lines (used by the window) |

## 5. Which options each mode uses

| Setting in the window | AI modes | Non-AI Scale | Option |
|---|---|---|---|
| Input / Output path, custom folders | ✓ | ✓ | `-i` `-o` (`-r`, `-l`, `-L`) |
| Output extension / quality | ✓ | ✓ | `-f` `-q` |
| Mode | ✓ | ✓ | `-M` |
| Denoise level / Detail level | ✓ | – | `-n` |
| Scale: Rate, Width, Height, Width x Height | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| Model | ✓ | – | `-m` |
| Filter, Taps, Anti-Ringing (main window) | – | ✓ | `-F` `-T` `-a` |
| Processing Unit | ✓ | ✓ | `-g` |
| Split size | ✓ | – | `-t` |
| Thread(s) | ✓ | ✓ | `-j` |
| Preferences: Real-CUGAN sync gap | ✓ | – | `-c` |
| Preferences: Pre-AI Conversion Settings (Color Debanding) | ✓ | – | `--deband` |
| Preferences: Post-AI Conversion Settings | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| Preferences: Non-AI Filters Settings | – | ✓ | `--deband` `--dither` `--linear-light` |
| Preferences: Don't overwrite existing file(s) | ✓ | ✓ | `-k` |
| Preferences: VRAM / RAM limit | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`, `-T`, `-a`, `--dither`, `--deband` and `--linear-light` mean the Non-AI settings in `non-ai-scale` and the
pre- / post-conversion settings in the AI modes: one run uses one mode, so one set of options is enough.

## 6. Folders, file lists and scripts

**A folder:** `-i` is a folder and `-o` the folder to write to. Every picture gets the same name with the output format's
extension (`-f`, default png). With `-r` the sub folders are converted too, into the same tree.

**A file list** (`-L`) converts any pictures to any names. Each line is `input`, a TAB, then `output`:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

A list can be made with the shell, for example every PNG below a folder:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**A shell script** (`convert.sh`) that converts every picture given to it, next to the original:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

Save it in the RePicturer folder, `chmod +x convert.sh`, then `./convert.sh ~/Pictures/*.jpg`.

**Every JPG of a folder through Non-AI Scale:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(For many pictures one call with a folder is faster: the model is loaded only once.)

## 7. Exit codes and messages

| Exit code (`$?`) | Meaning |
|---|---|
| `0` | every picture was converted |
| `1` | some pictures failed (each one is listed) |
| `255` | the conversion did not start: a wrong option, a missing model or input |

A picture that fails on the GPU (out of memory, a driver problem) is retried up to 2 times, first in 2 tiles, then as 1
tile; if it still fails the conversion stops with a message. In Non-AI Scale a picture the GPU cannot resize is resized
on the CPU instead, with the same result.

## 8. Older option names

The names of waifu2x-ncnn-vulkan and earlier RePicturer versions still work, so old scripts keep running:

| Older name | Name since v3.0 |
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

## 9. Troubleshooting

* **"Permission denied"**: run `chmod +x repicturer-cli` in the folder.
* **"command not found"**: write `./repicturer-cli` (with `./`) or add the folder to your PATH (section 1).
* **"no vulkan device found, falling back to cpu"**: install the Vulkan driver (`sudo apt install libvulkan1
  mesa-vulkan-drivers`, or the NVIDIA driver). `--list-gpu` shows the GPUs the converter sees; `vulkaninfo --summary`
  (package `vulkan-tools`) shows what the system offers.
* **"GPU error (probably out of GPU memory)"**: use a smaller split size (`-t 200`, `-t 100`), fewer threads (`-j 1:1:2`)
  or raise `--vram-percent`.
* **A file name with a TAB or a line break** cannot go into a file list (`-L`); convert it with `-i` / `-o` instead.
