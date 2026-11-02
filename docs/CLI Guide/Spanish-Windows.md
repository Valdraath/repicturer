# Guía de la línea de comandos de RePicturer (Windows)

`repicturer-cli.exe` es el conversor que funciona detrás de la ventana de RePicturer. También puedes ejecutarlo tú mismo,
en un script o un archivo por lotes, para convertir imágenes sin abrir el programa. Hace todo lo que hace la ventana: los
modelos de IA, la “Escala - sin IA”, todas las opciones de Preferencias, carpetas y listas de archivos. Esta guía es para
**Windows**; la de Linux es [Spanish-Linux.md](Spanish-Linux.md).

## Índice

1. Primeros pasos
2. Los cuatro modos
3. Ejemplos
4. Todas las opciones
5. Qué opciones usa cada modo
6. Carpetas, listas de archivos y archivos por lotes
7. Códigos de salida y mensajes
8. Nombres antiguos de las opciones
9. Solución de problemas

## 1. Primeros pasos

1. Abre la carpeta de RePicturer (la que tiene `RePicturer.exe` y `repicturer-cli.exe`).
2. Haz clic en la barra de direcciones del Explorador de archivos, escribe `cmd` o `powershell` y pulsa Enter. Se abre una
   terminal en esa carpeta.
3. Haz una primera conversión:

   ```
   repicturer-cli.exe -i photo.jpg -o photo-2x.png
   ```

   En **PowerShell**, empieza el nombre con `.\`:

   ```
   .\repicturer-cli.exe -i photo.jpg -o photo-2x.png
   ```

4. Mira todas las opciones:

   ```
   repicturer-cli.exe -h
   ```

Sin otras opciones, el modo es **Reducir ruido y ampliar - IA**, 2x, nivel de ruido 0, modelo `models-cunet`, en la primera GPU.

**Las rutas con espacios** necesitan comillas: `-i "C:\My Pictures\cat.png"`. Un modelo se puede indicar solo con el nombre
de su carpeta (`-m models-realcugan-se`): se busca junto a `repicturer-cli.exe`, esté donde esté la terminal.

## 2. Los cuatro modos

| Modo en la ventana | Valor de `-M` | Qué hace |
|---|---|---|
| Reducir ruido y ampliar - IA | `denoise-scale` | quita ruido / artefactos de JPEG y amplía, con un modelo de IA |
| Ampliar - IA | `scale` | amplía con un modelo de IA, sin reducir ruido |
| Reducir ruido - IA | `denoise` | quita ruido, mantiene el tamaño |
| Escala - sin IA | `non-ai-scale` | redimensiona con un filtro clásico (Lanczos, Bicubic, Jinc...), sin modelo |

Sin `-M`, el modo sigue a las demás opciones como en waifu2x-ncnn-vulkan: `-n -1` significa solo ampliar, `-s 1` significa
solo reducir ruido y lo demás es reducir ruido y ampliar.

## 3. Ejemplos

**Imagen de anime, 2x, Real-CUGAN SE con una reducción de ruido ligera:**

```
repicturer-cli.exe -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**Foto con exactamente 3840 píxeles de ancho (el alto se ajusta), NMKD Superscale:**

```
repicturer-cli.exe -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**Exactamente 1920 x 1080 (estirada a ese tamaño, como en waifu2x-caffe):**

```
repicturer-cli.exe -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**Solo quitar artefactos de JPEG, manteniendo el tamaño:**

```
repicturer-cli.exe -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**IA 2,25x con una reducción posterior más nítida (Lanczos 4 taps con antirringing):**

```
repicturer-cli.exe -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**Pixel art, 4x, sin IA (píxeles nítidos):**

```
repicturer-cli.exe -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**Foto, 1,5x sin IA con Lanczos 3 taps y antirringing:**

```
repicturer-cli.exe -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**Reducir una imagen a la mitad, en luz lineal:**

```
repicturer-cli.exe -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**Fondo de juego con bandas (cielo con escalones), IA 2x, con reducción de bandas antes de la IA:**

```
repicturer-cli.exe -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**Reducir una foto con degradados suaves: luz lineal y dithering por difusión de error:**

```
repicturer-cli.exe -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**Una carpeta entera, con subcarpetas, a WebP, saltando las imágenes ya hechas:**

```
repicturer-cli.exe -i D:\Pictures -o D:\Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**En la CPU (sin GPU), con 8 núcleos:**

```
repicturer-cli.exe -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. Todas las opciones

### Entrada y salida (todos los modos)

| Opción | Significado |
|---|---|
| `-i`, `--input path` | imagen o carpeta de entrada |
| `-o`, `--output path` | imagen o carpeta de salida |
| `-r`, `--recursive` | convierte también las subcarpetas, manteniendo la estructura |
| `-l`, `--extensions list` | extensiones que se leen de una carpeta, p. ej. `png:jpg:jpeg:webp` (por defecto: todos sus archivos; ponlo si la carpeta tiene otros archivos) |
| `-L`, `--file-list file` | archivo de texto UTF-8, un par `entrada<TAB>salida` por línea (ver sección 6) |
| `-k`, `--no-overwrite` | salta las imágenes cuyo archivo de salida ya existe (se conserva); el *No sobrescribir archivos existentes* de la ventana |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (por defecto: la extensión del archivo de salida, si no png). Los GIF / WebP animados siguen animados |
| `-q`, `--quality q` | jpg 0-100 (por defecto 100), webp 1-100 o 101 = sin pérdida (por defecto 101), tga 0 / 1 = RLE desactivado / activado |

### Modo y tamaño (todos los modos)

| Opción | Significado |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`, `scale`, `denoise` o `non-ai-scale` (sección 2) |
| `-s`, `--scale r` | factor de escala, cualquier número positivo: `2`, `1.5`, `0.5` (por defecto 2) |
| `-W`, `--width w` | ancho de salida; el alto sigue la proporción salvo que se indique `-H` |
| `-H`, `--height h` | alto de salida; el ancho sigue la proporción salvo que se indique `-W` |
| `-W` y `-H` juntos | exactamente ese ancho x alto |

### Modos con IA (`denoise-scale`, `scale`, `denoise`)

| Opción | Significado |
|---|---|
| `-m`, `--model path` | carpeta del modelo (por defecto `models-cunet`). El motor depende de la carpeta: `models-cunet`, `models-upconv_7_*`, `models-upresnet10` son waifu2x; `models-realcugan-se / -pro / -nose` son Real-CUGAN; `models-realesrgan-*`, `models-realesr-*`, `models-realesrnet-x4plus`, `models-nomos8ksc`, `models-lsdir-compact`, `models-nmkd-siax / -superscale` son Real-ESRGAN |
| `-n`, `--noise-level n` | nivel de ruido `-1` (ninguno), `0`-`3` (por defecto 0). Real-CUGAN: 0 = conservador. Real-ESRGAN General: fuerza de la reducción de ruido; LSDIR Compact: nivel de limpieza; NMKD Siax: el nivel de detalle 0-3; los demás modelos Real-ESRGAN limpian por sí solos |
| `-t`, `--split-size t` | `0` = automático (por defecto), `1` = sin dividir (toda la imagen como 1 bloque), `2` o más = bloques de ese tamaño; un valor por GPU con varias GPU (`0,0`) |
| `-c`, `--sync-gap n` | Sync gap de Real-CUGAN `0`-`3` (por defecto 3, 0 = desactivado) |

Antes de la IA, `--deband` puede suavizar las bandas de la imagen original (Preferencias > Ajustes antes de la conversión con IA,
en la ventana). Después de la IA, la imagen se reduce al tamaño exacto de salida (por ejemplo 2,25x, o un ancho x alto) con
el filtro **posterior a la conversión**: `-F`, `-T`, `-a`, `--dither` y `--linear-light` abajo (Preferencias > Ajustes
posteriores a la conversión, en la ventana). Por defecto es Bicubic Mitchell, sin antirringing, sin dithering, sin luz
lineal. Un 2x / 3x / 4x exacto del modelo se guarda tal cual.

### Filtro sin IA, reducción de bandas y dithering (`non-ai-scale`, y antes / después de la IA en los modos con IA)

| Opción | Significado |
|---|---|
| `-F`, `--filter name` | `nearest`, `bilinear`, `mitchell` (por defecto), `catrom`, `lanczos`, `spline36`, `jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (por defecto) / `4` / `8`; Jinc `3` (por defecto) / `4` / `8`; los demás filtros lo ignoran |
| `-a`, `--anti-ringing a` | `off` (por defecto), `relaxed`, `strict`: reduce los halos de `catrom`, `lanczos`, `spline36` y `jinc` |
| `--linear-light` | redimensiona en luz lineal; solo se usa cuando la imagen se hace más pequeña (ningún lado mayor, al menos uno menor) |
| `--dither d` | `off` (por defecto), `ordered`, `error-diffusion`: cómo se redondea el resultado a color de 8 bits. El dithering evita bandas nuevas en los degradados suaves; se usa al redimensionar |
| `--deband d` | `off` (por defecto), `low`, `medium`, `high`: **Reducción de bandas**, suaviza las bandas que ya hay en la imagen (cielos, sombras, degradados suaves) antes de la IA o del filtro sin IA. Solo cambian las zonas planas |

| Filtro | Carácter |
|---|---|
| `nearest` | Vecino más cercano: píxeles duros y pixelados, sin desenfoque. Pixel art a 2x / 3x / 4x |
| `bilinear` | rápido y suave, algo borroso, sin halos |
| `mitchell` | Bicubic Mitchell: suave y uniforme, casi sin halos. Una opción segura |
| `catrom` | Bicubic Catmull-Rom: más nítido que Mitchell, halos leves |
| `lanczos` | nítido, conserva el detalle fino, algunos halos (más taps = más nítido, más halos) |
| `spline36` | entre Catmull-Rom y Lanczos: nítido, bordes limpios, pocos halos |
| `jinc` | un filtro redondo: nítido, diagonales suaves, pocos dientes de sierra; bastante lento |

### Rendimiento (todos los modos)

| Opción | Significado |
|---|---|
| `-g`, `--gpu id` | la GPU a usar: `0`, `1`... o `-1` para la CPU (por defecto: la primera GPU). Varias GPU: `0,1` |
| `-j`, `--threads l:p:s` | hilos de carga : proceso : guardado (por defecto `1:2:2`). Proceso = imágenes a la vez en cada GPU (1-32), o los núcleos de la CPU con `-g -1`. Dos GPU: `1:2,2:2` |
| `--vram-percent p` | usa como máximo el p % (20-80, por defecto 60) de la memoria de GPU libre al empezar la conversión |
| `--ram-percent p` | usa como máximo el p % (20-80, por defecto 60) de la RAM libre al empezar la conversión |

### Otros

| Opción | Significado |
|---|---|
| `-h`, `--help` | muestra la ayuda |
| `-v`, `--verbose` | muestra cada paso (las redes ejecutadas, la memoria, el dispositivo) |
| `--list-gpu` | lista las GPU Vulkan y sale |
| `--progress` | líneas de progreso legibles por máquina (las usa la ventana) |

## 5. Qué opciones usa cada modo

| Ajuste en la ventana | Modos con IA | Escala - sin IA | Opción |
|---|---|---|---|
| Ruta de entrada / salida, carpetas personalizadas | ✓ | ✓ | `-i` `-o` (`-r`, `-l`, `-L`) |
| Extensión / calidad de salida | ✓ | ✓ | `-f` `-q` |
| Modo | ✓ | ✓ | `-M` |
| Nivel de ruido / Nivel de detalle | ✓ | – | `-n` |
| Ampliación: Factor, Ancho, Alto, Ancho x Alto | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| Modelo | ✓ | – | `-m` |
| Filtro, Muestras, Antirringing (ventana principal) | – | ✓ | `-F` `-T` `-a` |
| Unidad de procesamiento | ✓ | ✓ | `-g` |
| Tamaño de división | ✓ | – | `-t` |
| Hilos | ✓ | ✓ | `-j` |
| Preferencias: Sync gap de Real-CUGAN | ✓ | – | `-c` |
| Preferencias: Ajustes antes de la conversión con IA (Reducción de bandas) | ✓ | – | `--deband` |
| Preferencias: Ajustes después de la conversión con IA | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| Preferencias: Ajustes de los filtros sin IA | – | ✓ | `--deband` `--dither` `--linear-light` |
| Preferencias: No sobrescribir archivos existentes | ✓ | ✓ | `-k` |
| Preferencias: límite de VRAM / RAM | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`, `-T`, `-a`, `--dither`, `--deband` y `--linear-light` son los ajustes sin IA en `non-ai-scale` y los ajustes previos /
posteriores a la conversión en los modos con IA: cada ejecución usa un solo modo, así que basta con un conjunto de opciones.

## 6. Carpetas, listas de archivos y archivos por lotes

**Una carpeta:** `-i` es una carpeta y `-o` la carpeta donde escribir. Cada imagen recibe el mismo nombre con la extensión
del formato de salida (`-f`, por defecto png). Con `-r` también se convierten las subcarpetas, con la misma estructura.

**Una lista de archivos** (`-L`) convierte cualquier imagen a cualquier nombre. Cada línea es `entrada`, un TAB y después
`salida`:

```
D:\in\a.png	D:\out\a-2x.png
D:\in\b.jpg	D:\out\b-2x.webp
```

```
repicturer-cli.exe -L list.txt -m models-realcugan-se -s 2
```

**Un archivo por lotes** (`convert.bat`) que convierte cada imagen que se suelte sobre él, junto a la original:

```bat
@echo off
cd /d "%~dp0"
for %%F in (%*) do (
    repicturer-cli.exe -i "%%~F" -o "%%~dpnF-2x.png" -m models-realcugan-se -s 2
)
pause
```

Guárdalo en la carpeta de RePicturer y suelta las imágenes sobre él en el Explorador de archivos.

**PowerShell**, todos los JPG de una carpeta con la Escala - sin IA:

```powershell
Get-ChildItem D:\in -Filter *.jpg | ForEach-Object {
    .\repicturer-cli.exe -i $_.FullName -o ("D:\out\" + $_.BaseName + ".png") -M non-ai-scale -F lanczos -s 2
}
```

(Con muchas imágenes, una sola llamada con la carpeta es más rápida: el modelo se carga una sola vez.)

## 7. Códigos de salida y mensajes

| Código de salida (`%ERRORLEVEL%`) | Significado |
|---|---|
| `0` | se convirtieron todas las imágenes |
| `1` | algunas imágenes fallaron (cada una aparece en la lista) |
| `-1` | la conversión no empezó: una opción errónea, falta el modelo o la entrada |

Una imagen que falla en la GPU (falta de memoria, un problema del controlador) se reintenta hasta 2 veces, primero en 2
bloques y después como 1 bloque; si sigue fallando, la conversión se detiene con un mensaje. En la Escala - sin IA, una
imagen que la GPU no puede redimensionar se redimensiona en la CPU, con el mismo resultado.

## 8. Nombres antiguos de las opciones

Los nombres de waifu2x-ncnn-vulkan y de las versiones anteriores de RePicturer siguen funcionando, así que los scripts
antiguos siguen funcionando:

| Nombre antiguo | Nombre desde la v3.0 |
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

## 9. Solución de problemas

* **"no se reconoce como un comando interno o externo"**: la terminal no está en la carpeta de RePicturer. Usa `cd` para ir
  allí, o escribe la ruta completa: `"C:\Tools\RePicturer\repicturer-cli.exe"`.
* **PowerShell dice que no encuentra el comando**: escribe `.\repicturer-cli.exe`.
* **"no vulkan device found, falling back to cpu"**: actualiza el controlador de la tarjeta gráfica (NVIDIA, AMD o Intel).
  `--list-gpu` muestra las GPU que ve el conversor.
* **"GPU error (probably out of GPU memory)"**: usa un tamaño de división menor (`-t 200`, `-t 100`), menos hilos
  (`-j 1:1:2`) o sube `--vram-percent`.
* **Caracteres extraños en la salida**: el conversor escribe en UTF-8; en una consola antigua, ejecuta antes `chcp 65001`.
