// model folder description for repicturer-cli (enhanced waifu2x-ncnn-vulkan)
//
// three engines share one pipeline:
//   waifu2x    : noise{N}_model (1x), noise{N}_scale2.0x_model, scale2.0x_model
//   realcugan  : up{S}x-conservative, up{S}x-no-denoise, up{S}x-denoise{N}x      (S = 2/3/4)
//   realesrgan : x{S}, optionally x{S}-denoise{N} per denoise level              (S = 2/3/4, N = 0..3)
//                or x{S}-detail{N} per detail level (N = 0..2; level 3 = x{S})
//
// the engine is taken from model.ini (engine=...) or from the folder name like upstream
// (models-cunet, models-realcugan-se, models-realesrgan-x4plus ...). model.ini may also set
// name / prepadding / tile policy, which makes custom models usable.

#ifndef MODEL_INFO_H
#define MODEL_INFO_H

#include <string>
#include <stdlib.h>
#include "fsutil.h"

enum Engine
{
    ENGINE_WAIFU2X = 0,
    ENGINE_REALCUGAN = 1,
    ENGINE_REALESRGAN = 2,
};

enum TilePolicy
{
    TILE_POLICY_CUNET = 0,
    TILE_POLICY_UPCONV = 1,
    TILE_POLICY_REALCUGAN = 2,
    TILE_POLICY_REALESRGAN = 3,
};

struct ModelInfo
{
    path_t dir;
    std::string name;            // short name used in output file names
    int engine = ENGINE_WAIFU2X;
    int prepadding_noise = 0;    // waifu2x noise{N}_model (1x)
    int prepadding_scale = 0;    // waifu2x 2x networks / realesrgan
    int tile_policy = TILE_POLICY_UPCONV;
    int syncgap = 3;             // realcugan sync gap mode (0 = off)
    bool known = false;
};

static inline bool model_file_exists(const path_t& dir, const path_t& base)
{
    return path_exists(path_join(dir, base + PATHSTR(".param"))) && path_exists(path_join(dir, base + PATHSTR(".bin")));
}

static inline path_t to_path_str(const std::string& s)
{
    return utf8_to_path(s);
}

static inline path_t noise_model_base(int noise) { return to_path_str("noise" + std::to_string(noise) + "_model"); }
static inline path_t noise_scale_model_base(int noise) { return to_path_str("noise" + std::to_string(noise) + "_scale2.0x_model"); }
static inline path_t scale_model_base() { return PATHSTR("scale2.0x_model"); }

// realcugan: variant -2 = no-denoise, -1 = conservative, 1..3 = denoise level
static inline path_t realcugan_model_base(int scale, int variant)
{
    std::string v = variant == -2 ? "no-denoise" : variant == -1 ? "conservative" : "denoise" + std::to_string(variant) + "x";
    return to_path_str("up" + std::to_string(scale) + "x-" + v);
}

static inline path_t realesrgan_model_base(int scale)
{
    return to_path_str("x" + std::to_string(scale));
}

// a realesrgan model with a denoise strength per level (realesr-general-x4v3: blends of the general / wdn weights)
static inline path_t realesrgan_denoise_model_base(int scale, int noise)
{
    return to_path_str("x" + std::to_string(scale) + "-denoise" + std::to_string(noise));
}

// a realesrgan model with detail levels (NMKD Siax blended with its starting model): used in every mode
static inline path_t realesrgan_detail_model_base(int scale, int level)
{
    return to_path_str("x" + std::to_string(scale) + "-detail" + std::to_string(level));
}

static inline int realcugan_prepadding(int scale)
{
    return scale == 2 ? 18 : scale == 3 ? 14 : 19;
}

static inline void model_info_apply_ini(ModelInfo& mi)
{
    std::vector<unsigned char> data;
    if (!read_file(path_join(mi.dir, PATHSTR("model.ini")), data)) return;
    std::string s(data.begin(), data.end());
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        if (k == "name") mi.name = v;
        else if (k == "engine")
        {
            if (v == "realcugan") { mi.engine = ENGINE_REALCUGAN; mi.tile_policy = TILE_POLICY_REALCUGAN; }
            else if (v == "realesrgan") { mi.engine = ENGINE_REALESRGAN; mi.tile_policy = TILE_POLICY_REALESRGAN; mi.prepadding_scale = 10; }
            else mi.engine = ENGINE_WAIFU2X;
        }
        else if (k == "prepadding_noise") mi.prepadding_noise = atoi(v.c_str());
        else if (k == "prepadding_scale" || k == "prepadding") mi.prepadding_scale = atoi(v.c_str());
        else if (k == "tile_policy") mi.tile_policy = v == "cunet" ? TILE_POLICY_CUNET : v == "realcugan" ? TILE_POLICY_REALCUGAN : v == "realesrgan" ? TILE_POLICY_REALESRGAN : TILE_POLICY_UPCONV;
        else if (k == "syncgap") mi.syncgap = atoi(v.c_str());
        mi.known = true;
    }
}

static inline ModelInfo get_model_info(const path_t& dir)
{
    ModelInfo mi;
    mi.dir = dir;
    const std::string n = path_to_utf8(path_filename(dir));
    struct Builtin { const char* key; const char* name; int engine; int pn; int ps; int tp; };
    // longer keys first
    static const Builtin builtins[] = {
        {"models-upconv_7_anime_style_art_rgb", "UpRGB", ENGINE_WAIFU2X, 7, 7, TILE_POLICY_UPCONV},
        {"models-upconv_7_photo", "UpPhoto", ENGINE_WAIFU2X, 7, 7, TILE_POLICY_UPCONV},
        {"models-cunet", "CUnet", ENGINE_WAIFU2X, 28, 18, TILE_POLICY_CUNET},
        {"models-upresnet10", "UpResNet10", ENGINE_WAIFU2X, 13, 13, TILE_POLICY_CUNET},
        {"models-anime_style_art_rgb", "RGB", ENGINE_WAIFU2X, 7, 4, TILE_POLICY_CUNET},
        {"models-anime_style_art", "Y", ENGINE_WAIFU2X, 7, 4, TILE_POLICY_CUNET},
        {"models-photo", "Photo", ENGINE_WAIFU2X, 7, 4, TILE_POLICY_CUNET},
        {"models-ukbench", "ukbench", ENGINE_WAIFU2X, 7, 4, TILE_POLICY_CUNET},
        {"models-realcugan-se", "RealCUGAN-SE", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
        {"models-realcugan-pro", "RealCUGAN-Pro", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
        {"models-realcugan-nose", "RealCUGAN-NoSE", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
        {"models-realesrgan-x4plus-anime", "RealESRGAN-x4plus-anime", ENGINE_REALESRGAN, 0, 10, TILE_POLICY_REALESRGAN},
        {"models-realesrgan-x4plus", "RealESRGAN-x4plus", ENGINE_REALESRGAN, 0, 10, TILE_POLICY_REALESRGAN},
        {"models-realesr-animevideov3", "RealESR-animevideov3", ENGINE_REALESRGAN, 0, 10, TILE_POLICY_REALESRGAN},
        // upstream style folder names
        {"models-se", "RealCUGAN-SE", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
        {"models-pro", "RealCUGAN-Pro", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
        {"models-nose", "RealCUGAN-NoSE", ENGINE_REALCUGAN, 0, 0, TILE_POLICY_REALCUGAN},
    };
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
    {
        if (n.find(builtins[i].key) != std::string::npos)
        {
            mi.name = builtins[i].name;
            mi.engine = builtins[i].engine;
            mi.prepadding_noise = builtins[i].pn;
            mi.prepadding_scale = builtins[i].ps;
            mi.tile_policy = builtins[i].tp;
            mi.known = true;
            break;
        }
    }
    // realcugan "nose" models have no sync gap branch
    if (mi.engine == ENGINE_REALCUGAN && n.find("nose") != std::string::npos)
        mi.syncgap = 0;
    model_info_apply_ini(mi);
    if (mi.name.empty()) mi.name = n;
    return mi;
}

#endif // MODEL_INFO_H
