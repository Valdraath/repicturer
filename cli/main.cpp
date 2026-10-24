// waifu2x implemented with ncnn library
//
// enhanced command line: everything upstream waifu2x-ncnn-vulkan does, plus the waifu2x-caffe features
//   - any scale ratio (-s 1.6), target width / height / width x height (-W / -H)
//   - conversion modes noise / scale / noise_scale / auto_scale (denoise jpeg sources only)
//   - denoise-only with models that only ship noise+scale networks (2x then shrink, like caffe)
//   - output formats png bmp jpg jp2 sr tif hdr exr ppm webp tga gif (8 bit), quality
//   - input extension filter, recursive folders keeping the tree, file lists, no-overwrite
//   - --progress machine readable output and --list-gpu for front ends

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <clocale>

#if _WIN32
#include <windows.h>
#include <io.h>
#endif

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"

#include "waifu2x.h"
#include "realcugan.h"
#include "realesrgan.h"
int realcugan_take_error(); // realcugan.cpp
#include "fsutil.h"
#include "image_io.h"
#include "anim_io.h"
#include "model_info.h"
#include "../common/formats.h"

#ifndef W2X_VERSION
#define W2X_VERSION "dev"
#endif

// ============================================================================ output helpers (utf-8)

static std::mutex g_print_lock;

static void out_line(FILE* fp, const std::string& s)
{
    std::lock_guard<std::mutex> g(g_print_lock);
    fwrite(s.data(), 1, s.size(), fp);
    fputc('\n', fp);
    fflush(fp);
}

static std::string P(const path_t& p) { return path_to_utf8(p); }

static void print_usage()
{
    const char* u =
        "repicturer-cli " W2X_VERSION " (RePicturer converter: waifu2x + Real-CUGAN + Real-ESRGAN, based on waifu2x-ncnn-vulkan)\n"
        "Usage: repicturer-cli -i infile -o outfile [options]...\n"
        "\n"
        "  -h, --help                show this help\n"
        "  -v, --verbose             verbose output\n"
        "  -i, --input path          input image path or directory\n"
        "  -o, --output path         output image path or directory\n"
        "  -n, --noise_level n       denoise level (-1/0/1/2/3, default=0)\n"
        "  -s, --scale_ratio r       upscale ratio, any positive number (default=2)\n"
        "  -W, --scale_width w       target width (height follows the aspect ratio unless -H is set)\n"
        "  -H, --scale_height h      target height (width follows the aspect ratio unless -W is set)\n"
        "  -M, --mode mode           noise / scale / noise_scale / auto_scale (default: from -n and -s)\n"
        "  -t, --tile_size t         tile size (>=32/0=auto, default=0) can be 0,0,0 for multi-gpu\n"
        "  -m, --model_dir path      model folder (default=models-cunet); the engine follows the folder:\n"
        "                              models-cunet, models-upconv_7_*, models-upresnet10 : waifu2x\n"
        "                              models-realcugan-se / -pro / -nose                  : Real-CUGAN\n"
        "                              models-realesrgan-x4plus(-anime), models-realesr-*, models-realesrnet-x4plus,\n"
        "                              models-nomos8ksc, models-nmkd-siax / -superscale (model.ini)  : Real-ESRGAN\n"
        "                            Real-CUGAN: -n 0 = conservative, 1-3 = denoise, -1 = no denoise\n"
        "                            Real-ESRGAN: -n picks the denoise strength when the model has x4-denoise0..3\n"
        "                            (realesr-general-x4v3) or the detail level 0-3 when it has x4-detail0..2 (NMKD Siax);\n"
        "                            otherwise it is ignored (the network restores by itself)\n"
        "  -c, --syncgap n           Real-CUGAN sync gap mode 0-3 (default=3, 0 = off)\n"
        "  -g, --gpu id              gpu device to use (-1=cpu, default=auto) can be 0,1,2 for multi-gpu\n"
        "  -j, --jobs l:p:s          thread count for load/proc/save (default=1:2:2) can be 1:2,2,2:2 for multi-gpu\n"
        "  -x, --tta                 enable tta mode\n"
        "  -f, --format fmt          output format png/jpg/webp/bmp/jp2/sr/tif/hdr/exr/ppm/tga/gif (default=ext/png)\n"
        "                            animated gif / webp inputs stay animated (saved as .gif / .webp)\n"
        "  -q, --output_quality q    jpg 0-100 (100), webp 1-100 or 101=lossless (101), tga 0/1 = rle off/on\n"
        "  -l, --input_extension_list list  extensions to read from a directory, e.g. png:jpg:jpeg:tif:tiff:bmp:tga\n"
        "  -r, --recursive           also convert sub directories, keeping the folder structure\n"
        "  -k, --no_overwrite        do not overwrite existing output files\n"
        "  -L, --file_list file      utf-8 text file, one \"input<TAB>output\" pair per line\n"
        "      --list-gpu            print the vulkan devices and exit\n"
        "      --progress            print machine readable progress lines\n";
    fputs(u, stdout);
}

// ============================================================================ options

enum Mode
{
    MODE_NOISE = 0,
    MODE_SCALE = 1,
    MODE_NOISE_SCALE = 2,
    MODE_AUTO_SCALE = 3,
};

struct Options
{
    path_t inputpath;
    path_t outputpath;
    path_t filelist;
    int noise = 0;
    double scale = 2.0;
    int scale_width = 0;
    int scale_height = 0;
    int mode = -1;
    std::vector<int> tilesize;
    path_t model = PATHSTR("models-cunet");
    std::vector<int> gpuid;
    int jobs_load = 1;
    std::vector<int> jobs_proc;
    int jobs_save = 2;
    bool verbose = false;
    bool tta = false;
    std::string format = "png";
    bool format_set = false;
    int quality = -1;
    std::vector<path_t> extlist;
    bool recursive = false;
    bool no_overwrite = false;
    bool list_gpu = false;
    bool progress = false;
    int syncgap = -1;
};

static std::vector<int> parse_int_array(const std::string& s)
{
    std::vector<int> v;
    size_t pos = 0;
    while (pos <= s.size())
    {
        size_t c = s.find(',', pos);
        if (c == std::string::npos) c = s.size();
        v.push_back(atoi(s.substr(pos, c - pos).c_str()));
        pos = c + 1;
    }
    return v;
}

static bool parse_number(const std::string& s, double& d)
{
    if (s.empty()) return false;
    char* end = 0;
    d = strtod(s.c_str(), &end);
    return end && *end == 0;
}

static bool parse_int(const std::string& s, int& v)
{
    if (s.empty()) return false;
    char* end = 0;
    long l = strtol(s.c_str(), &end, 10);
    if (!end || *end != 0) return false;
    v = (int)l;
    return true;
}

// returns 0 ok, 1 help, -1 error
static int parse_args(const std::vector<path_t>& args, Options& o)
{
    struct Spec { char s; const char* l; bool arg; };
    static const Spec specs[] = {
        {'h', "help", false}, {'v', "verbose", false}, {'i', "input", true}, {'o', "output", true},
        {'n', "noise_level", true}, {'s', "scale_ratio", true}, {'W', "scale_width", true}, {'H', "scale_height", true},
        {'M', "mode", true}, {'t', "tile_size", true}, {'m', "model_dir", true}, {'g', "gpu", true},
        {'j', "jobs", true}, {'x', "tta", false}, {'f', "format", true}, {'q', "output_quality", true},
        {'l', "input_extension_list", true}, {'r', "recursive", false},
        {'k', "no_overwrite", false}, {'L', "file_list", true}, {0, "list-gpu", false}, {0, "progress", false},
        {0, "output_extention", true}, {0, "input_extention_list", true}, {'c', "syncgap", true},
    };
    const int nspec = (int)(sizeof(specs) / sizeof(specs[0]));

    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string a = P(args[i]);
        if (a.size() < 2 || a[0] != '-')
        {
            fprintf(stderr, "unexpected argument %s\n", a.c_str());
            return -1;
        }
        const Spec* sp = 0;
        std::string val;
        bool has_inline = false;
        if (a[1] == '-')
        {
            std::string name = a.substr(2);
            size_t eq = name.find('=');
            if (eq != std::string::npos) { val = name.substr(eq + 1); name = name.substr(0, eq); has_inline = true; }
            for (int k = 0; k < nspec; k++) if (name == specs[k].l) sp = &specs[k];
        }
        else if (a.size() == 2)
        {
            for (int k = 0; k < nspec; k++) if (specs[k].s && a[1] == specs[k].s) sp = &specs[k];
        }
        if (!sp)
        {
            fprintf(stderr, "unknown option %s\n", a.c_str());
            return -1;
        }
        path_t pval;
        if (sp->arg)
        {
            if (has_inline) pval = utf8_to_path(val);
            else
            {
                if (i + 1 >= args.size()) { fprintf(stderr, "option %s needs a value\n", a.c_str()); return -1; }
                pval = args[++i];
                val = P(pval);
            }
        }

        const std::string l = sp->l;
        if (l == "help") return 1;
        else if (l == "verbose") o.verbose = true;
        else if (l == "input") o.inputpath = pval;
        else if (l == "output") o.outputpath = pval;
        else if (l == "noise_level") { if (!parse_int(val, o.noise)) { fprintf(stderr, "invalid noise argument\n"); return -1; } }
        else if (l == "scale_ratio") { if (!parse_number(val, o.scale) || !(o.scale > 0)) { fprintf(stderr, "invalid scale argument\n"); return -1; } }
        else if (l == "scale_width") { if (!parse_int(val, o.scale_width) || o.scale_width <= 0) { fprintf(stderr, "invalid scale_width argument\n"); return -1; } }
        else if (l == "scale_height") { if (!parse_int(val, o.scale_height) || o.scale_height <= 0) { fprintf(stderr, "invalid scale_height argument\n"); return -1; } }
        else if (l == "mode")
        {
            if (val == "noise") o.mode = MODE_NOISE;
            else if (val == "scale") o.mode = MODE_SCALE;
            else if (val == "noise_scale") o.mode = MODE_NOISE_SCALE;
            else if (val == "auto_scale") o.mode = MODE_AUTO_SCALE;
            else { fprintf(stderr, "invalid mode argument\n"); return -1; }
        }
        else if (l == "tile_size") o.tilesize = parse_int_array(val);
        else if (l == "model_dir") o.model = pval;
        else if (l == "gpu") o.gpuid = parse_int_array(val);
        else if (l == "jobs")
        {
            size_t c1 = val.find(':'), c2 = val.rfind(':');
            if (c1 == std::string::npos || c2 == c1) { fprintf(stderr, "invalid thread count argument\n"); return -1; }
            o.jobs_load = atoi(val.substr(0, c1).c_str());
            o.jobs_proc = parse_int_array(val.substr(c1 + 1, c2 - c1 - 1));
            o.jobs_save = atoi(val.substr(c2 + 1).c_str());
        }
        else if (l == "tta") o.tta = true;
        else if (l == "format" || l == "output_extention") { o.format = val; o.format_set = true; }
        else if (l == "output_quality") { if (!parse_int(val, o.quality)) { fprintf(stderr, "invalid output_quality argument\n"); return -1; } }
        else if (l == "input_extension_list" || l == "input_extention_list")
        {
            o.extlist.clear();
            size_t pos = 0;
            while (pos <= val.size())
            {
                size_t c = val.find(':', pos);
                if (c == std::string::npos) c = val.size();
                std::string e = val.substr(pos, c - pos);
                if (!e.empty() && e[0] == '.') e.erase(0, 1);
                if (!e.empty()) o.extlist.push_back(to_lower(utf8_to_path(e)));
                pos = c + 1;
            }
        }
        else if (l == "recursive") o.recursive = true;
        else if (l == "no_overwrite") o.no_overwrite = true;
        else if (l == "file_list") o.filelist = pval;
        else if (l == "list-gpu") o.list_gpu = true;
        else if (l == "progress") o.progress = true;
        else if (l == "syncgap")
        {
            if (!parse_int(val, o.syncgap) || o.syncgap < 0 || o.syncgap > 3) { fprintf(stderr, "invalid syncgap argument\n"); return -1; }
        }
    }
    return 0;
}

// ============================================================================ queue

template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(size_t cap) : capacity(cap) {}

    void put(T&& v)
    {
        std::unique_lock<std::mutex> lk(m);
        not_full.wait(lk, [&] { return q.size() < capacity; });
        q.push_back(std::move(v));
        not_empty.notify_one();
    }

    T get()
    {
        std::unique_lock<std::mutex> lk(m);
        not_empty.wait(lk, [&] { return !q.empty(); });
        T v = std::move(q.front());
        q.pop_front();
        not_full.notify_one();
        return v;
    }

private:
    size_t capacity;
    std::deque<T> q;
    std::mutex m;
    std::condition_variable not_full, not_empty;
};

struct Task
{
    int id = -1; // -233 terminates a worker like upstream
    path_t inpath;
    path_t outpath;
    std::string format;
    Image8 image;
    DecodeInfo info;
    OutImage out;
    std::unique_ptr<AnimFrames> anim;     // animated gif / webp: all frames
    std::vector<unsigned char> encoded;   // animation already encoded by the proc worker
    bool userFormat = true;               // false when the format was switched to keep an animation
};

// ============================================================================ processing plan (caffe ReconstructImage, generalized)

// one network pass
struct NetSpec
{
    path_t base;     // model file name without extension
    int scale = 2;   // 1 / 2 / 3 / 4
    int noise = 0;   // value for the engine's "noise" field
};

struct Plan
{
    std::vector<NetSpec> nets;
    int outw = 0;
    int outh = 0;
    std::string error;
};

static Options g_opt;
static ModelInfo g_model;

// which networks exist in the model folder (cached file checks)
struct ModelCaps
{
    // waifu2x
    bool noise = false;
    bool noise_scale = false;
    bool scale = false;
    // realcugan [scale][variant+2] variant -2..3 ; realesrgan [scale][0]
    bool has[5][6] = {};
    // realesrgan with one network per denoise level [scale][noise]
    bool esrNoise[5][4] = {};
    // realesrgan with detail levels [scale][level 0..2] (level 3 = the plain network)
    bool esrDetail[5][3] = {};

    bool cugan(int s, int variant) const { return s >= 2 && s <= 4 && variant >= -2 && variant <= 3 && has[s][variant + 2]; }
};
static ModelCaps g_caps;

static void scan_caps(const Options& o)
{
    const path_t& dir = g_model.dir;
    if (g_model.engine == ENGINE_WAIFU2X)
    {
        if (o.noise >= 0)
        {
            g_caps.noise = model_file_exists(dir, noise_model_base(o.noise));
            g_caps.noise_scale = model_file_exists(dir, noise_scale_model_base(o.noise));
        }
        g_caps.scale = model_file_exists(dir, scale_model_base());
    }
    else if (g_model.engine == ENGINE_REALCUGAN)
    {
        for (int s = 2; s <= 4; s++)
            for (int v = -2; v <= 3; v++)
                if (v != 0) g_caps.has[s][v + 2] = model_file_exists(dir, realcugan_model_base(s, v));
    }
    else
    {
        for (int s = 2; s <= 4; s++)
        {
            g_caps.has[s][0] = model_file_exists(dir, realesrgan_model_base(s));
            for (int n = 0; n < 4; n++) g_caps.esrNoise[s][n] = model_file_exists(dir, realesrgan_denoise_model_base(s, n));
            for (int n = 0; n < 3; n++) g_caps.esrDetail[s][n] = model_file_exists(dir, realesrgan_detail_model_base(s, n));
        }
    }
}

static NetSpec cugan_spec(int s, int variant)
{
    NetSpec n;
    n.base = realcugan_model_base(s, variant);
    n.scale = s;
    // realcugan field: -1 conservative, 0 no-denoise, 1..3 denoise
    n.noise = variant == -2 ? 0 : variant;
    return n;
}

// noise >= 0: the network for that denoise level when the model has one
static NetSpec esrgan_spec(int s, int noise = -1)
{
    NetSpec n;
    n.base = noise >= 0 && noise < 4 && g_caps.esrNoise[s][noise] ? realesrgan_denoise_model_base(s, noise) : realesrgan_model_base(s);
    // detail levels apply to every pass in every mode (-n = level)
    if (g_caps.esrDetail[s][0] && g_opt.noise >= 0 && g_opt.noise < 3) n.base = realesrgan_detail_model_base(s, g_opt.noise);
    n.scale = s;
    n.noise = 0;
    return n;
}

// smallest available scale >= need, else the largest one; 0 if none
template<typename F>
static int pick_scale(double need, F avail)
{
    int best = 0, largest = 0;
    for (int s = 2; s <= 4; s++)
    {
        if (!avail(s)) continue;
        largest = s;
        if (best == 0 && s + 1e-9 >= need) best = s;
    }
    return best ? best : largest;
}

static Plan make_plan(const Options& o, int w, int h, bool is_jpeg)
{
    Plan p;
    const bool is_noise = o.mode == MODE_NOISE || o.mode == MODE_NOISE_SCALE || (o.mode == MODE_AUTO_SCALE && is_jpeg);
    const bool is_scale = o.mode == MODE_SCALE || o.mode == MODE_NOISE_SCALE || o.mode == MODE_AUTO_SCALE;

    double factor = 1.0;
    if (is_scale)
    {
        if (o.scale_width > 0 && o.scale_height > 0)
            factor = std::max((double)o.scale_width / w, (double)o.scale_height / h);
        else if (o.scale_width > 0)
            factor = (double)o.scale_width / w;
        else if (o.scale_height > 0)
            factor = (double)o.scale_height / h;
        else
            factor = o.scale;
    }

    double remaining = factor;

    if (g_model.engine == ENGINE_WAIFU2X)
    {
        if (is_noise)
        {
            // caffe: the noise+scale network is used whenever the model has one, except in denoise-only mode
            // when a pure denoise network exists
            const bool use_noise_scale = g_caps.noise_scale && !(o.mode == MODE_NOISE && g_caps.noise);
            NetSpec n;
            n.noise = o.noise;
            if (use_noise_scale)
            {
                n.base = noise_scale_model_base(o.noise);
                n.scale = 2;
                remaining /= 2.0;
            }
            else if (g_caps.noise)
            {
                n.base = noise_model_base(o.noise);
                n.scale = 1;
            }
            else
            {
                p.error = "the model has no denoise network for noise level " + std::to_string(o.noise);
                return p;
            }
            p.nets.push_back(n);
        }
        if (is_scale)
        {
            int cnt = (int)ceil(log(remaining) / log(2.0) - 1e-9);
            // 1x or less: still one scaling pass (shrunk back) unless the denoise pass already scaled
            bool scaled = false;
            for (const auto& n : p.nets) if (n.scale > 1) scaled = true;
            if (cnt < 1 && !scaled) cnt = 1;
            for (int i = 0; i < cnt; i++)
            {
                if (!g_caps.scale)
                {
                    p.error = "the model has no scale2.0x network";
                    return p;
                }
                NetSpec n;
                n.base = scale_model_base();
                n.scale = 2;
                n.noise = -1;
                p.nets.push_back(n);
            }
        }
    }
    else if (g_model.engine == ENGINE_REALCUGAN)
    {
        // first pass carries the denoise level (level 0 = conservative), later passes use no-denoise
        if (is_noise)
        {
            const int variant = o.noise == 0 ? -1 : o.noise;
            const double need = is_scale ? remaining : 0.0;
            const int s = pick_scale(need, [&](int x) { return g_caps.cugan(x, variant); });
            if (!s)
            {
                p.error = std::string("this Real-CUGAN model has no ") + (variant == -1 ? "conservative" : "denoise" + std::to_string(variant) + "x") + " network";
                return p;
            }
            p.nets.push_back(cugan_spec(s, variant));
            remaining /= s;
        }
        if (is_scale)
        {
            while (remaining > 1.0 + 1e-9)
            {
                const int s = pick_scale(remaining, [&](int x) { return g_caps.cugan(x, -2); });
                if (!s)
                {
                    p.error = "this Real-CUGAN model has no no-denoise network";
                    return p;
                }
                p.nets.push_back(cugan_spec(s, -2));
                remaining /= s;
            }
            // 1x or less: still one scaling pass (shrunk back)
            if (p.nets.empty())
            {
                const int s = pick_scale(0.0, [&](int x) { return g_caps.cugan(x, -2); });
                if (!s)
                {
                    p.error = "this Real-CUGAN model has no no-denoise network";
                    return p;
                }
                p.nets.push_back(cugan_spec(s, -2));
            }
        }
    }
    else
    {
        // Real-ESRGAN denoises / restores by itself: denoise-only = one pass shrunk back to 1x.
        // a model with a network per denoise level uses it for the first pass when denoise is asked
        int noise = is_noise ? o.noise : -1;
        if (is_noise && !is_scale)
        {
            const int s = pick_scale(0.0, [&](int x) { return g_caps.has[x][0]; });
            if (!s) { p.error = "the model folder has no x2/x3/x4 network"; return p; }
            p.nets.push_back(esrgan_spec(s, noise));
        }
        if (is_scale)
        {
            while (remaining > 1.0 + 1e-9)
            {
                const int s = pick_scale(remaining, [&](int x) { return g_caps.has[x][0]; });
                if (!s) { p.error = "the model folder has no x2/x3/x4 network"; return p; }
                p.nets.push_back(esrgan_spec(s, noise));
                noise = -1;
                remaining /= s;
            }
            // scale 1x or less: one pass shrunk back (the network still cleans up, like denoise-only)
            if (p.nets.empty())
            {
                const int s = pick_scale(0.0, [&](int x) { return g_caps.has[x][0]; });
                if (!s) { p.error = "the model folder has no x2/x3/x4 network"; return p; }
                p.nets.push_back(esrgan_spec(s, noise));
            }
        }
    }

    if (is_scale && o.scale_width > 0 && o.scale_height > 0)
    {
        p.outw = o.scale_width;
        p.outh = o.scale_height;
    }
    else if (is_scale && o.scale_width > 0)
    {
        p.outw = o.scale_width;
        p.outh = std::max(1, (int)((double)h * o.scale_width / w + 1e-7));
    }
    else if (is_scale && o.scale_height > 0)
    {
        p.outh = o.scale_height;
        p.outw = std::max(1, (int)((double)w * o.scale_height / h + 1e-7));
    }
    else
    {
        p.outw = std::max(1, (int)(w * factor + 1e-7));
        p.outh = std::max(1, (int)(h * factor + 1e-7));
    }
    return p;
}

// ============================================================================ networks (loaded on first use, per device)

class Processor
{
public:
    virtual ~Processor() {}
    virtual int process(const ncnn::Mat& in, ncnn::Mat& out) const = 0;
};

template<typename T>
class ProcessorT : public Processor
{
public:
    explicit ProcessorT(T* n) : net(n) {}
    ~ProcessorT() { delete net; }
    int process(const ncnn::Mat& in, ncnn::Mat& out) const { return net->process(in, out); }
    T* net;
};

struct GpuNets
{
    int gpuid = 0;
    int tilesize = 0;        // 0 = auto per network
    int num_threads = 1;
    uint32_t heap_budget = 0;
    uint32_t heap_total = 0;     // whole budget (Real-ESRGAN runs one image at a time per gpu)
    std::mutex run_lock;         // serializes Real-ESRGAN inference on this gpu
    std::mutex lock;
    std::map<path_t, Processor*> nets;
    std::map<path_t, bool> failed;
};

static int auto_tilesize(const GpuNets& g, int scale)
{
    if (g.gpuid == -1) return 400;
    // Real-ESRGAN is serialized per gpu, so it gets the whole memory budget
    const uint32_t hb = g_model.engine == ENGINE_REALESRGAN && g.heap_total ? g.heap_total : g.heap_budget;
    switch (g_model.tile_policy)
    {
    case TILE_POLICY_CUNET:
        return hb > 2600 ? 400 : hb > 740 ? 200 : hb > 250 ? 100 : 32;
    case TILE_POLICY_REALCUGAN:
        if (scale == 3) return hb > 3300 ? 400 : hb > 1900 ? 300 : hb > 950 ? 200 : hb > 320 ? 100 : 32;
        if (scale == 4) return hb > 1690 ? 400 : hb > 980 ? 300 : hb > 530 ? 200 : hb > 240 ? 100 : 32;
        return hb > 1300 ? 400 : hb > 800 ? 300 : hb > 400 ? 200 : hb > 200 ? 100 : 32;
    case TILE_POLICY_REALESRGAN:
        return hb > 1900 ? 200 : hb > 550 ? 100 : hb > 190 ? 64 : 32;
    default:
        return hb > 1900 ? 400 : hb > 550 ? 200 : hb > 190 ? 100 : 32;
    }
}

static const Processor* get_net(GpuNets* g, const NetSpec& spec, std::string& err)
{
    std::lock_guard<std::mutex> guard(g->lock);
    auto it = g->nets.find(spec.base);
    if (it != g->nets.end()) return it->second;
    if (g->failed.count(spec.base))
    {
        err = "failed to load " + P(spec.base);
        return 0;
    }

    const path_t param = path_join(g_model.dir, spec.base + PATHSTR(".param"));
    const path_t bin = path_join(g_model.dir, spec.base + PATHSTR(".bin"));
    const int tile = g->tilesize > 0 ? g->tilesize : auto_tilesize(*g, spec.scale);
    Processor* proc = 0;

    if (g_model.engine == ENGINE_WAIFU2X)
    {
        Waifu2x* w = new Waifu2x(g->gpuid, g_opt.tta, g->num_threads);
        if (w->load(param, bin) == 0)
        {
            w->noise = spec.noise;
            w->scale = spec.scale;
            w->tilesize = tile;
            w->prepadding = spec.scale == 1 ? g_model.prepadding_noise : g_model.prepadding_scale;
            proc = new ProcessorT<Waifu2x>(w);
        }
        else
            delete w;
    }
    else if (g_model.engine == ENGINE_REALCUGAN)
    {
        RealCUGAN* c = new RealCUGAN(g->gpuid, g_opt.tta, g->num_threads);
        if (c->load(param, bin) == 0)
        {
            c->noise = spec.noise;
            c->scale = spec.scale;
            c->tilesize = tile;
            c->prepadding = realcugan_prepadding(spec.scale);
            c->syncgap = g_opt.syncgap >= 0 && g_model.syncgap != 0 ? g_opt.syncgap : g_model.syncgap;
            proc = new ProcessorT<RealCUGAN>(c);
        }
        else
            delete c;
    }
    else
    {
        RealESRGAN* r = new RealESRGAN(g->gpuid, g_opt.tta, g->num_threads);
        if (r->load(param, bin) == 0)
        {
            r->scale = spec.scale;
            r->tilesize = tile;
            r->prepadding = g_model.prepadding_scale > 0 ? g_model.prepadding_scale : 10;
            proc = new ProcessorT<RealESRGAN>(r);
        }
        else
            delete r;
    }

    if (!proc)
    {
        g->failed[spec.base] = true;
        err = "failed to load model " + P(param);
        out_line(stderr, err);
        return 0;
    }
    if (g_opt.verbose)
        out_line(stderr, "loaded " + P(spec.base) + " (tile " + std::to_string(tile) + ")");
    g->nets[spec.base] = proc;
    return proc;
}

// ============================================================================ shared state

static std::atomic<int> g_done(0);
static std::atomic<int> g_failed(0);
static int g_total = 0;

static void report_done()
{
    int d = ++g_done;
    if (g_opt.progress)
        out_line(stdout, "PROGRESS\t" + std::to_string(d) + "\t" + std::to_string(g_total));
}

static void report_fail(const char* kind, const path_t& in, const path_t& out, const std::string& detail)
{
    g_failed++;
    if (g_opt.progress)
        out_line(stdout, std::string("FAIL\t") + kind + "\t" + P(in) + "\t" + P(out) + "\t" + detail);
    out_line(stderr, std::string(kind) + " failed: " + P(in) + " -> " + P(out) + (detail.empty() ? "" : " : " + detail));
    report_done();
}

static BlockingQueue<Task>* g_toproc = 0;
static BlockingQueue<Task>* g_tosave = 0;

// ============================================================================ threads

static void load_worker(const std::vector<path_t>* inputs, const std::vector<path_t>* outputs, std::atomic<int>* next)
{
    for (;;)
    {
        const int i = (*next)++;
        if (i >= (int)inputs->size()) break;
        Task t;
        t.id = i;
        t.inpath = (*inputs)[i];
        t.outpath = (*outputs)[i];

        path_t ext = to_lower(get_file_extension(t.outpath));
        const OutputFormatInfo* fi = find_output_format(P(ext).c_str());
        t.format = fi ? fi->ext : g_opt.format;

        if (g_opt.no_overwrite && path_exists(t.outpath))
        {
            if (g_opt.progress)
                out_line(stdout, "SKIP\t" + P(t.inpath) + "\t" + P(t.outpath));
            else
                out_line(stderr, "skip existing " + P(t.outpath));
            report_done();
            continue;
        }

        std::vector<unsigned char> filedata;
        if (!read_file(t.inpath, filedata))
        {
            report_fail("open", t.inpath, t.outpath, "cannot read file");
            continue;
        }
        std::string err;
        if (anim_is_animated(filedata.data(), filedata.size()))
        {
            // animated gif / webp: every frame is converted and the result stays animated
            t.anim.reset(new AnimFrames());
            if (!anim_decode(filedata.data(), filedata.size(), *t.anim, err))
            {
                report_fail("decode", t.inpath, t.outpath, err);
                continue;
            }
            if (t.format != "gif" && t.format != "webp")
            {
                // the chosen format cannot hold an animation: keep the source container
                t.format = t.anim->kind;
                t.userFormat = false;
                t.outpath = get_file_name_without_extension(t.outpath) + PATHSTR(".") + utf8_to_path(t.format);
                if (g_opt.no_overwrite && path_exists(t.outpath))
                {
                    if (g_opt.progress)
                        out_line(stdout, "SKIP\t" + P(t.inpath) + "\t" + P(t.outpath));
                    else
                        out_line(stderr, "skip existing " + P(t.outpath));
                    report_done();
                    continue;
                }
            }
            t.image.w = t.anim->w;
            t.image.h = t.anim->h;
            t.image.c = t.anim->alpha ? 4 : 3;
            const std::string note = std::to_string(t.anim->frames.size()) + " frames";
            if (g_opt.progress)
                out_line(stdout, "ANIM\t" + P(t.inpath) + "\t" + P(t.outpath) + "\t" + note);
            else if (g_opt.verbose)
                out_line(stderr, P(t.inpath) + ": animated, " + note + " -> " + P(t.outpath));
        }
        else if (!image_decode(filedata.data(), filedata.size(), t.image, t.info, err))
        {
            report_fail("decode", t.inpath, t.outpath, err);
            continue;
        }
        g_toproc->put(std::move(t));
    }
}

static int run_net(const Processor* net, int scale, const Image8& in, Image8& out)
{
    ncnn::Mat inimage(in.w, in.h, (void*)in.data.data(), (size_t)in.c, in.c);
    out.w = in.w * scale;
    out.h = in.h * scale;
    out.c = in.c;
    out.data.resize((size_t)out.w * out.h * out.c);
    ncnn::Mat outimage(out.w, out.h, (void*)out.data.data(), (size_t)out.c, out.c);
    return net->process(inimage, outimage);
}

static void proc_worker(GpuNets* g)
{
    for (;;)
    {
        Task t = g_toproc->get();
        if (t.id == -233) break;

        Plan plan = make_plan(g_opt, t.image.w, t.image.h, t.info.is_jpeg);
        if (!plan.error.empty())
        {
            report_fail("process", t.inpath, t.outpath, plan.error);
            continue;
        }

        // report the exact passes so a front end can show that denoise and magnify really both ran
        if (g_opt.progress || g_opt.verbose)
        {
            std::string d;
            for (size_t s = 0; s < plan.nets.size(); s++)
                d += (s ? " -> " : "") + P(plan.nets[s].base) + " (x" + std::to_string(plan.nets[s].scale) + ")";
            if (d.empty()) d = "no network";
            d += " -> resize " + std::to_string(t.image.w) + "x" + std::to_string(t.image.h) + " to " + std::to_string(plan.outw) + "x" + std::to_string(plan.outh);
            if (g_opt.mode == MODE_AUTO_SCALE)
                d += t.info.is_jpeg ? " (jpeg: denoised)" : " (not jpeg: no denoise in auto mode)";
            if (g_opt.progress)
                out_line(stdout, "PLAN\t" + P(t.inpath) + "\t" + d);
            else
                out_line(stderr, P(t.inpath) + ": " + d);
        }

        std::string err;
        auto run_plan = [&](Image8& cur) -> bool {
            for (size_t s = 0; s < plan.nets.size(); s++)
            {
                const Processor* net = get_net(g, plan.nets[s], err);
                if (!net) return false;
                Image8 next;
                int rr;
                if (g_model.engine == ENGINE_REALESRGAN && g->gpuid != -1)
                {
                    // x4plus keeps every layer of a tile in gpu memory: several images at once ran out of vram
                    std::lock_guard<std::mutex> lk(g->run_lock);
                    rr = run_net(net, plan.nets[s].scale, cur, next);
                }
                else
                    rr = run_net(net, plan.nets[s].scale, cur, next);
                if (g_model.engine == ENGINE_REALCUGAN && realcugan_take_error() != 0) rr = -1;
                if (rr != 0) { err = "GPU error (probably out of GPU memory) - use a smaller Split size or fewer Thread(s)"; return false; }
                cur = std::move(next);
            }
            return true;
        };

        if (t.anim)
        {
            // animation: every frame through the same plan, encoded as soon as it is done (8 bit)
            AnimFrames& a = *t.anim;
            std::unique_ptr<AnimEncoder> enc = anim_create_encoder(t.format, plan.outw, plan.outh, t.userFormat ? g_opt.quality : -1, a.loop_count, err);
            bool ok = enc != nullptr;
            const size_t n = a.frames.size();
            for (size_t k = 0; k < n && ok; k++)
            {
                Image8 cur = std::move(a.frames[k]);
                if (!run_plan(cur)) { ok = false; break; }
                OutImage o;
                image_resize_to(cur, plan.outw, plan.outh, o);
                image_alpha_clean(o);
                if (!enc->add(o, a.durations_ms[k], err)) { ok = false; break; }
                if (g_opt.progress)
                    out_line(stdout, "FRAME\t" + P(t.inpath) + "\t" + std::to_string(k + 1) + "\t" + std::to_string(n));
            }
            if (ok) ok = enc->finish(t.encoded, err);
            t.anim.reset();
            if (!ok)
            {
                report_fail(err.find("inference") != std::string::npos || err.find("load") != std::string::npos ? "process" : "encode",
                            t.inpath, t.outpath, err);
                continue;
            }
            g_tosave->put(std::move(t));
            continue;
        }

        Image8 cur = std::move(t.image);
        if (!run_plan(cur))
        {
            report_fail("process", t.inpath, t.outpath, err);
            continue;
        }

        image_resize_to(cur, plan.outw, plan.outh, t.out);
        image_alpha_clean(t.out);
        g_tosave->put(std::move(t));
    }
}

static bool rename_replace(const path_t& from, const path_t& to)
{
#if _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

static void save_worker()
{
    for (;;)
    {
        Task t = g_tosave->get();
        if (t.id == -233) break;

        const OutputFormatInfo* fi = find_output_format(t.format.c_str());
        OutImage& img = t.out;
        const bool preEncoded = !t.encoded.empty(); // animation: already encoded frame by frame
        if (!preEncoded && fi && !fi->alpha && img.c == 4)
        {
            // container without alpha channel: drop it
            const size_t npx = (size_t)img.w * img.h;
            std::vector<unsigned char> d(npx * 3);
            for (size_t i = 0; i < npx; i++) memcpy(&d[i * 3], &img.data[i * 4], 3);
            img.data.swap(d);
            img.c = 3;
        }

        std::vector<unsigned char> encoded;
        std::string err;
        if (preEncoded)
            encoded.swap(t.encoded);
        else if (!image_encode(img, t.format, g_opt.quality, encoded, err))
        {
            report_fail("encode", t.inpath, t.outpath, err);
            continue;
        }

        make_directories(path_parent(t.outpath));
        const path_t tmp = t.outpath + PATHSTR(".w2xtmp");
        if (!write_file(tmp, encoded.data(), encoded.size()) || !rename_replace(tmp, t.outpath))
        {
#if _WIN32
            DeleteFileW(tmp.c_str());
#else
            remove(tmp.c_str());
#endif
            report_fail("write", t.inpath, t.outpath, "cannot write output file");
            continue;
        }

        if (g_opt.verbose)
            out_line(stdout, P(t.inpath) + " -> " + P(t.outpath) + " done");
        report_done();
    }
}

// ============================================================================ input collection

static bool ext_allowed(const path_t& name)
{
    if (g_opt.extlist.empty()) return true;
    const path_t ext = to_lower(get_file_extension(name));
    for (size_t i = 0; i < g_opt.extlist.size(); i++)
        if (g_opt.extlist[i] == ext) return true;
    return false;
}

static bool read_file_list(const path_t& listpath, std::vector<path_t>& in, std::vector<path_t>& out)
{
    std::vector<unsigned char> data;
    if (!read_file(listpath, data)) return false;
    std::string s(data.begin(), data.end());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        in.push_back(utf8_to_path(line.substr(0, tab)));
        out.push_back(utf8_to_path(line.substr(tab + 1)));
    }
    return true;
}

// ============================================================================ main

static int list_gpus()
{
    ncnn::create_gpu_instance();
    int n = ncnn::get_gpu_count();
    for (int i = 0; i < n; i++)
    {
        const ncnn::GpuInfo& gi = ncnn::get_gpu_info(i);
        const char* type = gi.type() == 0 ? "discrete" : gi.type() == 1 ? "integrated" : gi.type() == 2 ? "virtual" : "cpu";
        uint32_t heap = ncnn::get_gpu_device(i)->get_heap_budget();
        out_line(stdout, "GPU\t" + std::to_string(i) + "\t" + gi.device_name() + "\t" + type + "\t" + std::to_string(heap));
    }
    out_line(stdout, "CPU\t" + std::to_string(ncnn::get_cpu_count()));
    ncnn::destroy_gpu_instance();
    return 0;
}

static int run(const std::vector<path_t>& args)
{
    Options& o = g_opt;
    int pr = parse_args(args, o);
    if (pr == 1) { print_usage(); return 0; }
    if (pr != 0) { print_usage(); return -1; }

    if (o.list_gpu) return list_gpus();

    if (o.filelist.empty() && (o.inputpath.empty() || o.outputpath.empty()))
    {
        print_usage();
        return -1;
    }

    if (o.noise < -1 || o.noise > 3)
    {
        fprintf(stderr, "invalid noise argument\n");
        return -1;
    }

    // mode defaults keep upstream semantics: -n -1 means scale only, -s 1 means denoise only
    if (o.mode < 0)
    {
        if (o.noise == -1) o.mode = MODE_SCALE;
        else if (o.scale == 1.0 && o.scale_width == 0 && o.scale_height == 0) o.mode = MODE_NOISE;
        else o.mode = MODE_NOISE_SCALE;
    }
    if (o.noise == -1)
    {
        if (o.mode == MODE_NOISE)
        {
            fprintf(stderr, "noise level -1 cannot be used with mode noise\n");
            return -1;
        }
        o.mode = MODE_SCALE;
    }

    if (!find_output_format(o.format.c_str()))
    {
        fprintf(stderr, "invalid format argument\n");
        return -1;
    }
    o.format = find_output_format(o.format.c_str())->ext;


    // collect input and output file paths
    std::vector<path_t> input_files, output_files;
    if (!o.filelist.empty())
    {
        if (!read_file_list(o.filelist, input_files, output_files))
        {
            fprintf(stderr, "cannot read file list %s\n", P(o.filelist).c_str());
            return -1;
        }
    }
    else if (path_is_directory(o.inputpath))
    {
        if (path_exists(o.outputpath) && !path_is_directory(o.outputpath))
        {
            fprintf(stderr, "inputpath is a directory but outputpath is a file\n");
            return -1;
        }
        std::vector<path_t> rel;
        if (list_directory(o.inputpath, rel, o.recursive) != 0)
        {
            fprintf(stderr, "opendir failed %s\n", P(o.inputpath).c_str());
            return -1;
        }
        const path_t fmt = utf8_to_path(o.format);
        std::map<path_t, int> used;
        for (size_t i = 0; i < rel.size(); i++)
        {
            if (!ext_allowed(rel[i])) continue;
            const path_t noext = get_file_name_without_extension(rel[i]);
            path_t outrel = noext + PATHSTR(".") + fmt;
            if (used.count(to_lower(outrel)))
            {
                path_t outrel2 = rel[i] + PATHSTR(".") + fmt;
                out_line(stderr, P(rel[i]) + " and another file both output " + P(outrel) + " ! " + P(rel[i]) + " will output " + P(outrel2));
                outrel = outrel2;
            }
            used[to_lower(outrel)] = 1;
            input_files.push_back(path_join(o.inputpath, rel[i]));
            output_files.push_back(path_join(o.outputpath, outrel));
        }
    }
    else
    {
        if (!path_exists(o.inputpath))
        {
            fprintf(stderr, "input file %s does not exist\n", P(o.inputpath).c_str());
            return -1;
        }
        path_t outpath = o.outputpath;
        if (path_is_directory(outpath))
            outpath = path_join(outpath, get_file_name_without_extension(path_filename(o.inputpath)) + PATHSTR(".") + utf8_to_path(o.format));
        else
        {
            const path_t ext = get_file_extension(outpath);
            if (!find_output_format(P(ext).c_str()))
            {
                fprintf(stderr, "invalid outputpath extension type\n");
                return -1;
            }
        }
        input_files.push_back(o.inputpath);
        output_files.push_back(outpath);
    }

    // validate the format (and quality) of every output
    for (size_t i = 0; i < output_files.size(); i++)
    {
        const path_t ext = get_file_extension(output_files[i]);
        const OutputFormatInfo* fi = find_output_format(P(ext).c_str());
        if (!fi)
        {
            fprintf(stderr, "invalid output extension %s\n", P(output_files[i]).c_str());
            return -1;
        }
        if (fi->has_quality && o.quality >= 0 && (o.quality < fi->quality_min || o.quality > fi->quality_max))
        {
            fprintf(stderr, "output quality for .%s must be %d-%d\n", fi->ext, fi->quality_min, fi->quality_max);
            return -1;
        }
    }

    g_total = (int)input_files.size();
    if (o.progress) out_line(stdout, "TOTAL\t" + std::to_string(g_total));
    if (g_total == 0)
    {
        fprintf(stderr, "no input files\n");
        return 0;
    }

    // model
    path_t modeldir = sanitize_dirpath(o.model);
    if (!path_is_directory(modeldir))
    {
        fprintf(stderr, "model directory %s not found\n", P(o.model).c_str());
        return -1;
    }
    g_model = get_model_info(modeldir);
    if (!g_model.known)
    {
        fprintf(stderr, "unknown model dir type\n");
        return -1;
    }
    scan_caps(o);

    // validate the conversion against the model (the real plan is made per image)
    {
        const bool may_jpeg[2] = {false, true};
        for (int k = 0; k < (o.mode == MODE_AUTO_SCALE ? 2 : 1); k++)
        {
            Plan probe = make_plan(o, 1000, 1000, may_jpeg[k]);
            if (!probe.error.empty())
            {
                fprintf(stderr, "%s (model %s)\n", probe.error.c_str(), P(modeldir).c_str());
                return -1;
            }
        }
    }

#if _WIN32
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    ncnn::create_gpu_instance();

    const int gpu_count = ncnn::get_gpu_count();
    if (o.gpuid.empty())
    {
        if (gpu_count == 0)
        {
            out_line(stderr, "no vulkan device found, falling back to cpu");
            o.gpuid.push_back(-1);
        }
        else
            o.gpuid.push_back(ncnn::get_default_gpu_index());
    }

    const int use_gpu_count = (int)o.gpuid.size();

    if (o.tilesize.size() != (size_t)use_gpu_count && !o.tilesize.empty())
    {
        fprintf(stderr, "invalid tilesize argument\n");
        ncnn::destroy_gpu_instance();
        return -1;
    }
    for (size_t i = 0; i < o.tilesize.size(); i++)
    {
        if (o.tilesize[i] != 0 && o.tilesize[i] < 32)
        {
            fprintf(stderr, "invalid tilesize argument\n");
            ncnn::destroy_gpu_instance();
            return -1;
        }
    }
    if (o.jobs_load < 1 || o.jobs_save < 1)
    {
        fprintf(stderr, "invalid thread count argument\n");
        ncnn::destroy_gpu_instance();
        return -1;
    }
    if (o.jobs_proc.size() != (size_t)use_gpu_count && !o.jobs_proc.empty())
    {
        fprintf(stderr, "invalid jobs_proc thread count argument\n");
        ncnn::destroy_gpu_instance();
        return -1;
    }
    for (size_t i = 0; i < o.jobs_proc.size(); i++)
    {
        if (o.jobs_proc[i] < 1)
        {
            fprintf(stderr, "invalid jobs_proc thread count argument\n");
            ncnn::destroy_gpu_instance();
            return -1;
        }
    }
    for (int i = 0; i < use_gpu_count; i++)
    {
        if (o.gpuid[i] < -1 || o.gpuid[i] >= gpu_count)
        {
            fprintf(stderr, "invalid gpu device\n");
            ncnn::destroy_gpu_instance();
            return -1;
        }
    }

    if (o.jobs_proc.empty()) o.jobs_proc.resize(use_gpu_count, 2);
    if (o.tilesize.empty()) o.tilesize.resize(use_gpu_count, 0);

    const int cpu_count = std::max(1, ncnn::get_cpu_count());
    o.jobs_load = std::min(o.jobs_load, cpu_count);
    o.jobs_save = std::min(o.jobs_save, cpu_count);

    int total_jobs_proc = 0;
    int jobs_proc_per_gpu[64] = {0};
    for (int i = 0; i < use_gpu_count; i++)
    {
        if (o.gpuid[i] == -1)
        {
            o.jobs_proc[i] = std::min(o.jobs_proc[i], cpu_count);
            total_jobs_proc += 1;
        }
        else
        {
            total_jobs_proc += o.jobs_proc[i];
            jobs_proc_per_gpu[o.gpuid[i]] += o.jobs_proc[i];
        }
    }

    // devices
    std::vector<GpuNets> gpus(use_gpu_count);
    for (int i = 0; i < use_gpu_count; i++)
    {
        gpus[i].gpuid = o.gpuid[i];
        gpus[i].tilesize = o.tilesize[i];
        gpus[i].num_threads = o.gpuid[i] == -1 ? o.jobs_proc[i] : 1;
        if (o.gpuid[i] != -1)
        {
            uint32_t heap_budget = ncnn::get_gpu_device(o.gpuid[i])->get_heap_budget();
            if (g_total > 1 && jobs_proc_per_gpu[o.gpuid[i]] > 0) heap_budget /= jobs_proc_per_gpu[o.gpuid[i]];
            gpus[i].heap_total = ncnn::get_gpu_device(o.gpuid[i])->get_heap_budget();
            gpus[i].heap_budget = heap_budget;
        }
    }

    // load the networks a typical image needs now (others are loaded on first use)
    bool load_failed = false;
    {
        Plan probe = make_plan(o, 1000, 1000, o.mode != MODE_SCALE);
        for (int i = 0; i < use_gpu_count && !load_failed; i++)
            for (size_t s = 0; s < probe.nets.size(); s++)
            {
                std::string err;
                if (!get_net(&gpus[i], probe.nets[s], err)) { load_failed = true; break; }
            }
    }

    if (!load_failed)
    {
        for (int i = 0; i < use_gpu_count; i++)
        {
            const std::string name = o.gpuid[i] == -1 ? std::string("CPU") : std::string(ncnn::get_gpu_info(o.gpuid[i]).device_name());
            const std::string tile = o.tilesize[i] > 0 ? std::to_string(o.tilesize[i]) : std::string("auto");
            if (o.progress)
                out_line(stdout, "DEVICE\t" + std::to_string(o.gpuid[i]) + "\t" + name + " (tile " + tile + ")");
            else if (o.verbose)
                out_line(stderr, "using " + name + ", tile size " + tile);
        }
        if (o.progress) out_line(stdout, "READY");

        BlockingQueue<Task> toproc(8), tosave(8);
        g_toproc = &toproc;
        g_tosave = &tosave;

        std::atomic<int> next(0);
        std::vector<std::thread> load_threads;
        for (int i = 0; i < o.jobs_load; i++)
            load_threads.emplace_back(load_worker, &input_files, &output_files, &next);

        std::vector<std::thread> proc_threads;
        for (int i = 0; i < use_gpu_count; i++)
        {
            const int n = o.gpuid[i] == -1 ? 1 : o.jobs_proc[i];
            for (int j = 0; j < n; j++)
                proc_threads.emplace_back(proc_worker, &gpus[i]);
        }

        std::vector<std::thread> save_threads;
        for (int i = 0; i < o.jobs_save; i++)
            save_threads.emplace_back(save_worker);

        for (size_t i = 0; i < load_threads.size(); i++) load_threads[i].join();
        for (size_t i = 0; i < proc_threads.size(); i++) { Task end; end.id = -233; toproc.put(std::move(end)); }
        for (size_t i = 0; i < proc_threads.size(); i++) proc_threads[i].join();
        for (size_t i = 0; i < save_threads.size(); i++) { Task end; end.id = -233; tosave.put(std::move(end)); }
        for (size_t i = 0; i < save_threads.size(); i++) save_threads[i].join();
    }

    for (int i = 0; i < use_gpu_count; i++)
        for (auto& kv : gpus[i].nets)
            delete kv.second;
    gpus.clear();

    ncnn::destroy_gpu_instance();

    if (load_failed) return -1;
    return g_failed > 0 ? 1 : 0;
}

#if _WIN32
int wmain(int argc, wchar_t** argv)
{
    setlocale(LC_ALL, "");
    if (_isatty(_fileno(stdout))) SetConsoleOutputCP(CP_UTF8);
    std::vector<path_t> args;
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    return run(args);
}
#else
int main(int argc, char** argv)
{
    setlocale(LC_ALL, "");
    std::vector<path_t> args;
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    return run(args);
}
#endif
