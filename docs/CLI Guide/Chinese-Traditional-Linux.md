# RePicturer 命令列使用指南 (Linux)

`repicturer-cli` 是 RePicturer 視窗背後的轉換程式。你也可以自己執行它,或在指令碼中呼叫,不必開啟程式就能
轉換圖片。視窗能做的它都能做:AI 模型、「縮放 - 非 AI」、偏好設定裡的所有選項、資料夾和檔案清單。本指南適用於
**Linux**;Windows 版請看 [Chinese-Traditional-Windows.md](Chinese-Traditional-Windows.md)。

## 目錄

1. 入門
2. 四種模式
3. 範例
4. 全部選項
5. 各模式使用哪些選項
6. 資料夾、檔案清單和指令碼
7. 結束代碼和訊息
8. 舊的選項名稱
9. 疑難排解

## 1. 入門

1. 解壓縮 `RePicturer-linux.zip`,在該資料夾中開啟終端機:

   ```
   cd ~/RePicturer-linux
   ```

2. 為轉換程式加上執行權限 (只需一次;有些下載方式會遺失這個權限):

   ```
   chmod +x repicturer-cli
   ```

3. 執行第一次轉換:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. 查看所有選項:

   ```
   ./repicturer-cli -h
   ```

不加其他選項時,使用的是 **降噪和放大 - AI**、2 倍、降噪等級 0、模型 `models-cunet`,在第一張 GPU 上執行。

**含空格的路徑**需要加引號:`-i "$HOME/My Pictures/cat.png"`。模型可以只寫資料夾名稱 (`-m models-realcugan-se`):
不論終端機目前在哪個資料夾,程式都會在 `repicturer-cli` 旁邊找到它。

**在任何位置執行:** 把該資料夾加入 PATH (例如在 `~/.bashrc` 中寫 `export PATH="$HOME/RePicturer-linux:$PATH"`),
之後不用 `./` 直接呼叫 `repicturer-cli`。

GPU 透過 **Vulkan** 使用:請安裝 `libvulkan1` 和 Mesa 的 Vulkan 驅動程式或 NVIDIA 驅動程式 (見 README)。沒有它們時轉換程式會在 CPU 上執行,慢很多。

## 2. 四種模式

| 視窗中的模式 | `-M` 的值 | 作用 |
|---|---|---|
| 降噪和放大 - AI | `denoise-scale` | 用 AI 模型去除雜訊 / JPEG 壓縮痕跡並放大 |
| 放大 - AI | `scale` | 用 AI 模型放大,不降噪 |
| 降噪 - AI | `denoise` | 去除雜訊,尺寸不變 |
| 縮放 - 非 AI | `non-ai-scale` | 用經典濾鏡 (Lanczos、Bicubic、Jinc 等) 調整大小,不用模型 |

不指定 `-M` 時,模式由其他選項決定,與 waifu2x-ncnn-vulkan 相同:`-n -1` 表示只放大,`-s 1` 表示只降噪,其他情況為降噪並放大。

## 3. 範例

**動漫圖片,2 倍,Real-CUGAN SE 輕度降噪:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**照片寬度正好 3840 像素 (高度依比例),NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**正好 1920 x 1080 (拉伸到該尺寸,與 waifu2x-caffe 相同):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**只去除 JPEG 壓縮痕跡,尺寸不變:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25 倍,轉換後用更銳利的縮小 (Lanczos 4 抽頭加抗振鈴):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**像素畫,4 倍,不用 AI (像素清晰):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**照片,非 AI 放大 1.5 倍,Lanczos 3 抽頭加抗振鈴:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**在線性光下把圖片縮小一半:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**有色帶的遊戲背景 (天空出現階梯),AI 2 倍,在 AI 前去色帶:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**縮小帶有平滑漸層的照片:線性光加誤差擴散抖動:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**整個資料夾 (含子資料夾) 轉為 WebP,略過已處理的圖片:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**在 CPU 上執行 (沒有 GPU),使用 8 個核心:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. 全部選項

### 輸入和輸出 (所有模式)

| 選項 | 意義 |
|---|---|
| `-i`, `--input path` | 輸入圖片或資料夾 |
| `-o`, `--output path` | 輸出圖片或資料夾 |
| `-r`, `--recursive` | 一併轉換子資料夾,保持資料夾結構 |
| `-l`, `--extensions list` | 從資料夾讀取的副檔名,例如 `png:jpg:jpeg:webp` (預設:資料夾中的所有檔案;資料夾裡還有其他檔案時請設定) |
| `-L`, `--file-list file` | UTF-8 文字檔,每行一組 `輸入<TAB>輸出` (見第 6 節) |
| `-k`, `--no-overwrite` | 略過已有輸出檔案的圖片 (保留原檔);對應視窗中的「不覆寫已有檔案」 |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (預設:輸出檔案的副檔名,否則為 png)。動態 GIF / WebP 保持動態 |
| `-q`, `--quality q` | jpg 0-100 (預設 100),webp 1-100 或 101 = 無損 (預設 101),tga 0 / 1 = 關閉 / 開啟 RLE |

### 模式和尺寸 (所有模式)

| 選項 | 意義 |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`、`scale`、`denoise` 或 `non-ai-scale` (見第 2 節) |
| `-s`, `--scale r` | 倍率,任意正數:`2`、`1.5`、`0.5` (預設 2) |
| `-W`, `--width w` | 輸出寬度;不指定 `-H` 時高度依比例 |
| `-H`, `--height h` | 輸出高度;不指定 `-W` 時寬度依比例 |
| 同時使用 `-W` 和 `-H` | 正好是該寬度 x 高度 |

### AI 模式 (`denoise-scale`、`scale`、`denoise`)

| 選項 | 意義 |
|---|---|
| `-m`, `--model path` | 模型資料夾 (預設 `models-cunet`)。引擎由資料夾決定:`models-cunet`、`models-upconv_7_*`、`models-upresnet10` 為 waifu2x;`models-realcugan-se / -pro / -nose` 為 Real-CUGAN;`models-realesrgan-*`、`models-realesr-*`、`models-realesrnet-x4plus`、`models-nomos8ksc`、`models-lsdir-compact`、`models-nmkd-siax / -superscale` 為 Real-ESRGAN |
| `-n`, `--noise-level n` | 降噪等級 `-1` (不降噪)、`0`-`3` (預設 0)。Real-CUGAN:0 = 保守。Real-ESRGAN General:降噪強度;LSDIR Compact:清理等級;NMKD Siax:細節等級 0-3;其他 Real-ESRGAN 模型會自行清理 |
| `-t`, `--split-size t` | `0` = 自動 (預設),`1` = 不分割 (整張圖片作為 1 塊),`2`-`512` = 該大小的分割;多張 GPU 時每張一個值 (`0,0`) |
| `-c`, `--sync-gap n` | Real-CUGAN 的 Sync gap `0`-`3` (預設 3,0 = 關閉) |

在 AI 之前,`--deband` 可以平滑原圖中的色帶 (視窗中的「偏好設定 > AI 轉換前設定」)。AI 之後,圖片會用**轉換後**的濾鏡縮小到
精確的輸出尺寸 (例如 2.25 倍,或寬 x 高):即下面的 `-F`、`-T`、`-a`、`--dither` 和 `--linear-light` (視窗中的
「偏好設定 > AI 轉換後設定」)。預設為 Bicubic Mitchell,不抗振鈴,不抖動,不使用線性光。模型正好輸出的 2x / 3x / 4x 會原樣儲存。

### 非 AI 濾鏡、去色帶和抖動 (`non-ai-scale`,以及 AI 模式中 AI 前後的步驟)

| 選項 | 意義 |
|---|---|
| `-F`, `--filter name` | `nearest`、`bilinear`、`mitchell` (預設)、`catrom`、`lanczos`、`spline36`、`jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (預設) / `4` / `8`;Jinc `3` (預設) / `4` / `8`;其他濾鏡忽略此項 |
| `-a`, `--anti-ringing a` | `off` (預設)、`relaxed`、`strict`:減少 `catrom`、`lanczos`、`spline36` 和 `jinc` 的光暈 |
| `--linear-light` | 在線性光下縮放;只在圖片變小時使用 (沒有一邊變大,至少一邊變小) |
| `--dither d` | `off` (預設)、`ordered`、`error-diffusion`:結果如何捨入為 8 位元色。抖動讓平滑漸層不出現新的色帶;在圖片被縮放時使用 |
| `--deband d` | `off` (預設)、`low`、`medium`、`high`:**去色帶**,在 AI 或非 AI 濾鏡之前平滑圖片中已有的色帶 (天空、陰影、柔和漸層)。只改變平坦區域 |

| 濾鏡 | 特點 |
|---|---|
| `nearest` | 最近鄰:像素硬朗、呈塊狀,不模糊。適合 2x / 3x / 4x 的像素畫 |
| `bilinear` | 快速柔和,稍有模糊,無光暈 |
| `mitchell` | Bicubic Mitchell:柔和平滑,幾乎無光暈。穩妥的選擇 |
| `catrom` | Bicubic Catmull-Rom:比 Mitchell 更銳利,有輕微光暈 |
| `lanczos` | 銳利,保留細節,有些光暈 (抽頭越多越銳利,光暈越多) |
| `spline36` | 介於 Catmull-Rom 和 Lanczos 之間:銳利,邊緣乾淨,光暈少 |
| `jinc` | 圓形濾鏡:銳利,斜邊平滑,鋸齒少;相當慢 |

### 效能 (所有模式)

| 選項 | 意義 |
|---|---|
| `-g`, `--gpu id` | 使用的 GPU:`0`、`1`... 或 `-1` 表示 CPU (預設:第一張 GPU)。多張 GPU:`0,1` |
| `-j`, `--threads l:p:s` | 讀取 : 處理 : 儲存 的執行緒數 (預設 `1:2:2`)。處理 = 每張 GPU 同時處理的圖片數 (1-20),使用 `-g -1` 時為 CPU 核心數。兩張 GPU:`1:2,2:2` |
| `--vram-percent p` | 最多使用轉換開始時可用顯示記憶體的 p % (20-80,預設 60) |
| `--ram-percent p` | 最多使用轉換開始時可用記憶體的 p % (20-80,預設 60) |

### 其他

| 選項 | 意義 |
|---|---|
| `-h`, `--help` | 顯示說明 |
| `-v`, `--verbose` | 顯示每一步 (執行的網路、記憶體、裝置) |
| `--list-gpu` | 列出 Vulkan GPU 後結束 |
| `--progress` | 輸出機器可讀的進度 (視窗使用) |

## 5. 各模式使用哪些選項

| 視窗中的設定 | AI 模式 | 縮放 - 非 AI | 選項 |
|---|---|---|---|
| 輸入 / 輸出路徑、自訂資料夾 | ✓ | ✓ | `-i` `-o` (`-r`、`-l`、`-L`) |
| 輸出副檔名 / 輸出品質 | ✓ | ✓ | `-f` `-q` |
| 模式 | ✓ | ✓ | `-M` |
| 降噪等級 / 細節等級 | ✓ | – | `-n` |
| 放大:倍率、寬度、高度、寬度x高度 | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| 模型 | ✓ | – | `-m` |
| 濾鏡、抽頭數、抗振鈴 (主視窗) | – | ✓ | `-F` `-T` `-a` |
| 處理單元 | ✓ | ✓ | `-g` |
| 分割大小 | ✓ | – | `-t` |
| 執行緒數 | ✓ | ✓ | `-j` |
| 偏好設定:Real-CUGAN 的 Sync gap | ✓ | – | `-c` |
| 偏好設定:AI 轉換前設定 (去色帶) | ✓ | – | `--deband` |
| 偏好設定:AI 轉換後設定 | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| 偏好設定:非 AI 濾鏡設定 | – | ✓ | `--deband` `--dither` `--linear-light` |
| 偏好設定:不覆寫已有檔案 | ✓ | ✓ | `-k` |
| 偏好設定:顯示記憶體 / 記憶體上限 | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`、`-T`、`-a`、`--dither`、`--deband` 和 `--linear-light` 在 `non-ai-scale` 中是非 AI 設定,在 AI 模式中是轉換前 / 轉換後
設定:每次執行只用一種模式,所以一組選項就夠了。

## 6. 資料夾、檔案清單和指令碼

**資料夾:** `-i` 是資料夾,`-o` 是寫入的資料夾。每張圖片保持原名,副檔名改為輸出格式 (`-f`,預設 png)。加 `-r` 時子資料夾
也會轉換,並保持相同結構。

**檔案清單** (`-L`) 可以把任意圖片轉換成任意名稱。每行是 `輸入`、一個 TAB,然後是 `輸出`:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

可以用 shell 產生清單,例如某個資料夾下的所有 PNG:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**Shell 指令碼** (`convert.sh`),轉換傳給它的每張圖片,結果儲存在原圖旁邊:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

把它儲存在 RePicturer 資料夾中,執行 `chmod +x convert.sh`,然後執行 `./convert.sh ~/Pictures/*.jpg`。

**把一個資料夾中的所有 JPG 用「縮放 - 非 AI」處理:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(圖片很多時,用一次呼叫處理整個資料夾更快:模型只載入一次。)

## 7. 結束代碼和訊息

| 結束代碼 (`$?`) | 意義 |
|---|---|
| `0` | 所有圖片都已轉換 |
| `1` | 部分圖片失敗 (每張都會列出) |
| `255` | 轉換沒有開始:選項錯誤、缺少模型或輸入 |

在 GPU 上失敗的圖片 (顯示記憶體不足、驅動程式問題) 最多重試 2 次,先分成 2 塊,再作為 1 塊;仍然失敗時轉換會停止並顯示訊息。
在「縮放 - 非 AI」中,GPU 處理不了的圖片會改用 CPU 處理,結果相同。

## 8. 舊的選項名稱

waifu2x-ncnn-vulkan 和舊版 RePicturer 的選項名稱仍然可用,舊指令碼可以繼續執行:

| 舊名稱 | v3.0 起的名稱 |
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

## 9. 疑難排解

* **「Permission denied」**:在該資料夾中執行 `chmod +x repicturer-cli`。
* **「command not found」**:寫成 `./repicturer-cli` (帶 `./`),或把資料夾加入 PATH (見第 1 節)。
* **「no vulkan device found, falling back to cpu」**:安裝 Vulkan 驅動程式 (`sudo apt install libvulkan1 mesa-vulkan-drivers`,
  或 NVIDIA 驅動程式)。`--list-gpu` 會顯示轉換程式看得到的 GPU;`vulkaninfo --summary` (`vulkan-tools` 套件) 顯示系統提供的內容。
* **「GPU error (probably out of GPU memory)」**:減小分割大小 (`-t 200`、`-t 100`)、減少執行緒 (`-j 1:1:2`) 或提高 `--vram-percent`。
* **檔名含 TAB 或換行**時不能寫進檔案清單 (`-L`);請改用 `-i` / `-o` 轉換該檔案。
