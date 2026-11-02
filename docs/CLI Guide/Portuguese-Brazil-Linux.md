# Guia da linha de comando do RePicturer (Linux)

O `repicturer-cli` é o conversor que funciona por trás da janela do RePicturer. Você também pode executá-lo por conta
própria, num script, para converter imagens sem abrir o programa. Ele faz tudo o que a janela faz: os
modelos de IA, a “Escala - sem IA”, todas as opções das Preferências, pastas e listas de arquivos. Este guia é para
**Linux**; o de Windows é o [Portuguese-Brazil-Windows.md](Portuguese-Brazil-Windows.md).

## Índice

1. Primeiros passos
2. Os quatro modos
3. Exemplos
4. Todas as opções
5. Quais opções cada modo usa
6. Pastas, listas de arquivos e scripts
7. Códigos de saída e mensagens
8. Nomes antigos das opções
9. Solução de problemas

## 1. Primeiros passos

1. Descompacte o `RePicturer-linux.zip` e abra um terminal na pasta:

   ```
   cd ~/RePicturer-linux
   ```

2. Torne o conversor executável (uma vez; alguns downloads perdem essa permissão):

   ```
   chmod +x repicturer-cli
   ```

3. Faça a primeira conversão:

   ```
   ./repicturer-cli -i photo.jpg -o photo-2x.png
   ```

4. Veja todas as opções:

   ```
   ./repicturer-cli -h
   ```

Sem outras opções, o modo é **Reduzir ruído e ampliar - IA**, 2x, nível de ruído 0, modelo `models-cunet`, na primeira GPU.

**Caminhos com espaços** precisam de aspas: `-i "$HOME/My Pictures/cat.png"`. Um modelo pode ser indicado só pelo nome da
pasta (`-m models-realcugan-se`): ele é encontrado ao lado do `repicturer-cli`, em qualquer pasta em que o terminal esteja.

**Rodar de qualquer lugar:** adicione a pasta ao PATH (por exemplo no `~/.bashrc`:
`export PATH="$HOME/RePicturer-linux:$PATH"`) e chame `repicturer-cli` sem `./`.

A GPU é usada pelo **Vulkan**: instale `libvulkan1` e os drivers Vulkan do Mesa ou o driver da NVIDIA (veja o README). Sem
eles, o conversor roda na CPU, bem mais devagar.

## 2. Os quatro modos

| Modo na janela | Valor de `-M` | O que faz |
|---|---|---|
| Reduzir ruído e ampliar - IA | `denoise-scale` | remove ruído / artefatos de JPEG e amplia, com um modelo de IA |
| Ampliar - IA | `scale` | amplia com um modelo de IA, sem reduzir ruído |
| Reduzir ruído - IA | `denoise` | remove ruído, mantém o tamanho |
| Escala - sem IA | `non-ai-scale` | redimensiona com um filtro clássico (Lanczos, Bicubic, Jinc...), sem modelo |

Sem `-M`, o modo segue as outras opções como no waifu2x-ncnn-vulkan: `-n -1` significa só ampliar, `-s 1` significa só
reduzir ruído, e o resto é reduzir ruído e ampliar.

## 3. Exemplos

**Imagem de anime, 2x, Real-CUGAN SE com redução de ruído leve:**

```
./repicturer-cli -i art.png -o art-2x.png -m models-realcugan-se -n 1 -s 2
```

**Foto com exatamente 3840 pixels de largura (a altura acompanha), NMKD Superscale:**

```
./repicturer-cli -i photo.jpg -o photo-4k.png -M scale -m models-nmkd-superscale -W 3840
```

**Exatamente 1920 x 1080 (esticada para esse tamanho, como no waifu2x-caffe):**

```
./repicturer-cli -i shot.png -o shot-1080p.png -M scale -m models-realesr-animevideov3 -W 1920 -H 1080
```

**Só remover artefatos de JPEG, mantendo o tamanho:**

```
./repicturer-cli -i scan.jpg -o scan-clean.png -M denoise -n 2 -m models-cunet
```

**IA 2,25x com uma redução pós-conversão mais nítida (Lanczos 4 taps com antirringing):**

```
./repicturer-cli -i art.png -o art.png -M scale -m models-realcugan-se -s 2.25 -F lanczos -T 4 -a relaxed
```

**Pixel art, 4x, sem IA (pixels nítidos):**

```
./repicturer-cli -i sprite.png -o sprite-4x.png -M non-ai-scale -F nearest -s 4
```

**Foto, 1,5x sem IA com Lanczos 3 taps e antirringing:**

```
./repicturer-cli -i photo.jpg -o photo-15.png -M non-ai-scale -F lanczos -T 3 -a relaxed -s 1.5
```

**Reduzir uma imagem à metade, em luz linear:**

```
./repicturer-cli -i big.png -o small.png -M non-ai-scale -F catrom -s 0.5 --linear-light
```

**Fundo de jogo com faixas (céu com degraus), IA 2x, com debanding de cor antes da IA:**

```
./repicturer-cli -i sky.png -o sky-2x.png -m models-realcugan-se -s 2 --deband medium
```

**Reduzir uma foto com degradês suaves: luz linear e dithering por difusão de erro:**

```
./repicturer-cli -i photo.png -o photo-small.png -M non-ai-scale -F catrom -s 0.5 --linear-light --dither error-diffusion
```

**Uma pasta inteira, com subpastas, para WebP, pulando imagens já feitas:**

```
./repicturer-cli -i ~/Pictures -o ~/Pictures-2x -r -k -f webp -q 90 -m models-realcugan-se -s 2
```

**Na CPU (sem GPU), com 8 núcleos:**

```
./repicturer-cli -i art.png -o art-2x.png -g -1 -j 1:8:2
```

## 4. Todas as opções

### Entrada e saída (todos os modos)

| Opção | Significado |
|---|---|
| `-i`, `--input path` | imagem ou pasta de entrada |
| `-o`, `--output path` | imagem ou pasta de saída |
| `-r`, `--recursive` | também converte subpastas, mantendo a estrutura |
| `-l`, `--extensions list` | extensões lidas de uma pasta, ex. `png:jpg:jpeg:webp` (padrão: todos os arquivos dela; defina quando a pasta tiver outros arquivos) |
| `-L`, `--file-list file` | arquivo de texto UTF-8, um par `entrada<TAB>saída` por linha (veja a seção 6) |
| `-k`, `--no-overwrite` | pula imagens cujo arquivo de saída já existe (ele é mantido); o *Não substituir arquivos existentes* da janela |
| `-f`, `--format fmt` | `png` `jpg` `webp` `bmp` `jp2` `sr` `tif` `hdr` `exr` `ppm` `tga` `gif` (padrão: a extensão do arquivo de saída, senão png). GIF / WebP animados continuam animados |
| `-q`, `--quality q` | jpg 0-100 (padrão 100), webp 1-100 ou 101 = sem perdas (padrão 101), tga 0 / 1 = RLE desligado / ligado |

### Modo e tamanho (todos os modos)

| Opção | Significado |
|---|---|
| `-M`, `--mode mode` | `denoise-scale`, `scale`, `denoise` ou `non-ai-scale` (seção 2) |
| `-s`, `--scale r` | fator de escala, qualquer número positivo: `2`, `1.5`, `0.5` (padrão 2) |
| `-W`, `--width w` | largura de saída; a altura segue a proporção, a menos que `-H` seja definido |
| `-H`, `--height h` | altura de saída; a largura segue a proporção, a menos que `-W` seja definido |
| `-W` e `-H` juntos | exatamente essa largura x altura |

### Modos com IA (`denoise-scale`, `scale`, `denoise`)

| Opção | Significado |
|---|---|
| `-m`, `--model path` | pasta do modelo (padrão `models-cunet`). O motor segue a pasta: `models-cunet`, `models-upconv_7_*`, `models-upresnet10` são waifu2x; `models-realcugan-se / -pro / -nose` são Real-CUGAN; `models-realesrgan-*`, `models-realesr-*`, `models-realesrnet-x4plus`, `models-nomos8ksc`, `models-lsdir-compact`, `models-nmkd-siax / -superscale` são Real-ESRGAN |
| `-n`, `--noise-level n` | nível de ruído `-1` (nenhum), `0`-`3` (padrão 0). Real-CUGAN: 0 = conservador. Real-ESRGAN General: força da redução de ruído; LSDIR Compact: nível de limpeza; NMKD Siax: o nível de detalhe 0-3; os outros modelos Real-ESRGAN limpam sozinhos |
| `-t`, `--split-size t` | `0` = automático (padrão), `1` = sem divisão (a imagem inteira como 1 bloco), `2` ou mais = blocos desse tamanho; um valor por GPU com várias GPUs (`0,0`) |
| `-c`, `--sync-gap n` | Sync gap do Real-CUGAN `0`-`3` (padrão 3, 0 = desligado) |

Antes da IA, `--deband` pode suavizar o banding da imagem original (Preferências > Configurações antes da conversão por IA, na janela).
Depois da IA, a imagem é reduzida ao tamanho exato de saída (por exemplo 2,25x, ou largura x altura) com o filtro
**pós-conversão**: `-F`, `-T`, `-a`, `--dither` e `--linear-light` abaixo (Preferências > Configurações após a conversão por IA, na
janela). O padrão é Bicubic Mitchell, sem antirringing, sem dithering, sem luz linear. Um 2x / 3x / 4x exato do modelo é
salvo como está.

### Filtro sem IA, debanding e dithering (`non-ai-scale`, e antes / depois da IA nos modos com IA)

| Opção | Significado |
|---|---|
| `-F`, `--filter name` | `nearest`, `bilinear`, `mitchell` (padrão), `catrom`, `lanczos`, `spline36`, `jinc` |
| `-T`, `--taps n` | Lanczos `2` / `3` (padrão) / `4` / `8`; Jinc `3` (padrão) / `4` / `8`; os outros filtros ignoram |
| `-a`, `--anti-ringing a` | `off` (padrão), `relaxed`, `strict`: reduz os halos de `catrom`, `lanczos`, `spline36` e `jinc` |
| `--linear-light` | redimensiona em luz linear; usado só quando a imagem fica menor (nenhum lado maior, pelo menos um menor) |
| `--dither d` | `off` (padrão), `ordered`, `error-diffusion`: como o resultado é arredondado para cor de 8 bits. O dithering evita novas faixas em degradês suaves; usado quando a imagem é redimensionada |
| `--deband d` | `off` (padrão), `low`, `medium`, `high`: **Debanding de cor**, suaviza o banding que já existe na imagem (céus, sombras, degradês suaves) antes da IA ou do filtro sem IA. Só áreas planas mudam |

| Filtro | Característica |
|---|---|
| `nearest` | Vizinho mais próximo: pixels duros e quadriculados, sem desfoque. Pixel art em 2x / 3x / 4x |
| `bilinear` | rápido e suave, um pouco desfocado, sem halos |
| `mitchell` | Bicubic Mitchell: suave e uniforme, quase sem halos. Uma escolha segura |
| `catrom` | Bicubic Catmull-Rom: mais nítido que o Mitchell, halos leves |
| `lanczos` | nítido, preserva detalhes finos, alguns halos (mais taps = mais nítido, mais halos) |
| `spline36` | entre Catmull-Rom e Lanczos: nítido, bordas limpas, poucos halos |
| `jinc` | um filtro redondo: nítido, diagonais suaves, poucos serrilhados; bem lento |

### Desempenho (todos os modos)

| Opção | Significado |
|---|---|
| `-g`, `--gpu id` | a GPU usada: `0`, `1`... ou `-1` para a CPU (padrão: a primeira GPU). Várias GPUs: `0,1` |
| `-j`, `--threads l:p:s` | threads de leitura : processamento : gravação (padrão `1:2:2`). Processamento = imagens ao mesmo tempo em cada GPU (1-32), ou os núcleos da CPU com `-g -1`. Duas GPUs: `1:2,2:2` |
| `--vram-percent p` | usa no máximo p % (20-80, padrão 60) da memória da GPU livre no início da conversão |
| `--ram-percent p` | usa no máximo p % (20-80, padrão 60) da RAM livre no início da conversão |

### Outros

| Opção | Significado |
|---|---|
| `-h`, `--help` | mostra a ajuda |
| `-v`, `--verbose` | mostra cada etapa (as redes executadas, a memória, o dispositivo) |
| `--list-gpu` | lista as GPUs Vulkan e sai |
| `--progress` | linhas de progresso legíveis por máquina (usadas pela janela) |

## 5. Quais opções cada modo usa

| Configuração na janela | Modos com IA | Escala - sem IA | Opção |
|---|---|---|---|
| Caminho de entrada / saída, pastas personalizadas | ✓ | ✓ | `-i` `-o` (`-r`, `-l`, `-L`) |
| Extensão / qualidade de saída | ✓ | ✓ | `-f` `-q` |
| Modo | ✓ | ✓ | `-M` |
| Nível de ruído / Nível de detalhe | ✓ | – | `-n` |
| Ampliação: Fator, Largura, Altura, Largura x Altura | ✓ | ✓ | `-s` / `-W` / `-H` / `-W` + `-H` |
| Modelo | ✓ | – | `-m` |
| Filtro, Amostras, Antirringing (janela principal) | – | ✓ | `-F` `-T` `-a` |
| Unidade de processamento | ✓ | ✓ | `-g` |
| Tamanho do bloco | ✓ | – | `-t` |
| Threads | ✓ | ✓ | `-j` |
| Preferências: Sync gap do Real-CUGAN | ✓ | – | `-c` |
| Preferências: Configurações antes da conversão por IA (Debanding de cor) | ✓ | – | `--deband` |
| Preferências: Configurações após a conversão por IA | ✓ | – | `-F` `-T` `-a` `--dither` `--linear-light` |
| Preferências: Configurações dos filtros sem IA | – | ✓ | `--deband` `--dither` `--linear-light` |
| Preferências: Não substituir arquivos existentes | ✓ | ✓ | `-k` |
| Preferências: limite de VRAM / RAM | ✓ | ✓ | `--vram-percent` `--ram-percent` |

`-F`, `-T`, `-a`, `--dither`, `--deband` e `--linear-light` são as configurações sem IA em `non-ai-scale` e as configurações
pré / pós-conversão nos modos com IA: cada execução usa um só modo, então um conjunto de opções basta.

## 6. Pastas, listas de arquivos e scripts

**Uma pasta:** `-i` é uma pasta e `-o` a pasta onde gravar. Cada imagem recebe o mesmo nome com a extensão do formato de
saída (`-f`, padrão png). Com `-r` as subpastas também são convertidas, na mesma estrutura.

**Uma lista de arquivos** (`-L`) converte quaisquer imagens para quaisquer nomes. Cada linha é `entrada`, um TAB, e depois
`saída`:

```
/home/me/in/a.png	/home/me/out/a-2x.png
/home/me/in/b.jpg	/home/me/out/b-2x.webp
```

```
./repicturer-cli -L list.txt -m models-realcugan-se -s 2
```

Dá para gerar a lista pelo shell, por exemplo todos os PNG de uma pasta:

```sh
find ~/in -name '*.png' | while read -r f; do printf '%s\t%s\n' "$f" "${f%.png}-2x.png"; done > list.txt
```

**Um script shell** (`convert.sh`) que converte cada imagem passada a ele, ao lado da original:

```sh
#!/bin/sh
cli="$(dirname "$0")/repicturer-cli"
for f in "$@"; do
    "$cli" -i "$f" -o "${f%.*}-2x.png" -m models-realcugan-se -s 2
done
```

Salve-o na pasta do RePicturer, rode `chmod +x convert.sh` e depois `./convert.sh ~/Pictures/*.jpg`.

**Todos os JPGs de uma pasta pela Escala - sem IA:**

```sh
for f in ~/in/*.jpg; do
    ./repicturer-cli -i "$f" -o ~/out/"$(basename "${f%.jpg}")".png -M non-ai-scale -F lanczos -s 2
done
```

(Para muitas imagens, uma única chamada com a pasta é mais rápida: o modelo é carregado só uma vez.)

## 7. Códigos de saída e mensagens

| Código de saída (`$?`) | Significado |
|---|---|
| `0` | todas as imagens foram convertidas |
| `1` | algumas imagens falharam (cada uma é listada) |
| `255` | a conversão não começou: opção errada, modelo ou entrada ausente |

Uma imagem que falha na GPU (falta de memória, problema de driver) é refeita até 2 vezes, primeiro em 2 blocos, depois como
1 bloco; se ainda falhar, a conversão para com uma mensagem. Na Escala - sem IA, uma imagem que a GPU não consegue
redimensionar é redimensionada na CPU, com o mesmo resultado.

## 8. Nomes antigos das opções

Os nomes do waifu2x-ncnn-vulkan e das versões anteriores do RePicturer continuam funcionando, então scripts antigos seguem
rodando:

| Nome antigo | Nome desde a v3.0 |
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

## 9. Solução de problemas

* **"Permission denied"**: rode `chmod +x repicturer-cli` na pasta.
* **"command not found"**: escreva `./repicturer-cli` (com `./`) ou adicione a pasta ao PATH (seção 1).
* **"no vulkan device found, falling back to cpu"**: instale o driver Vulkan (`sudo apt install libvulkan1 mesa-vulkan-drivers`,
  ou o driver da NVIDIA). `--list-gpu` mostra as GPUs que o conversor enxerga; `vulkaninfo --summary` (pacote `vulkan-tools`)
  mostra o que o sistema oferece.
* **"GPU error (probably out of GPU memory)"**: use um tamanho de bloco menor (`-t 200`, `-t 100`), menos threads
  (`-j 1:1:2`) ou aumente `--vram-percent`.
* **Um nome de arquivo com TAB ou quebra de linha** não pode ir numa lista de arquivos (`-L`); converta-o com `-i` / `-o`.
