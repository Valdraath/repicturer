# RePicturer コマンドライン ガイド (Linux)

`repicturer-cli` は RePicturer のウィンドウの裏で動いている変換プログラムです。自分で実行したり、スクリプトから呼び出したりして、プログラムを開かずに画像を変換できます。ウィンドウでできることはすべてできます: AI モデル、
「拡大縮小 - 非 AI」、環境設定のすべての項目、フォルダーとファイルリスト。このガイドは **Linux** 用です。Windows 用は
[Japanese-Windows.md](Japanese-Windows.md) を見てください。

## 目次

1. はじめに
2. 4 つのモード
3. 使用例
4. すべてのオプション
5. 各モードが使うオプション
6. フォルダー、ファイルリスト、スクリプト
7. 終了コードとメッセージ
8. 以前のオプション名
9. トラブルシューティング

## 1. はじめに

1. `RePicturer-linux.zip` を展開し、そのフォルダーで端末を開きます:

   ```
   cd ~/RePicturer-linux
   ```

2. 変換プログラムに実行権限を付けます (1 回だけ。ダウンロード方法によっては権限が失われます):

   ```
   chmod +x repicturer-cli
   ```

3. 最初の変換を実行します:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. すべてのオプションを表示します:

   ```
   ./repicturer-cli -h
   ```

ほかのオプションを付けないときは **ノイズ除去と拡大 - AI**、2 倍、ノイズ除去レベル 0、モデル `models-cunet` で、最初の
GPU を使います。

**スペースを含むパス**は引用符で囲みます: `-i "$HOME/My Pictures/cat.png"`。モデルはフォルダー名だけで指定できます
(`-m models-realcugan-se`)。端末がどのフォルダーにあっても、`repicturer-cli` の隣から見つけます。

**どこからでも実行する:** フォルダーを PATH に追加すると (例: `~/.bashrc` に `export PATH="$HOME/RePicturer-linux:$PATH"`)、
`./` なしで `repicturer-cli` を呼び出せます。

GPU は **Vulkan** で使います: `libvulkan1` と Mesa の Vulkan ドライバーまたは NVIDIA ドライバーを入れてください (README
を参照)。ない場合は CPU で動き、かなり遅くなります。

## 2. 4 つのモード

| ウィンドウのモード | `-M` の値 | 内容 |
|---|---|---|
| ノイズ除去と拡大 - AI | `denoise-scale` | AI モデルでノイズ / JPEG の圧縮ノイズを取り除き、拡大する |
| 拡大 - AI | `scale` | AI モデルで拡大する。ノイズ除去なし |
| ノイズ除去 - AI | `denoise` | ノイズを取り除く。サイズはそのまま |
| 拡大縮小 - 非 AI | `non-ai-scale` | 従来のフィルター (Lanczos、Bicubic、Jinc など) でリサイズする。モデルなし |

`-M` を付けないときは、waifu2x-ncnn-vulkan と同じくほかのオプションで決まります: `-n -1` は拡大のみ、`-s 1` はノイズ除去
のみ、それ以外はノイズ除去と拡大です。

## 3. 使用例

**アニメ画像、2 倍、Real-CUGAN SE で軽いノイズ除去:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**写真を幅ちょうど 3840 ピクセルに (高さは比率に合わせる)、NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**ちょうど 1920 x 1080 に (waifu2x-caffe と同じくそのサイズに引き伸ばす):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**JPEG の圧縮ノイズだけを取り除き、サイズはそのまま:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**AI 2.25 倍、変換後の縮小をよりシャープに (Lanczos 4 タップ + アンチリンギング):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**ドット絵、4 倍、AI なし (くっきりしたピクセル):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**写真を AI なしで 1.5 倍、Lanczos 3 タップ + アンチリンギング:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**画像を半分のサイズに、リニアライトで縮小:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**バンディングのあるゲーム背景 (空に段差)、AI 2 倍、AI の前にカラーデバンディング:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**なめらかなグラデーションの写真を縮小: リニアライト + 誤差拡散ディザリング:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**フォルダー全体 (サブフォルダーも) を WebP に、変換済みの画像はスキップ:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**CPU で実行 (GPU なし)、8 コアを使用:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. すべてのオプション

### 入力と出力 (すべてのモード)

| オプション | 意味 |
|---|---|
| `-i`, `--input path` | 入力画像またはフォルダー |
| `-o`, `--output path` | 出力画像またはフォルダー |
| `-r`, `--recursive` | サブフォルダーも変換し、フォルダー構成をそのまま保つ |
| `-l`, `--extensions list` | フォルダーから読み込む拡張子。例: `png:jpg:jpeg:webp` (既定: フォルダー内のすべてのファイル。ほかのファイルもあるときは指定してください) |
| `-L`, `--file-list file` | UTF-8 のテキストファイル。1 行に `入力<TAB>出力` を 1 組 (6 章を参照) |
| `-k`, `--no-overwrite` | 出力ファイルがすでにある画像はスキップ (既存のファイルは残る)。ウィンドウの「既存のファイルを上書きしない」 |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (既定: 出力ファイルの拡張子、なければ png)。アニメーション GIF / WebP はアニメーションのまま |
| `-q`, `--quality q` | jpg 0-100 (既定 100)、webp 1-100 または 101 = ロスレス (既定 101)、tga 0 / 1 = RLE オフ / オン |

### モードとサイズ (すべてのモード)

| オプション | 意味 |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`、`scale`、`denoise`、`non-ai-scale` (2 章) |
| `-s`, `--scale r` | 拡大率。任意の正の数: `2`、`1.5`、`0.5` (既定 2) |
| `-W`, `--width w` | 出力の幅。`-H` がなければ高さは縦横比に合わせる |
| `-H`, `--height h` | 出力の高さ。`-W` がなければ幅は縦横比に合わせる |
| `-W` と `-H` の両方 | ちょうどその幅 x 高さ |

### AI モード (`denoise-scale`、`scale`、`denoise`)

| オプション | 意味 |
|---|---|
| `-m`, `--model path` | モデルのフォルダー (既定 `models-cunet`)。エンジンはフォルダーで決まります: `models-cunet`、`models-upconv_7_*`、`models-upresnet10` は waifu2x、`models-realcugan-se / -pro / -nose` は Real-CUGAN、`models-realesrgan-*`、`models-realesr-*`、`models-realesrnet-x4plus`、`models-nomos8ksc`、`models-lsdir-compact`、`models-nmkd-siax / -superscale` は Real-ESRGAN |
| `-n`, `--noise-level n` | ノイズ除去レベル `-1` (なし)、`0`-`3` (既定 0)。Real-CUGAN: 0 = 保守的。Real-ESRGAN General: ノイズ除去の強さ。LSDIR Compact: 補正の強さ。NMKD Siax: ディテールレベル 0-3。ほかの Real-ESRGAN モデルは自動で補正します |
| `-t`, `--split-size t` | `0` = 自動 (既定)、`1` = 分割しない (画像全体を 1 タイル)、`2` 以上 = そのサイズのタイル。複数 GPU では GPU ごとに 1 つ (`0,0`) |
| `-c`, `--sync-gap n` | Real-CUGAN の Sync gap `0`-`3` (既定 3、0 = オフ) |

AI の前には、`--deband` で元画像のバンディングをなめらかにできます (ウィンドウの「環境設定 > AI 変換前の設定」)。AI の後、
画像は**変換後**のフィルターで正確な出力サイズ (例: 2.25 倍、または 幅 x 高さ) に縮小されます: 下の `-F`、`-T`、`-a`、
`--dither`、`--linear-light` です (ウィンドウの「環境設定 > AI 変換後の設定」)。既定は Bicubic Mitchell、アンチリンギング
なし、ディザリングなし、リニアライトなしです。モデルそのままの 2x / 3x / 4x はそのまま保存されます。

### 非 AI フィルター、デバンディング、ディザリング (`non-ai-scale`、および AI モードの AI の前後)

| オプション | 意味 |
|---|---|
| `-F`, `--filter name` | `nearest`、`bilinear`、`mitchell` (既定)、`catrom`、`lanczos`、`spline36`、`jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (既定) / `4` / `8`、Jinc `3` (既定) / `4` / `8`。ほかのフィルターでは無視されます |
| `-a`, `--anti-ringing a` | `off` (既定)、`relaxed`、`strict`: `catrom`、`lanczos`、`spline36`、`jinc` のハローを減らす |
| `--linear-light` | リニアライトで拡大縮小。画像が小さくなるときだけ使う (どの辺も大きくならず、少なくとも 1 辺が小さくなる) |
| `--dither d` | `off` (既定)、`ordered`、`error-diffusion`: 結果を 8 ビットの色に丸める方法。ディザリングはなめらかなグラデーションに新しいバンドを出さない。リサイズするときに使う |
| `--deband d` | `off` (既定)、`low`、`medium`、`high`: **カラーデバンディング**。AI または非 AI フィルターの前に、画像にもともとあるバンディング (空、影、やわらかいグラデーション) をなめらかにする。平坦な部分だけが変わる |

| フィルター | 特徴 |
|---|---|
| `nearest` | ニアレストネイバー: 角ばったくっきりしたピクセル、ぼけなし。2x / 3x / 4x のドット絵に |
| `bilinear` | 速くてやわらかい、少しぼける、ハローなし |
| `mitchell` | Bicubic Mitchell: やわらかくなめらか、ハローはほとんどなし。無難な選択 |
| `catrom` | Bicubic Catmull-Rom: Mitchell よりシャープ、わずかなハロー |
| `lanczos` | シャープで細部を保つ、ハローあり (タップが多いほどシャープでハローも増える) |
| `spline36` | Catmull-Rom と Lanczos の中間: シャープ、輪郭がきれい、ハロー少なめ |
| `jinc` | 円形のフィルター: シャープ、斜めの輪郭がなめらか、ジャギー少なめ。かなり遅い |

### パフォーマンス (すべてのモード)

| オプション | 意味 |
|---|---|
| `-g`, `--gpu id` | 使う GPU: `0`、`1`… または CPU なら `-1` (既定: 最初の GPU)。複数 GPU: `0,1` |
| `-j`, `--threads l:p:s` | 読み込み : 処理 : 保存 のスレッド数 (既定 `1:2:2`)。処理 = GPU ごとに同時に処理する画像の数 (1-32)、`-g -1` では CPU コア数。GPU 2 台: `1:2,2:2` |
| `--vram-percent p` | 変換開始時に空いている GPU メモリの最大 p % を使う (20-80、既定 60) |
| `--ram-percent p` | 変換開始時に空いている RAM の最大 p % を使う (20-80、既定 60) |

### その他

| オプション | 意味 |
|---|---|
| `-h`, `--help` | ヘルプを表示 |
| `-v`, `--verbose` | すべての手順を表示 (実行したネットワーク、メモリ、デバイス) |
| `--list-gpu` | Vulkan の GPU を一覧表示して終了 |
| `--progress` | 機械が読める進行状況を出力 (ウィンドウが使用) |

## 5. 各モードが使うオプション

| ウィンドウの設定 | AI モード | 拡大縮小 - 非 AI | オプション |
|---|---|---|---|
| 入力 / 出力パス、個別のフォルダー | ✓ | ✓ | `-i` `-o` (`-r`、`-l`、`-L`) |
| 出力拡張子 / 出力品質 | ✓ | ✓ | `-f` `-q` |
| モード | ✓ | ✓ | `-M` |
| ノイズ除去レベル / ディテールレベル | ✓ | – | `-n` |
| 拡大: 拡大率、幅、高さ、幅x高さ | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| モデル | ✓ | – | `-m` |
| フィルター、タップ数、アンチリンギング (メインウィンドウ) | – | ✓ | `-F` `-T` `-a` |
| 処理ユニット | ✓ | ✓ | `-g` |
| 分割サイズ | ✓ | – | `-t` |
| スレッド数 | ✓ | ✓ | `-j` |
| 環境設定: Real-CUGAN の Sync gap | ✓ | – | `-c` |
| 環境設定: AI 変換前の設定 (カラーデバンディング) | ✓ | – | `--deband` |
| 環境設定: AI 変換後の設定 | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| 環境設定: 非 AI フィルターの設定 | – | ✓ | `--deband` `--dither` `--linear-light` |
| 環境設定: 既存のファイルを上書きしない | ✓ | ✓ | `-k` |
| 環境設定: VRAM / RAM の上限 | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`、`-T`、`-a`、`--dither`、`--deband`、`--linear-light` は、`non-ai-scale` では非 AI の設定、AI モードでは変換前 / 変換後の
設定になります。1 回の実行で使うモードは 1 つなので、オプションは 1 組で足ります。

## 6. フォルダー、ファイルリスト、スクリプト

**フォルダー:** `-i` にフォルダー、`-o` に書き込み先のフォルダーを指定します。各画像は同じ名前で、出力形式の拡張子
(`-f`、既定 png) になります。`-r` を付けるとサブフォルダーも同じ構成で変換されます。

**ファイルリスト** (`-L`) では、好きな画像を好きな名前に変換できます。各行は `入力`、TAB、`出力` です:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

リストはシェルで作れます。例: フォルダー以下のすべての PNG:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**シェルスクリプト** (`convert.sh`): 渡された画像をすべて変換し、元の画像の隣に保存します:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

RePicturer のフォルダーに保存し、`chmod +x convert.sh` を実行してから `./convert.sh ~/Pictures/*.jpg` で使います。

**フォルダー内のすべての JPG を「拡大縮小 - 非 AI」で処理:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(画像が多いときは、フォルダーを指定して 1 回で実行するほうが速くなります: モデルの読み込みが 1 回で済みます。)

## 7. 終了コードとメッセージ

| 終了コード (`$?`) | 意味 |
|---|---|
| `0` | すべての画像を変換した |
| `1` | 一部の画像が失敗した (失敗した画像はすべて表示される) |
| `255` | 変換が始まらなかった: オプションの誤り、モデルや入力がない |

GPU で失敗した画像 (メモリ不足、ドライバーの問題) は最大 2 回やり直します。まず 2 つのタイルで、次に 1 つのタイルで。
それでも失敗すると、メッセージを表示して変換を止めます。「拡大縮小 - 非 AI」では、GPU で処理できない画像は代わりに CPU で
処理され、結果は同じです。

## 8. 以前のオプション名

waifu2x-ncnn-vulkan と以前の RePicturer のオプション名も使えるので、古いスクリプトもそのまま動きます:

| 以前の名前 | v3.0 からの名前 |
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

## 9. トラブルシューティング

* **「Permission denied」**: フォルダーで `chmod +x repicturer-cli` を実行してください。
* **「command not found」**: `./repicturer-cli` (`./` 付き) と書くか、フォルダーを PATH に追加してください (1 章)。
* **「no vulkan device found, falling back to cpu」**: Vulkan ドライバーを入れてください (`sudo apt install libvulkan1
  mesa-vulkan-drivers`、または NVIDIA ドライバー)。`--list-gpu` で変換プログラムが認識している GPU を、`vulkaninfo --summary`
  (`vulkan-tools` パッケージ) でシステムの状態を確認できます。
* **「GPU error (probably out of GPU memory)」**: 分割サイズを小さくする (`-t 200`、`-t 100`)、スレッドを減らす
  (`-j 1:1:2`)、または `--vram-percent` を上げてください。
* **TAB や改行を含むファイル名**はファイルリスト (`-L`) に書けません。そのファイルは `-i` / `-o` で変換してください。
