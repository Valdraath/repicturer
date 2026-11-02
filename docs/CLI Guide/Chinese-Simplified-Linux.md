# RePicturer 命令行使用指南 (Linux)

`repicturer-cli` 是 RePicturer 窗口背后的转换程序。你也可以自己运行它,或在脚本中调用,无需打开程序就能
转换图片。窗口能做的它都能做:AI 模型、“缩放 - 非 AI”、首选项里的所有选项、文件夹和文件列表。本指南适用于 **Linux**;
Windows 版请看 [Chinese-Simplified-Windows.md](Chinese-Simplified-Windows.md)。

## 目录

1. 入门
2. 四种模式
3. 示例
4. 全部选项
5. 各模式使用哪些选项
6. 文件夹、文件列表和脚本
7. 退出代码和信息
8. 旧的选项名称
9. 疑难解答

## 1. 入门

1. 解压 `RePicturer-linux.zip`,在该文件夹中打开终端:

   ```
   cd ~/RePicturer-linux
   ```

2. 给转换程序加上执行权限 (只需一次;有些下载方式会丢失这个权限):

   ```
   chmod +x repicturer-cli
   ```

3. 运行第一次转换:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. 查看所有选项:

   ```
   ./repicturer-cli -h
   ```

不加其他选项时,使用的是 **降噪和放大 - AI**、2 倍、降噪等级 0、模型 `models-cunet`,在第一块 GPU 上运行。

**含空格的路径**需要加引号:`-i "$HOME/My Pictures/cat.png"`。模型可以只写文件夹名 (`-m models-realcugan-se`):
无论终端当前在哪个文件夹,程序都会在 `repicturer-cli` 旁边找到它。

**在任何位置运行:** 把该文件夹加入 PATH (例如在 `~/.bashrc` 中写 `export PATH="$HOME/RePicturer-linux:$PATH"`),
之后不用 `./` 直接调用 `repicturer-cli`。

GPU 通过 **Vulkan** 使用:请安装 `libvulkan1` 和 Mesa 的 Vulkan 驱动或 NVIDIA 驱动 (见 README)。没有它们时转换程序会在 CPU 上运行,慢很多。

## 2. 四种模式

| 窗口中的模式 | `-M` 的值 | 作用 |
|---|---|---|
| 降噪和放大 - AI | `denoise-scale` | 用 AI 模型去除噪点 / JPEG 压缩痕迹并放大 |
| 放大 - AI | `scale` | 用 AI 模型放大,不降噪 |
| 降噪 - AI | `denoise` | 去除噪点,尺寸不变 |
| 缩放 - 非 AI | `non-ai-scale` | 用经典滤镜 (Lanczos、Bicubic、Jinc 等) 调整大小,不用模型 |

不指定 `-M` 时,模式由其他选项决定,与 waifu2x-ncnn-vulkan 相同:`-n -1` 表示只放大,`-s 1` 表示只降噪,其他情况为降噪并放大。

## 3. 示例

**动漫图片,2 倍,Real-CUGAN SE 轻度降噪:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**照片宽度正好 3840 像素 (高度按比例),NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**正好 1920 x 1080 (拉伸到该尺寸,与 waifu2x-caffe 相同):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**只去除 JPEG 压缩痕迹,尺寸不变:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25 倍,转换后用更锐利的缩小 (Lanczos 4 抽头加抗振铃):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**像素画,4 倍,不用 AI (像素清晰):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**照片,非 AI 放大 1.5 倍,Lanczos 3 抽头加抗振铃:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**在线性光下把图片缩小一半:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**有色带的游戏背景 (天空出现阶梯),AI 2 倍,在 AI 前去色带:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**缩小带有平滑渐变的照片:线性光加误差扩散抖动:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**整个文件夹 (含子文件夹) 转为 WebP,跳过已处理的图片:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**在 CPU 上运行 (没有 GPU),使用 8 个核心:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. 全部选项

### 输入和输出 (所有模式)

| 选项 | 含义 |
|---|---|
| `-i`, `--input path` | 输入图片或文件夹 |
| `-o`, `--output path` | 输出图片或文件夹 |
| `-r`, `--recursive` | 同时转换子文件夹,保持文件夹结构 |
| `-l`, `--extensions list` | 从文件夹中读取的扩展名,例如 `png:jpg:jpeg:webp` (默认:文件夹中的所有文件;文件夹里还有其他文件时请设置) |
| `-L`, `--file-list file` | UTF-8 文本文件,每行一对 `输入<TAB>输出` (见第 6 节) |
| `-k`, `--no-overwrite` | 跳过已有输出文件的图片 (保留原文件);对应窗口中的“不覆盖已有文件” |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (默认:输出文件的扩展名,否则为 png)。动态 GIF / WebP 保持动态 |
| `-q`, `--quality q` | jpg 0-100 (默认 100),webp 1-100 或 101 = 无损 (默认 101),tga 0 / 1 = 关闭 / 开启 RLE |

### 模式和尺寸 (所有模式)

| 选项 | 含义 |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`、`scale`、`denoise` 或 `non-ai-scale` (见第 2 节) |
| `-s`, `--scale r` | 倍率,任意正数:`2`、`1.5`、`0.5` (默认 2) |
| `-W`, `--width w` | 输出宽度;不指定 `-H` 时高度按比例 |
| `-H`, `--height h` | 输出高度;不指定 `-W` 时宽度按比例 |
| 同时使用 `-W` 和 `-H` | 正好是该宽度 x 高度 |

### AI 模式 (`denoise-scale`、`scale`、`denoise`)

| 选项 | 含义 |
|---|---|
| `-m`, `--model path` | 模型文件夹 (默认 `models-cunet`)。引擎由文件夹决定:`models-cunet`、`models-upconv_7_*`、`models-upresnet10` 为 waifu2x;`models-realcugan-se / -pro / -nose` 为 Real-CUGAN;`models-realesrgan-*`、`models-realesr-*`、`models-realesrnet-x4plus`、`models-nomos8ksc`、`models-lsdir-compact`、`models-nmkd-siax / -superscale` 为 Real-ESRGAN |
| `-n`, `--noise-level n` | 降噪等级 `-1` (不降噪)、`0`-`3` (默认 0)。Real-CUGAN:0 = 保守。Real-ESRGAN General:降噪强度;LSDIR Compact:清理等级;NMKD Siax:细节等级 0-3;其他 Real-ESRGAN 模型会自行清理 |
| `-t`, `--split-size t` | `0` = 自动 (默认),`1` = 不分块 (整张图片作为 1 块),`2`-`512` = 该大小的分块;多块 GPU 时每块一个值 (`0,0`) |
| `-c`, `--sync-gap n` | Real-CUGAN 的 Sync gap `0`-`3` (默认 3,0 = 关闭) |

在 AI 之前,`--deband` 可以平滑原图中的色带 (窗口中的“首选项 > AI 转换前设置”)。AI 之后,图片会用**转换后**的滤镜缩小到
精确的输出尺寸 (例如 2.25 倍,或宽 x 高):即下面的 `-F`、`-T`、`-a`、`--dither` 和 `--linear-light` (窗口中的
“首选项 > AI 转换后设置”)。默认为 Bicubic Mitchell,不抗振铃,不抖动,不使用线性光。模型正好输出的 2x / 3x / 4x 会原样保存。

### 非 AI 滤镜、去色带和抖动 (`non-ai-scale`,以及 AI 模式中 AI 前后的步骤)

| 选项 | 含义 |
|---|---|
| `-F`, `--filter name` | `nearest`、`bilinear`、`mitchell` (默认)、`catrom`、`lanczos`、`spline36`、`jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (默认) / `4` / `8`;Jinc `3` (默认) / `4` / `8`;其他滤镜忽略此项 |
| `-a`, `--anti-ringing a` | `off` (默认)、`relaxed`、`strict`:减少 `catrom`、`lanczos`、`spline36` 和 `jinc` 的光晕 |
| `--linear-light` | 在线性光下缩放;只在图片变小时使用 (没有一边变大,至少一边变小) |
| `--dither d` | `off` (默认)、`ordered`、`error-diffusion`:结果如何舍入为 8 位色。抖动让平滑渐变不出现新的色带;在图片被缩放时使用 |
| `--deband d` | `off` (默认)、`low`、`medium`、`high`:**去色带**,在 AI 或非 AI 滤镜之前平滑图片中已有的色带 (天空、阴影、柔和渐变)。只改变平坦区域 |

| 滤镜 | 特点 |
|---|---|
| `nearest` | 最近邻:像素硬朗、呈块状,不模糊。适合 2x / 3x / 4x 的像素画 |
| `bilinear` | 快速柔和,稍有模糊,无光晕 |
| `mitchell` | Bicubic Mitchell:柔和平滑,几乎无光晕。稳妥的选择 |
| `catrom` | Bicubic Catmull-Rom:比 Mitchell 更锐利,有轻微光晕 |
| `lanczos` | 锐利,保留细节,有些光晕 (抽头越多越锐利,光晕越多) |
| `spline36` | 介于 Catmull-Rom 和 Lanczos 之间:锐利,边缘干净,光晕少 |
| `jinc` | 圆形滤镜:锐利,斜边平滑,锯齿少;相当慢 |

### 性能 (所有模式)

| 选项 | 含义 |
|---|---|
| `-g`, `--gpu id` | 使用的 GPU:`0`、`1`... 或 `-1` 表示 CPU (默认:第一块 GPU)。多块 GPU:`0,1` |
| `-j`, `--threads l:p:s` | 读取 : 处理 : 保存 的线程数 (默认 `1:2:2`)。处理 = 每块 GPU 同时处理的图片数 (1-20),使用 `-g -1` 时为 CPU 核心数。两块 GPU:`1:2,2:2` |
| `--vram-percent p` | 最多使用转换开始时空闲 GPU 显存的 p % (20-80,默认 60) |
| `--ram-percent p` | 最多使用转换开始时空闲内存的 p % (20-80,默认 60) |

### 其他

| 选项 | 含义 |
|---|---|
| `-h`, `--help` | 显示帮助 |
| `-v`, `--verbose` | 显示每一步 (运行的网络、内存、设备) |
| `--list-gpu` | 列出 Vulkan GPU 后退出 |
| `--progress` | 输出机器可读的进度 (窗口使用) |

## 5. 各模式使用哪些选项

| 窗口中的设置 | AI 模式 | 缩放 - 非 AI | 选项 |
|---|---|---|---|
| 输入 / 输出路径、自定义文件夹 | ✓ | ✓ | `-i` `-o` (`-r`、`-l`、`-L`) |
| 输出扩展名 / 输出质量 | ✓ | ✓ | `-f` `-q` |
| 模式 | ✓ | ✓ | `-M` |
| 降噪等级 / 细节等级 | ✓ | – | `-n` |
| 放大:倍率、宽度、高度、宽度x高度 | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| 模型 | ✓ | – | `-m` |
| 滤镜、抽头数、抗振铃 (主窗口) | – | ✓ | `-F` `-T` `-a` |
| 处理单元 | ✓ | ✓ | `-g` |
| 分块大小 | ✓ | – | `-t` |
| 线程数 | ✓ | ✓ | `-j` |
| 首选项:Real-CUGAN 的 Sync gap | ✓ | – | `-c` |
| 首选项:AI 转换前设置 (去色带) | ✓ | – | `--deband` |
| 首选项:AI 转换后设置 | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| 首选项:非 AI 滤镜设置 | – | ✓ | `--deband` `--dither` `--linear-light` |
| 首选项:不覆盖已有文件 | ✓ | ✓ | `-k` |
| 首选项:显存 / 内存上限 | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`、`-T`、`-a`、`--dither`、`--deband` 和 `--linear-light` 在 `non-ai-scale` 中是非 AI 设置,在 AI 模式中是转换前 / 转换后
设置:每次运行只用一种模式,所以一组选项就够了。

## 6. 文件夹、文件列表和脚本

**文件夹:** `-i` 是文件夹,`-o` 是写入的文件夹。每张图片保持原名,扩展名改为输出格式 (`-f`,默认 png)。加 `-r` 时子文件夹
也会转换,并保持相同结构。

**文件列表** (`-L`) 可以把任意图片转换成任意名称。每行是 `输入`、一个 TAB,然后是 `输出`:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

可以用 shell 生成列表,例如某个文件夹下的所有 PNG:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**Shell 脚本** (`convert.sh`),转换传给它的每张图片,结果保存在原图旁边:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

把它保存在 RePicturer 文件夹中,执行 `chmod +x convert.sh`,然后运行 `./convert.sh ~/Pictures/*.jpg`。

**把一个文件夹中的所有 JPG 用“缩放 - 非 AI”处理:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(图片很多时,用一次调用处理整个文件夹更快:模型只加载一次。)

## 7. 退出代码和信息

| 退出代码 (`$?`) | 含义 |
|---|---|
| `0` | 所有图片都已转换 |
| `1` | 部分图片失败 (每张都会列出) |
| `255` | 转换没有开始:选项错误、缺少模型或输入 |

在 GPU 上失败的图片 (显存不足、驱动问题) 最多重试 2 次,先分成 2 块,再作为 1 块;仍然失败时转换会停止并显示信息。
在“缩放 - 非 AI”中,GPU 处理不了的图片会改用 CPU 处理,结果相同。

## 8. 旧的选项名称

waifu2x-ncnn-vulkan 和旧版 RePicturer 的选项名仍然可用,旧脚本可以继续运行:

| 旧名称 | v3.0 起的名称 |
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

## 9. 疑难解答

* **“Permission denied”**:在该文件夹中运行 `chmod +x repicturer-cli`。
* **“command not found”**:写成 `./repicturer-cli` (带 `./`),或把文件夹加入 PATH (见第 1 节)。
* **“no vulkan device found, falling back to cpu”**:安装 Vulkan 驱动 (`sudo apt install libvulkan1 mesa-vulkan-drivers`,
  或 NVIDIA 驱动)。`--list-gpu` 会显示转换程序能看到的 GPU;`vulkaninfo --summary` (`vulkan-tools` 软件包) 显示系统提供的内容。
* **“GPU error (probably out of GPU memory)”**:减小分块大小 (`-t 200`、`-t 100`)、减少线程 (`-j 1:1:2`) 或提高 `--vram-percent`。
* **文件名含 TAB 或换行**时不能写进文件列表 (`-L`);请改用 `-i` / `-o` 转换该文件。
