# Third party licenses

The program contains the following models and software. Their license texts are in the `licenses/` folder next to the program
(in the source tree: `licenses/` and `third_party/`). Every model is free to use, share and modify, also commercially.

## Models

| model (folder) | author | license | license text / source |
|---|---|---|---|
| waifu2x CUnet, UpRGB, UpPhoto (models-cunet, models-upconv_7_*) | nagadomi | MIT | licenses/waifu2x-MIT.txt, https://github.com/nagadomi/waifu2x |
| waifu2x UpResNet10 (models-upresnet10), converted from waifu2x-caffe for this program | lltcggie | MIT | licenses/waifu2x-caffe-MIT.txt, https://github.com/lltcggie/waifu2x-caffe |
| Real-CUGAN SE, Pro, NoSE (models-realcugan-*) | Bilibili AI Lab | MIT | licenses/Real-CUGAN-MIT.txt, https://github.com/bilibili/ailab/tree/main/Real-CUGAN |
| Real-ESRGAN x4plus, x2plus (models-realesrgan-x4plus/x2.*), x4plus anime, animevideo v3, RealESRNet x4plus, realesr-general-x4v3 + wdn (models-realesrgan-*, models-realesr-*, models-realesrnet-x4plus). Converted to ncnn for this program where no ncnn version existed; models-realesr-general-x4v3 holds blends of the general / wdn networks at five denoise strengths | Xintao Wang / Tencent ARC | BSD-3-Clause | licenses/Real-ESRGAN-BSD-3-Clause.txt, https://github.com/xinntao/Real-ESRGAN |
| 4x-NMKD-Siax ("CX", 200k) (models-nmkd-siax). Fine-tuned from ESRGAN 4x-PSNR. Converted from PyTorch to ncnn (fp16) | Nmkd | WTFPL | models-nmkd-siax/LICENSE.txt, https://openmodeldb.info/models/4x-NMKD-Siax-CX |
| 4x-NMKD-Superscale (SP_178000_G) (models-nmkd-superscale). Fine-tuned from ESRGAN 4x. Converted from PyTorch to ncnn (fp16) | Nmkd | WTFPL | models-nmkd-superscale/LICENSE.txt, https://openmodeldb.info/models/4x-NMKD-Superscale |
| ESRGAN: the pretrained weights the two NMKD models start from, and RRDB_PSNR_x4 blended into NMKD Siax for its detail levels (models-nmkd-siax/x4-detail*) | Xintao Wang | Apache-2.0 | licenses/ESRGAN-Apache-2.0.txt, https://github.com/xinntao/ESRGAN |
| 4xNomos8kSC (models-nomos8ksc). Fine-tuned from RealESRGAN_x4plus. Changes: converted from PyTorch to ncnn (fp16) | Helaman (Philip Hofmann) | CC-BY-4.0 | models-nomos8ksc/LICENSE.txt, https://github.com/Phhofm/models/tree/main/4xNomos8kSC, https://creativecommons.org/licenses/by/4.0/ |
| 4xLSDIRCompact version 3: N3, C3, R3 (models-lsdir-compact). Changes: converted from PyTorch to ncnn (fp16); levels 1 and 2 are C3 / R3 weight interpolations (75/25 like the author's 4xLSDIRCompact3, 50/50) | Helaman (Philip Hofmann) | CC-BY-4.0 | models-lsdir-compact/LICENSE.txt, https://github.com/Phhofm/models/tree/main/4xLSDIRCompact, https://creativecommons.org/licenses/by/4.0/ |

## Software

| component | license | license text |
|---|---|---|
| waifu2x-caffe (lltcggie): GUI design, language files | MIT | licenses/waifu2x-caffe-MIT.txt |
| waifu2x-ncnn-vulkan (nihui): waifu2x engine | MIT | licenses/waifu2x-ncnn-vulkan-MIT.txt |
| realcugan-ncnn-vulkan (nihui): Real-CUGAN engine, cli/realcugan.* | MIT | licenses/realcugan-ncnn-vulkan-MIT.txt |
| Real-ESRGAN-ncnn-vulkan (Xintao Wang): Real-ESRGAN engine, cli/realesrgan.* | MIT | licenses/Real-ESRGAN-ncnn-vulkan-MIT.txt |
| ncnn (Tencent) | BSD-3-Clause | licenses/ncnn-BSD-3-Clause.txt |
| glslang (inside ncnn) | see file | licenses/glslang.txt |
| libwebp (Google) | BSD-3-Clause | licenses/libwebp-BSD-3-Clause.txt, licenses/libwebp-PATENTS.txt |
| openjpeg | BSD-2-Clause | licenses/openjpeg-BSD-2-Clause.txt |
| zlib-ng | zlib | licenses/zlib-ng-zlib.txt |
| Dear ImGui (Omar Cornut) | MIT | licenses/imgui-MIT.txt |
| GLFW | zlib | licenses/glfw-zlib.txt |
| stb_image, stb_image_write, stb_image_resize2 (Sean Barrett) | MIT or public domain | licenses/stb-MIT-or-PublicDomain.txt |
| Noto Sans, Noto Sans JP / SC / TC / KR (UI fonts in fonts/, static Regular instances of the variable fonts) | SIL Open Font License 1.1 | licenses/NotoSans-OFL-1.1.txt, fonts/OFL.txt |
| Roboto Medium (Christian Robertson / Google), embedded fallback UI font (gui/roboto_medium_ttf.h) | Apache-2.0 | licenses/Roboto-Apache-2.0.txt |
| X.Org headers (build time only, not in the program) | MIT | third_party/x11-include |
