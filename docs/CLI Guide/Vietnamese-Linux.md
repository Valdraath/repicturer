# Hướng dẫn dùng RePicturer bằng dòng lệnh (Linux)

`repicturer-cli` là bộ chuyển đổi chạy phía sau cửa sổ RePicturer. Bạn cũng có thể tự chạy nó, trong script, để xử lý ảnh mà không cần mở chương trình. Nó làm được mọi thứ cửa sổ làm được: các model AI, Phóng to - Không AI,
mọi tùy chọn trong Tùy chọn, thư mục và danh sách tệp. Hướng dẫn này dành cho **Linux**; bản cho Windows là
[Vietnamese-Windows.md](Vietnamese-Windows.md).

## Mục lục

1. Bắt đầu
2. Bốn chế độ
3. Ví dụ
4. Mọi tham số
5. Chế độ nào dùng tham số nào
6. Thư mục, danh sách tệp và script
7. Mã thoát và thông báo
8. Tên tham số cũ
9. Khắc phục sự cố

## 1. Bắt đầu

1. Giải nén `RePicturer-linux.zip` và mở cửa sổ dòng lệnh trong thư mục đó:

   ```
   cd ~/RePicturer-linux
   ```

2. Cấp quyền chạy cho bộ chuyển đổi (một lần; một số cách tải về làm mất quyền này):

   ```
   chmod +x repicturer-cli
   ```

3. Chạy thử một lần chuyển đổi:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. Xem mọi tham số:

   ```
   ./repicturer-cli -h
   ```

Nếu không có tham số nào khác, chế độ mặc định là **Khử nhiễu & Phóng to - AI**, 2x, mức khử nhiễu 0, model `models-cunet`,
chạy trên GPU đầu tiên.

**Đường dẫn có khoảng trắng** phải đặt trong ngoặc kép: `-i "$HOME/My Pictures/cat.png"`. Model có thể ghi bằng tên thư mục
(`-m models-realcugan-se`): chương trình sẽ tìm nó cạnh `repicturer-cli`, dù cửa sổ dòng lệnh đang ở thư mục nào.

**Chạy từ bất kỳ đâu:** thêm thư mục vào PATH (ví dụ trong `~/.bashrc`: `export PATH="$HOME/RePicturer-linux:$PATH"`) rồi
gọi `repicturer-cli` không cần `./`.

GPU được dùng qua **Vulkan**: hãy cài `libvulkan1` và driver Vulkan của Mesa hoặc driver NVIDIA (xem README). Nếu không có,
bộ chuyển đổi chạy bằng CPU, chậm hơn nhiều.

## 2. Bốn chế độ

| Chế độ trong cửa sổ | Giá trị `-M` | Tác dụng |
|---|---|---|
| Khử nhiễu & Phóng to - AI | `denoise-scale` | khử nhiễu / vết nén JPEG và phóng to bằng model AI |
| Phóng to - AI | `scale` | phóng to bằng model AI, không khử nhiễu |
| Khử nhiễu - AI | `denoise` | khử nhiễu, giữ nguyên kích thước |
| Phóng to - Không AI | `non-ai-scale` | đổi kích thước bằng bộ lọc truyền thống (Lanczos, Bicubic, Jinc...), không dùng model |

Nếu không có `-M`, chế độ được chọn theo các tham số khác như waifu2x-ncnn-vulkan: `-n -1` là chỉ phóng to, `-s 1` là chỉ
khử nhiễu, còn lại là khử nhiễu và phóng to.

## 3. Ví dụ

**Ảnh anime, 2x, Real-CUGAN SE kèm khử nhiễu nhẹ:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**Ảnh chụp, rộng đúng 3840 pixel (chiều cao tự theo tỉ lệ), NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**Đúng 1920 x 1080 (kéo giãn về kích thước đó, giống waifu2x-caffe):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**Chỉ xóa vết nén JPEG, giữ nguyên kích thước:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25x, bước thu nhỏ sau AI sắc hơn (Lanczos 4 tap kèm chống nhiễu vòng):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**Pixel art, 4x, không AI (điểm ảnh sắc cạnh):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**Ảnh chụp, phóng to 1.5x không AI bằng Lanczos 3 tap kèm chống nhiễu vòng:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**Thu nhỏ ảnh còn một nửa, xử lý bằng linear light:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**Hình nền game bị dải màu (bầu trời lộ bậc), AI 2x, khử dải màu trước khi chạy AI:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**Thu nhỏ ảnh chụp có dải chuyển màu mượt: linear light và dithering Error Diffusion:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**Cả một thư mục, kể cả thư mục con, xuất ra WebP, bỏ qua ảnh đã xử lý:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**Chạy bằng CPU (không có GPU), dùng 8 nhân:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. Mọi tham số

### Đầu vào và đầu ra (mọi chế độ)

| Tham số | Ý nghĩa |
|---|---|
| `-i`, `--input path` | ảnh hoặc thư mục đầu vào |
| `-o`, `--output path` | ảnh hoặc thư mục đầu ra |
| `-r`, `--recursive` | xử lý cả thư mục con, giữ nguyên cấu trúc thư mục |
| `-l`, `--extensions list` | các đuôi tệp sẽ đọc trong thư mục, ví dụ `png:jpg:jpeg:webp` (mặc định: mọi tệp trong thư mục; nên đặt khi thư mục có cả tệp khác) |
| `-L`, `--file-list file` | tệp văn bản UTF-8, mỗi dòng một cặp `đầu vào<TAB>đầu ra` (xem mục 6) |
| `-k`, `--no-overwrite` | bỏ qua ảnh đã có tệp đầu ra (tệp cũ được giữ nguyên); tương ứng *Không ghi đè tệp đã có* trong cửa sổ |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (mặc định: theo đuôi tệp đầu ra, nếu không thì png). GIF / WebP động vẫn giữ là ảnh động |
| `-q`, `--quality q` | jpg 0-100 (mặc định 100), webp 1-100 hoặc 101 = không mất dữ liệu (mặc định 101), tga 0 / 1 = tắt / bật nén RLE |

### Chế độ và kích thước (mọi chế độ)

| Tham số | Ý nghĩa |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`, `scale`, `denoise` hoặc `non-ai-scale` (mục 2) |
| `-s`, `--scale r` | tỉ lệ phóng to, số dương bất kỳ: `2`, `1.5`, `0.5` (mặc định 2) |
| `-W`, `--width w` | chiều rộng đầu ra; chiều cao tự theo tỉ lệ ảnh nếu không có `-H` |
| `-H`, `--height h` | chiều cao đầu ra; chiều rộng tự theo tỉ lệ ảnh nếu không có `-W` |
| `-W` và `-H` cùng lúc | đúng chiều rộng x chiều cao đó |

### Các chế độ AI (`denoise-scale`, `scale`, `denoise`)

| Tham số | Ý nghĩa |
|---|---|
| `-m`, `--model path` | thư mục model (mặc định `models-cunet`). Engine tự chọn theo thư mục: `models-cunet`, `models-upconv_7_*`, `models-upresnet10` là waifu2x; `models-realcugan-se / -pro / -nose` là Real-CUGAN; `models-realesrgan-*`, `models-realesr-*`, `models-realesrnet-x4plus`, `models-nomos8ksc`, `models-lsdir-compact`, `models-nmkd-siax / -superscale` là Real-ESRGAN |
| `-n`, `--noise-level n` | mức khử nhiễu `-1` (không khử), `0`-`3` (mặc định 0). Real-CUGAN: 0 = bảo toàn. Real-ESRGAN General: độ mạnh khử nhiễu; LSDIR Compact: mức làm sạch; NMKD Siax: mức chi tiết 0-3; các model Real-ESRGAN khác tự làm sạch |
| `-t`, `--split-size t` | `0` = tự động (mặc định), `1` = không chia (cả ảnh là 1 tile), `2`-`512` = tile có kích thước đó; mỗi GPU một giá trị nếu dùng nhiều GPU (`0,0`) |
| `-c`, `--sync-gap n` | Sync gap của Real-CUGAN `0`-`3` (mặc định 3, 0 = tắt) |

Trước bước AI, `--deband` có thể làm mượt các dải màu có sẵn trong ảnh gốc (Tùy chọn > Cài đặt trước xử lý AI trong cửa
sổ). Sau bước AI, ảnh được thu nhỏ về đúng kích thước đầu ra (ví dụ 2.25x, hoặc rộng x cao) bằng bộ lọc **sau xử lý AI**:
`-F`, `-T`, `-a`, `--dither` và `--linear-light` bên dưới (Tùy chọn > Cài đặt sau xử lý AI trong cửa sổ). Mặc định là
Bicubic Mitchell, không chống nhiễu vòng, không dithering, không linear light. Nếu đúng 2x / 3x / 4x của model thì ảnh
được lưu luôn.

### Bộ lọc không AI, khử dải màu và dithering (`non-ai-scale`, và trước / sau bước AI ở các chế độ AI)

| Tham số | Ý nghĩa |
|---|---|
| `-F`, `--filter name` | `nearest`, `bilinear`, `mitchell` (mặc định), `catrom`, `lanczos`, `spline36`, `jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (mặc định) / `4` / `8`; Jinc `3` (mặc định) / `4` / `8`; các bộ lọc khác bỏ qua tham số này |
| `-a`, `--anti-ringing a` | `off` (mặc định), `relaxed`, `strict`: giảm nhiễu vòng của `catrom`, `lanczos`, `spline36` và `jinc` |
| `--linear-light` | xử lý bằng linear light; chỉ dùng khi ảnh nhỏ đi (không cạnh nào to ra, ít nhất một cạnh nhỏ lại) |
| `--dither d` | `off` (mặc định), `ordered`, `error-diffusion`: cách làm tròn kết quả về màu 8-bit. Dithering giúp dải chuyển màu mượt không bị lộ dải mới; dùng khi ảnh được đổi kích thước |
| `--deband d` | `off` (mặc định), `low`, `medium`, `high`: **Khử dải màu**, làm mượt các dải màu có sẵn trong ảnh (bầu trời, vùng tối, dải chuyển màu mềm) trước bước AI hoặc bộ lọc không AI. Chỉ các vùng phẳng bị thay đổi |

| Bộ lọc | Đặc điểm |
|---|---|
| `nearest` | Nearest Neighbor: điểm ảnh sắc cạnh, vuông vức, không mờ. Hợp với pixel art ở 2x / 3x / 4x |
| `bilinear` | nhanh và mềm, hơi mờ, không có nhiễu vòng |
| `mitchell` | Bicubic Mitchell: mềm và mượt, gần như không có nhiễu vòng. Lựa chọn an toàn |
| `catrom` | Bicubic Catmull-Rom: sắc hơn Mitchell, có chút nhiễu vòng |
| `lanczos` | sắc nét, giữ được chi tiết nhỏ, có chút nhiễu vòng (càng nhiều tap càng sắc, nhiễu vòng càng nhiều) |
| `spline36` | nằm giữa Catmull-Rom và Lanczos: sắc nét, cạnh sạch, ít nhiễu vòng |
| `jinc` | bộ lọc dạng tròn: sắc nét, cạnh chéo mượt, ít răng cưa; khá chậm |

### Hiệu năng (mọi chế độ)

| Tham số | Ý nghĩa |
|---|---|
| `-g`, `--gpu id` | GPU sẽ dùng: `0`, `1`... hoặc `-1` để dùng CPU (mặc định: GPU đầu tiên). Nhiều GPU: `0,1` |
| `-j`, `--threads l:p:s` | số luồng đọc : xử lý : lưu (mặc định `1:2:2`). Xử lý = số ảnh chạy cùng lúc trên mỗi GPU (1-20), hoặc số nhân CPU khi dùng `-g -1`. Hai GPU: `1:2,2:2` |
| `--vram-percent p` | dùng tối đa p % (20-80, mặc định 60) bộ nhớ GPU còn trống lúc bắt đầu chuyển đổi |
| `--ram-percent p` | dùng tối đa p % (20-80, mặc định 60) RAM còn trống lúc bắt đầu chuyển đổi |

### Khác

| Tham số | Ý nghĩa |
|---|---|
| `-h`, `--help` | hiện trợ giúp |
| `-v`, `--verbose` | in ra từng bước (các model đã chạy, bộ nhớ, thiết bị) |
| `--list-gpu` | liệt kê các GPU Vulkan rồi thoát |
| `--progress` | in tiến độ dạng máy đọc được (cửa sổ chương trình dùng) |

## 5. Chế độ nào dùng tham số nào

| Thiết lập trong cửa sổ | Các chế độ AI | Phóng to - Không AI | Tham số |
|---|---|---|---|
| Đường dẫn đầu vào / đầu ra, thư mục tùy chỉnh | ✓ | ✓ | `-i` `-o` (`-r`, `-l`, `-L`) |
| Định dạng / chất lượng đầu ra | ✓ | ✓ | `-f` `-q` |
| Chế độ | ✓ | ✓ | `-M` |
| Mức khử nhiễu / Mức chi tiết | ✓ | – | `-n` |
| Phóng to: Tỉ lệ, Rộng, Cao, Rộng x Cao | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| Model | ✓ | – | `-m` |
| Bộ lọc, Số tap, Chống nhiễu vòng (cửa sổ chính) | – | ✓ | `-F` `-T` `-a` |
| Bộ xử lý | ✓ | ✓ | `-g` |
| Kích thước tile | ✓ | – | `-t` |
| Luồng | ✓ | ✓ | `-j` |
| Tùy chọn: Sync gap của Real-CUGAN | ✓ | – | `-c` |
| Tùy chọn: Cài đặt trước xử lý AI (Khử dải màu) | ✓ | – | `--deband` |
| Tùy chọn: Cài đặt sau xử lý AI | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| Tùy chọn: Cài đặt bộ lọc không AI | – | ✓ | `--deband` `--dither` `--linear-light` |
| Tùy chọn: Không ghi đè tệp đã có | ✓ | ✓ | `-k` |
| Tùy chọn: giới hạn VRAM / RAM | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`, `-T`, `-a`, `--dither`, `--deband` và `--linear-light` là các thiết lập không AI khi dùng `non-ai-scale`, và là các
thiết lập trước / sau xử lý AI khi dùng các chế độ AI: mỗi lần chạy chỉ dùng một chế độ, nên một bộ tham số là đủ.

## 6. Thư mục, danh sách tệp và script

**Một thư mục:** `-i` là thư mục, `-o` là thư mục để ghi kết quả. Mỗi ảnh giữ nguyên tên, đổi đuôi theo định dạng đầu ra
(`-f`, mặc định png). Có `-r` thì các thư mục con cũng được xử lý, theo đúng cấu trúc cũ.

**Danh sách tệp** (`-L`) cho phép đổi ảnh bất kỳ sang tên bất kỳ. Mỗi dòng gồm `đầu vào`, một dấu TAB, rồi `đầu ra`:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

Có thể tạo danh sách bằng shell, ví dụ mọi ảnh PNG trong một thư mục:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**Một script shell** (`convert.sh`) xử lý mọi ảnh được truyền vào, lưu cạnh ảnh gốc:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

Lưu nó trong thư mục RePicturer, `chmod +x convert.sh`, rồi chạy `./convert.sh ~/Pictures/*.jpg`.

**Mọi ảnh JPG trong một thư mục qua chế độ Phóng to - Không AI:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(Khi có nhiều ảnh, chạy một lần với cả thư mục sẽ nhanh hơn: model chỉ phải tải một lần.)

## 7. Mã thoát và thông báo

| Mã thoát (`$?`) | Ý nghĩa |
|---|---|
| `0` | mọi ảnh đều đã được xử lý |
| `1` | có ảnh bị lỗi (từng ảnh được liệt kê) |
| `255` | không bắt đầu được: sai tham số, thiếu model hoặc thiếu đầu vào |

Ảnh bị lỗi trên GPU (hết bộ nhớ, lỗi driver) sẽ được thử lại tối đa 2 lần, lần đầu chia 2 tile, lần sau xử lý cả ảnh là 1
tile; nếu vẫn lỗi thì quá trình dừng lại kèm thông báo. Ở chế độ Phóng to - Không AI, ảnh nào GPU không xử lý được sẽ
được xử lý lại bằng CPU, kết quả vẫn y hệt.

## 8. Tên tham số cũ

Các tên của waifu2x-ncnn-vulkan và các bản RePicturer trước vẫn dùng được, nên script cũ vẫn chạy bình thường:

| Tên cũ | Tên từ v3.0 |
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

## 9. Khắc phục sự cố

* **"Permission denied"**: chạy `chmod +x repicturer-cli` trong thư mục.
* **"command not found"**: ghi `./repicturer-cli` (có `./`) hoặc thêm thư mục vào PATH (mục 1).
* **"no vulkan device found, falling back to cpu"**: cài driver Vulkan (`sudo apt install libvulkan1 mesa-vulkan-drivers`,
  hoặc driver NVIDIA). `--list-gpu` liệt kê các GPU mà bộ chuyển đổi nhìn thấy; `vulkaninfo --summary` (gói `vulkan-tools`)
  cho biết hệ thống có gì.
* **"GPU error (probably out of GPU memory)"**: giảm kích thước tile (`-t 200`, `-t 100`), giảm số luồng (`-j 1:1:2`) hoặc
  tăng `--vram-percent`.
* **Tên tệp có ký tự TAB hoặc xuống dòng** không đưa vào danh sách tệp (`-L`) được; hãy dùng `-i` / `-o` cho tệp đó.
