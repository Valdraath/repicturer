// main window of RePicturer, rebuilt from waifu2x-caffe-gui MainDialog.cpp

#include "MainDialog.h"
#include "ImageProbe.h"
#include "../common/formats.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <functional>
#include <sstream>
#include <numeric>
#include <set>
#include <stdlib.h>
#include <string.h>

#include "imgui_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MessageBox
#else
#include <unistd.h>
#endif

static const int MaxBatchSizeList = 20;
static const int MinCommonDivisor = 50;
static const int DefaultCommonDivisor = 128;
static const std::pair<int, int> DefaultCommonDivisorRange = {90, 140};

static const char* const CropSizeListName = "crop_size_list.txt";
static const char* const SettingFileName = "setting.ini";
static const char* const LangDir = "lang";
static const char* const LangListFileName = "lang/LangList.txt";
static const char* const MultiFileStr = "(Multi File)"; // shown translated (T)
static const char* const CropSeparator = "-----------------------";

enum { IconError = 0, IconInfo = 1, IconWarning = 2 };

// dialog units of the original Resource.rc -> pixels (MS Shell Dlg 8pt base units at 96 dpi)
static const float DLU_X = 1.5f;
static const float DLU_Y = 1.625f;

struct BuiltinModel
{
    const char* folder;
    const char* type;
    const char* langKey;
    const char* fallbackTitle;
    const char* name;
    int engine;
};

// best first; the outdated waifu2x-caffe vgg_7 models (RGB, Photo, Y, ukbench) are not shipped any more
// (any extra models-* folder still shows up at the end of the list)
static const BuiltinModel g_builtinModels[] = {
    {"models-realcugan-se", "realcugan-se", "ModelRealCUGAN_SE", "Real-CUGAN SE - illustration / anime (best)", "RealCUGAN-SE", EngineRealCUGAN},
    {"models-realcugan-pro", "realcugan-pro", "ModelRealCUGAN_Pro", "Real-CUGAN Pro - illustration / anime (sharper)", "RealCUGAN-Pro", EngineRealCUGAN},
    {"models-realcugan-nose", "realcugan-nose", "ModelRealCUGAN_NoSE", "Real-CUGAN NoSE - illustration, no denoise", "RealCUGAN-NoSE", EngineRealCUGAN},
    {"models-nmkd-superscale", "nmkd-superscale", "ModelNMKD_Superscale", "NMKD Superscale - photo / 3D render (natural detail)", "NMKD-Superscale", EngineRealESRGAN},
    {"models-nmkd-siax", "nmkd-siax", "ModelNMKD_Siax", "NMKD Siax - photo / 3D render (sharp detail)", "NMKD-Siax", EngineRealESRGAN},
    {"models-nomos8ksc", "nomos8ksc", "ModelNomos8kSC", "Nomos8kSC - photo, repairs JPEG / compression", "Nomos8kSC", EngineRealESRGAN},
    {"models-realesr-general-x4v3", "realesr-general-x4v3", "ModelRealESR_general_x4v3", "Real-ESRGAN General - photo (fast, adjustable denoise)", "RealESR-general-x4v3", EngineRealESRGAN},
    {"models-lsdir-compact", "lsdir-compact", "ModelLSDIR_Compact", "LSDIR Compact - photo / 3D render (fast, adjustable cleanup)", "LSDIR-Compact", EngineRealESRGAN},
    {"models-realesrnet-x4plus", "realesrnet-x4plus", "ModelRealESRNet_x4plus", "RealESRNet x4plus - photo / 3D render (faithful)", "RealESRNet-x4plus", EngineRealESRGAN},
    {"models-realesrgan-x4plus", "realesrgan-x4plus", "ModelRealESRGAN_x4plus", "Real-ESRGAN x4plus - photo / realistic 3D (good)", "RealESRGAN-x4plus", EngineRealESRGAN},
    {"models-realesrgan-x4plus-anime", "realesrgan-x4plus-anime", "ModelRealESRGAN_x4plus_anime", "Real-ESRGAN x4plus anime - illustration (strong)", "RealESRGAN-x4plus-anime", EngineRealESRGAN},
    {"models-realesr-animevideov3", "realesr-animevideov3", "ModelRealESR_animevideov3", "Real-ESRGAN animevideo v3 - anime frames (fast)", "RealESR-animevideov3", EngineRealESRGAN},
    {"models-cunet", "cunet", "ModelW2X_CUnet", "waifu2x CUnet - illustration (faithful)", "CUnet", EngineWaifu2x},
    {"models-upresnet10", "upresnet10", "ModelW2X_UpResNet10", "waifu2x UpResNet10 - illustration (balanced)", "UpResNet10", EngineWaifu2x},
    {"models-upconv_7_anime_style_art_rgb", "upconv_7_anime_style_art_rgb", "ModelW2X_UpRGB", "waifu2x UpRGB - illustration (fastest)", "UpRGB", EngineWaifu2x},
    {"models-upconv_7_photo", "upconv_7_photo", "ModelW2X_UpPhoto", "waifu2x UpPhoto - photo (light, fast, balanced)", "UpPhoto", EngineWaifu2x},
};
static const char* const DefaultModelType = "realcugan-se";
static const char* const FallbackModelType = "cunet";
// the image types the program reads: fixed, not a setting
static const char* const DefaultInputExtList = "png:jpg:jpeg:tif:tiff:bmp:tga:webp:gif";
// "PNG | JPG | JPEG | ..." for display
static std::string ExtListDisplay()
{
    std::string r;
    for (std::string e : Split(DefaultInputExtList, ':', true))
    {
        for (char& c : e) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        r += (r.empty() ? "" : " | ") + e;
    }
    return r;
}
// explorer settings an earlier version wrote: accepted by the setting.ini check, removed on the next save
static const char* const LegacyBrowserKeys[] = {"LastBrowserBookmarks", "LastBrowserPinned", "LastBrowserView",
    "LastBrowserFoldersFirst", "LastBrowserPreview", "LastBrowserAutoOpen", "LastBrowserColumns", "LastInputFileExt", "LastBrowserHidden", "LastOutputDepth"};

// ---- value rules of setting.ini (CheckSettingFile, SaveIni)

// whole number in [lo, hi], digits with an optional leading '-'
static bool IsIntIn(const std::string& s, long lo, long hi)
{
    if (s.empty() || s.size() > 10) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); k++)
        if (s[k] < '0' || s[k] > '9') return false;
    const long v = strtol(s.c_str(), nullptr, 10);
    return v >= lo && v <= hi;
}

// "" or "a,b,c" with every number in [lo, hi] (gpu ids, threads per gpu)
static bool IsValidIntList(const std::string& s, long lo, long hi)
{
    if (s.empty()) return true;
    for (const auto& p : Split(s, ',', false))
        if (!IsIntIn(Trim(p), lo, hi)) return false;
    return true;
}

// "" or "a,b" split sizes: 0 (auto) or 32 .. 65536
static bool IsValidTileList(const std::string& s)
{
    if (s.empty()) return true;
    for (const auto& p : Split(s, ',', false))
    {
        const std::string t = Trim(p);
        if (!(t == "0" || IsIntIn(t, 32, 65536))) return false;
    }
    return true;
}

// ---------------------------------------------------------------- model capabilities

bool ModelEntry::NoiseLevelAvailable(int level) const
{
    if (level < 0 || level > 3) return false;
    if (engine == EngineWaifu2x) return noise[level] || noiseScale[level];
    if (engine == EngineRealCUGAN)
    {
        const int variant = level == 0 ? -1 : level; // level 0 = conservative
        for (int s = 2; s <= 4; s++)
            if (cugan[s][variant + 2]) return true;
        return false;
    }
    // Real-ESRGAN: only models with one network per level (realesr-general-x4v3) have denoise levels;
    // models with detail levels (NMKD Siax) have all four
    if (HasDetailLevels()) return true;
    for (int s = 2; s <= 4; s++)
        if (esrNoise[s][level]) return true;
    return false;
}

bool ModelEntry::BuiltInCleanup() const
{
    if (engine != EngineRealESRGAN) return false;
    for (int s = 2; s <= 4; s++)
        for (int l = 0; l < 4; l++)
            if (esrNoise[s][l]) return false;
    return true;
}

bool ModelEntry::AnyDenoise() const
{
    if (engine == EngineRealESRGAN) return CanMagnify(); // the network restores by itself
    for (int l = 0; l < 4; l++)
        if (NoiseLevelAvailable(l)) return true;
    return false;
}

bool ModelEntry::CanMagnify() const
{
    if (engine == EngineWaifu2x) return scale;
    for (int s = 2; s <= 4; s++)
    {
        if (engine == EngineRealCUGAN && cugan[s][0]) return true;
        if (engine == EngineRealESRGAN && esr[s]) return true;
    }
    return false;
}

namespace
{
    std::vector<int> CommonDivisorList(const int N)
    {
        std::vector<int> list;
        const int sq = (int)sqrt((double)N);
        for (int i = 1; i <= sq; i++)
            if (N % i == 0) list.push_back(i);
        const size_t sqs = list.size();
        for (size_t i = 0; i < sqs; i++)
            list.push_back(N / list[i]);
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        return list;
    }

    // boost relativePath equivalent
    std::string RelativePath(const std::string& path, const std::string& base)
    {
        std::error_code ec;
        fs::path r = fs::relative(U8Path(path), U8Path(base), ec);
        if (ec || r.empty()) return FileName(path);
        return PathU8(r);
    }

    void CopyStr(char* dst, size_t n, const std::string& s)
    {
        snprintf(dst, n, "%s", s.c_str());
    }
}

// ============================================================================ construction

DialogEvent::DialogEvent() : mode(eModeNoiseScale), modeStr("noise_scale"), noise_level(0), scale_ratio(1.0), scale_width(0), scale_height(0),
    process("gpu"), outputExt(".png"), inputFileExt(DefaultInputExtList), use_tta(false), output_quality(-1),
    crop_size(0), batch_size(2), gpu_no(-1), isLastError(false), scaleType(eScaleTypeEnd), modelType(0), tAutoMode("none"),
    isArgStartAuto(true), isArgStartSuccessFinish(true), isOutputNoOverwrite(false), isNotSaveParam(false), isSetInitCrop(false),
    isCommandLineStart(false), loadThreads(1), saveThreads(2), verboseLog(false), syncgap(3), gpuState(0), cancelFlag(false)
{
    // the input types are fixed: ready before anything is dropped, typed or browsed
    for (auto ext : Split(inputFileExt, ':', true))
        extList.push_back("." + ToLowerAscii(ext));
}

DialogEvent::~DialogEvent()
{
    {
        std::lock_guard<std::mutex> g(procLock);
        if (proc) proc->Kill();
    }
    cancelFlag = true;
    if (processThread.joinable()) processThread.join();
    if (gpuThread.joinable()) gpuThread.join();
}

std::string DialogEvent::CliPath() const
{
#ifdef _WIN32
    return JoinPath(exeDir, "repicturer-cli.exe");
#else
    return JoinPath(exeDir, "repicturer-cli");
#endif
}

std::string DialogEvent::SettingPath() const
{
    return JoinPath(exeDir, SettingFileName);
}

void DialogEvent::MessageBox(const std::string& text, const std::string& title, int icon)
{
    msgBoxes.push_back({title, text, icon});
}

void DialogEvent::PostEvent(const Event& e)
{
    std::lock_guard<std::mutex> g(eventLock);
    events.push_back(e);
}

// ============================================================================ models / gpus

void DialogEvent::ScanModels()
{
    models.clear();
    auto has = [](const std::string& dir, const std::string& base) {
        return PathExists(JoinPath(dir, base + ".param")) && PathExists(JoinPath(dir, base + ".bin"));
    };
    auto fill = [&has](ModelEntry& m) {
        for (int n = 0; n < 4; n++)
        {
            std::string s = std::to_string(n);
            m.noise[n] = has(m.dir, "noise" + s + "_model");
            m.noiseScale[n] = has(m.dir, "noise" + s + "_scale2.0x_model");
        }
        m.scale = has(m.dir, "scale2.0x_model");
        for (int sc = 2; sc <= 4; sc++)
        {
            const std::string up = "up" + std::to_string(sc) + "x-";
            m.cugan[sc][0] = has(m.dir, up + "no-denoise");
            m.cugan[sc][1] = has(m.dir, up + "conservative");
            for (int d = 1; d <= 3; d++) m.cugan[sc][d + 2] = has(m.dir, up + "denoise" + std::to_string(d) + "x");
            m.esr[sc] = has(m.dir, "x" + std::to_string(sc));
            for (int d = 0; d < 4; d++) m.esrNoise[sc][d] = has(m.dir, "x" + std::to_string(sc) + "-denoise" + std::to_string(d));
            for (int d = 0; d < 3; d++) m.esrDetail[sc][d] = has(m.dir, "x" + std::to_string(sc) + "-detail" + std::to_string(d));
            if (m.engine == EngineRealESRGAN)
            {
                std::error_code fec;
                const auto bytes = fs::file_size(U8Path(JoinPath(m.dir, "x" + std::to_string(sc) + ".bin")), fec);
                if (!fec && bytes > 20u * 1024 * 1024) m.heavy = true;
            }
        }
    };
    std::set<std::string> known;
    for (const auto& b : g_builtinModels)
    {
        known.insert(b.folder);
        std::string dir = JoinPath(exeDir, b.folder);
        if (!IsDirectory(dir)) continue;
        ModelEntry m;
        m.dir = dir;
        m.folder = b.folder;
        m.type = b.type;
        m.langKey = b.langKey;
        m.name = b.name;
        m.engine = b.engine;
        fill(m);
        models.push_back(m);
    }
    // custom models: any other models-* folder next to the executable (name from model.ini)
    std::error_code ec;
    std::vector<std::string> extra;
    for (auto it = fs::directory_iterator(U8Path(exeDir), ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        if (!it->is_directory(ec)) continue;
        std::string f = PathU8(it->path().filename());
        if (f.rfind("models-", 0) != 0 || known.count(f)) continue;
        extra.push_back(f);
    }
    std::sort(extra.begin(), extra.end());
    for (const auto& f : extra)
    {
        ModelEntry m;
        m.dir = JoinPath(exeDir, f);
        m.folder = f;
        m.type = f.substr(7);
        m.name = f.substr(7);
        std::string ini;
        if (ReadTextFileUtf8(JoinPath(m.dir, "model.ini"), ini))
        {
            for (const auto& line : Split(ini, '\n', true))
            {
                std::string t = Trim(line);
                if (t.rfind("name=", 0) == 0) m.name = Trim(t.substr(5));
                if (t == "engine=realcugan") m.engine = EngineRealCUGAN;
                if (t == "engine=realesrgan") m.engine = EngineRealESRGAN;
            }
        }
        fill(m);
        models.push_back(m);
    }
}

void DialogEvent::StartGpuDetect(bool showResult)
{
    if (gpuState == 1) { gpuShowResult = gpuShowResult || showResult; return; }
    if (gpuThread.joinable()) gpuThread.join();
    gpuShowResult = showResult;
    gpuApplied = false;
    gpuState = 1;
    const std::string exe = CliPath();
    gpuThread = std::thread([this, exe]() {
        std::vector<GpuDevice> found;
        std::string err;
        std::mutex m;
        if (!PathExists(exe))
            err = T("not found:") + " " + exe;
        else
        {
            Subprocess p;
            std::string serr;
            bool ok = p.Start(exe, {"--list-gpu"}, [&](int stream, const std::string& line) {
                std::lock_guard<std::mutex> g(m);
                if (stream == 1 && line.rfind("GPU\t", 0) == 0)
                {
                    auto c = Split(line, '\t', false);
                    if (c.size() >= 5)
                        found.push_back({atoi(c[1].c_str()), c[2], c[3], atoi(c[4].c_str())});
                }
                else if (stream == 2 && !line.empty())
                    serr += line + "\n";
            }, err);
            if (ok) p.Wait();
            if (found.empty() && err.empty()) err = serr;
        }
        std::lock_guard<std::mutex> g(eventLock);
        gpusPending = found;
        gpuErrorPending = err;
        gpuState = 2;
    });
}

std::vector<std::string> DialogEvent::DeviceComboItems() const
{
    std::vector<std::string> v;
    v.push_back(L2("DeviceAuto", "Auto (GPU, CPU if no GPU)"));
    for (const auto& g : gpus) v.push_back(Format("GPU %d: %s", g.index, g.name.c_str()));
    v.push_back(L2("DeviceCPU", "CPU"));
    return v;
}

int DialogEvent::DeviceComboIndex() const
{
    if (process == "cpu") return (int)gpus.size() + 1;
    if (gpu_no < 0) return 0;
    for (size_t i = 0; i < gpus.size(); i++)
        if (gpus[i].index == gpu_no) return (int)i + 1;
    return 0;
}

void DialogEvent::SetDeviceFromCombo(int idx)
{
    if (idx <= 0) { process = "gpu"; gpu_no = -1; }
    else if (idx > (int)gpus.size()) { process = "cpu"; }
    else { process = "gpu"; gpu_no = gpus[idx - 1].index; }
}

// ============================================================================ caffe logic

std::string DialogEvent::AddName() const
{
    std::string addstr("(");
    if (modelType >= 0 && modelType < (int)models.size()) addstr += models[modelType].name;
    addstr += ")";

    addstr += "(";
    switch (mode)
    {
    case eModeNoise: addstr += "noise"; break;
    case eModeScale: addstr += "scale"; break;
    case eModeNoiseScale: addstr += "noise_scale"; break;
    case eModeAutoScale: addstr += "auto_scale"; break;
    }
    addstr += ")";

    const bool detailLevels = modelType >= 0 && modelType < (int)models.size() && models[modelType].HasDetailLevels();
    if (detailLevels)
        addstr += "(Detail" + std::to_string(noise_level) + ")";
    else if (mode == eModeNoise || mode == eModeNoiseScale || mode == eModeAutoScale)
        addstr += "(Level" + std::to_string(noise_level) + ")";
    if (use_tta)
        addstr += "(tta)";

    if (mode == eModeScale || mode == eModeNoiseScale || mode == eModeAutoScale)
    {
        if (scaleType == eScaleTypeRatio)
            addstr += "(x" + Format("%.2f", scale_ratio) + ")";
        else if (scaleType == eScaleTypeWidth)
            addstr += "(width " + std::to_string(scale_width) + ")";
        else if (scaleType == eScaleTypeHeight)
            addstr += "(height " + std::to_string(scale_height) + ")";
        else
            addstr += "(" + std::to_string(scale_width) + "x" + std::to_string(scale_height) + ")";
    }


    return addstr;
}

bool DialogEvent::SyncMember(const bool NotSyncCropSize, const bool silent)
{
    bool ret = true;
    const std::string& errTitle = L("MessageTitleError");

    if (input_str_multi.empty())
        input_str = editInput;

    output_str = editOutput;

    switch (radioMode)
    {
    case 2: mode = eModeNoise; modeStr = "noise"; break;
    case 1: mode = eModeScale; modeStr = "scale"; break;
    case 0: mode = eModeNoiseScale; modeStr = "noise_scale"; break;
    default: mode = eModeAutoScale; modeStr = "auto_scale"; break;
    }

    noise_level = std::min(3, std::max(0, radioNoise));
    scaleType = (eScaleType)std::min(3, std::max(0, radioScale));

    {
        double d;
        if (!ParseDouble(editScaleRatio, d) || d <= 0.0)
        {
            if (scaleType == eScaleTypeRatio)
            {
                ret = false;
                if (!silent) MessageBox(L("MessageScaleRateCheckError"), errTitle, IconError);
            }
        }
        else
            scale_ratio = d;
    }

    {
        long l;
        if (!ParseInt(editScaleWidth, l) || l <= 0)
        {
            if (scaleType == eScaleTypeWidth)
            {
                ret = false;
                if (!silent) MessageBox(L("MessageScaleWidthCheckError"), errTitle, IconError);
            }
        }
        else
            scale_width = (int)l;
    }

    {
        long l;
        if (!ParseInt(editScaleHeight, l) || l <= 0)
        {
            if (scaleType == eScaleTypeHeight)
            {
                ret = false;
                if (!silent) MessageBox(L("MessageScaleWidthCheckError"), errTitle, IconError);
            }
        }
        else
            scale_height = (int)l;
    }

    if (scaleType == eScaleTypeWidthHeight)
    {
        // the Width and Height boxes above
        long w, h;
        if (ParseInt(editScaleWidth, w) && w > 0 && ParseInt(editScaleHeight, h) && h > 0)
        {
            scale_width = (int)w;
            scale_height = (int)h;
        }
        else
        {
            ret = false;
            if (!silent) MessageBox(L("MessageScaleWidthCheckError"), errTitle, IconError);
        }
    }

    if (comboModel >= 0 && comboModel < (int)models.size())
    {
        modelType = comboModel;
        model_dir = models[comboModel].dir;
    }

    if (outExtIndex < 0 || outExtIndex >= g_output_format_count)
    {
        if (!silent) MessageBox(L("MessageOutputExtentionCheckError"), errTitle, IconError);
    }
    else
    {
        const OutputFormatInfo& elm = g_output_formats[outExtIndex];
        outputExt = std::string(".") + elm.ext;
        if (elm.has_quality)
        {
            long num;
            if (!ParseInt(editQuality, num) || num < elm.quality_min || num > elm.quality_max)
            {
                output_quality = -1;
                ret = false;
                if (!silent) MessageBox(L("MessageOutputQualityCheckError"), errTitle, IconError);
            }
            else
                output_quality = (int)num;
        }
        else
            output_quality = -1;
    }

    inputFileExt = DefaultInputExtList;
    extList.clear();
    for (auto ext : Split(inputFileExt, ':', true))
        extList.push_back("." + ToLowerAscii(ext));

    if (!NotSyncCropSize)
    {
        long l;
        if (!ParseInt(Trim(comboCrop), l) || l < 0)
        {
            crop_size = 0;
            ret = false;
            if (!silent) MessageBox(L("MessageCropSizeCheckError"), errTitle, IconError);
        }
        else if (l != 0 && l < 32) // ncnn tiles must be 0 (auto) or >= 32
        {
            ret = false;
            if (!silent) MessageBox(L2("MessageTileSizeCheckError", "Split size must be 0 (auto) or an integer of 32 or more"), errTitle, IconError);
        }
        else
            crop_size = (int)l;
    }

    {
        long l;
        if (!ParseInt(Trim(comboBatch), l) || l <= 0)
        {
            batch_size = 2;
            ret = false;
            if (!silent) MessageBox(L2("MessageProcThreadsCheckError", "Thread(s) must be an integer of 1 or more"), errTitle, IconError);
        }
        else
            batch_size = (int)l;
    }

    use_tta = checkTTA;
    return ret;
}

void DialogEvent::SetCropSizeList(const std::string& input_path)
{
    if (isSetInitCrop) return;

    int gcd = 1;
    if (PathExists(input_path) && !IsDirectory(input_path))
    {
        int w, h;
        if (!ProbeImageSize(input_path, w, h)) return;
        gcd = std::gcd(w, h);
    }

    cropItems.clear();

    std::vector<int> list(CommonDivisorList(gcd));
    list.erase(std::remove_if(list.begin(), list.end(), [](const int v) { return v < MinCommonDivisor; }), list.end());

    // ncnn picks the tile size from the free gpu memory: "0" is the recommended value
    cropItems.push_back("0");

    if (!list.empty())
        cropItems.push_back(CropSeparator);

    for (const int n : list)
        cropItems.push_back(std::to_string(n));

    cropItems.push_back(CropSeparator);
    for (const int n : CropSizeList)
        cropItems.push_back(std::to_string(n));

    if (strlen(comboCrop) == 0)
        CopyStr(comboCrop, sizeof(comboCrop), cropItems[0]);
}

void DialogEvent::ReplaceAddString()
{
    SyncMember(true, true);

    const std::string output_path = output_str;
    std::string stem;
    const bool isFile = input_str_multi.empty() && !IsDirectory(input_str);

    if (isFile) stem = Stem(output_path);
    else stem = FileName(output_path);

    if (!stem.empty() && stem.length() >= autoSetAddName.length())
    {
        const auto pos = stem.rfind(autoSetAddName);
        if (pos != std::string::npos && !autoSetAddName.empty())
        {
            const std::string addstr(AddName());
            std::string new_name = stem;
            new_name.replace(pos, autoSetAddName.length(), addstr);
            autoSetAddName = addstr;

            std::string new_out_path;
            if (isFile) new_out_path = JoinPath(ParentPath(output_path), new_name + outputExt);
            else new_out_path = JoinPath(ParentPath(output_path), new_name);

            CopyStr(editOutput, sizeof(editOutput), NativeSeparators(new_out_path));
            output_str = editOutput;
        }
    }
}

void DialogEvent::AddLogMessage(const std::string& msg)
{
    if (logMessage.empty()) logMessage += msg;
    else logMessage += "\n" + msg;
}

static std::string FormatDuration(const char* fmt, std::chrono::steady_clock::duration d)
{
    long long t = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    if (t < 0) t = 0;
    const int msec = (int)(t % 1000); t /= 1000;
    const int sec = (int)(t % 60); t /= 60;
    const int min = (int)(t % 60); t /= 60;
    const int hour = (int)t;
    return Format(fmt, hour, min, sec, msec);
}

void DialogEvent::Waifu2xTime()
{
    std::string p;
    if (usedDevices.empty()) p = process == "cpu" ? "CPU" : "GPU (Vulkan)";
    else
        for (size_t i = 0; i < usedDevices.size(); i++) p += (i ? ", " : "") + TranslateDetail(usedDevices[i]);
    std::string msg = Format(L("MessageUseProcessorMode").c_str(), p.c_str());
    const auto initEnd = readyReceived ? readyTime : startTime;
    msg += "\n" + FormatDuration(L("MessageProcessTime").c_str(), endTime - initEnd);
    msg += "\n" + FormatDuration(L("MessageInitTime").c_str(), initEnd - startTime);
    AddLogMessage(msg);
}

void DialogEvent::SaveIni(const bool isSyncMember)
{
    if (isSyncMember) SyncMember(true, true);
    if (isNotSaveParam) return;

    IniFile ini;
    ini.Load(SettingPath());
    const char* S = "Setting";

    std::string tScaleMode;
    if (scaleType == eScaleTypeRatio) tScaleMode = "Ratio";
    else if (scaleType == eScaleTypeWidth) tScaleMode = "Width";
    else if (scaleType == eScaleTypeHeight) tScaleMode = "Height";
    else tScaleMode = "WidthHeight";

    // only values the startup check (CheckSettingFile) accepts are written: a half typed value in the window is
    // stored as its default, so setting.ini never looks broken because of the program's own save
    const OutputFormatInfo* fmt = find_output_format(outputExt.c_str());
    std::string tQuality;
    if (fmt && fmt->has_quality)
        tQuality = std::to_string(output_quality >= fmt->quality_min && output_quality <= fmt->quality_max ? output_quality : fmt->quality_default);
    const std::string tCropText = Trim(comboCrop);
    long tCropNum;
    const std::string tCrop = ParseInt(tCropText, tCropNum) && (tCropNum == 0 || (tCropNum >= 32 && tCropNum <= 65536)) ? tCropText : "0";

    ini.Set(S, "LastScaleMode", tScaleMode);
    ini.Set(S, "LastScale", scale_ratio > 0.0 ? Format("%.2f", scale_ratio) : "1.00");
    ini.Set(S, "LastScaleWidth", scale_width > 0 && scale_width <= 999999999 ? std::to_string(scale_width) : "");
    ini.Set(S, "LastScaleHeight", scale_height > 0 && scale_height <= 999999999 ? std::to_string(scale_height) : "");
    ini.Set(S, "LastScaleWidthHeight", scale_width > 0 && scale_height > 0 && scale_width <= 999999999 && scale_height <= 999999999 ? std::to_string(scale_width) + "x" + std::to_string(scale_height) : "");
    ini.Set(S, "LastOutputExt", fmt ? std::string(".") + fmt->ext : std::string(".png"));
    ini.Set(S, "LastMode", modeStr);
    ini.Set(S, "LastNoiseLevel", std::to_string(noise_level));
    ini.Set(S, "LastProcess", process);
    ini.Set(S, "LastModelType", modelType >= 0 && modelType < (int)models.size() ? models[modelType].type : DefaultModelType);
    ini.Set(S, "LastUseTTA", use_tta ? "1" : "0");
    ini.Set(S, "LastDeviceNo", std::to_string(gpu_no >= -1 && gpu_no <= 63 ? gpu_no : -1));
    ini.Set(S, "LastOutputQuality", tQuality);
    ini.Set(S, "LastLanguage", LangName);
    ini.Set(S, "LastAutoMode", tAutoMode);
    ini.Set(S, "LastIsArgStartAuto", isArgStartAuto ? "1" : "0");
    ini.Set(S, "LastIsArgStartSuccessFinish", isArgStartSuccessFinish ? "1" : "0");
    ini.Set(S, "LastIsOutputNoOverwrite", isOutputNoOverwrite ? "1" : "0");
    ini.Set(S, "LastInputDirFix", tInputDirFix);
    ini.Set(S, "LastOutputDirFix", tOutputDirFix);
    ini.Set(S, "LastBatchSize", std::to_string(batch_size >= 1 && batch_size <= 1024 ? batch_size : 2));
    ini.Set(S, "LastCropSize", tCrop);
    ini.Set(S, "LastGpuIds", IsValidIntList(gpuIdsMulti, -1, 63) ? gpuIdsMulti : "");
    ini.Set(S, "LastTileSizes", IsValidTileList(tileMulti) ? tileMulti : "");
    ini.Set(S, "LastProcThreads", IsValidIntList(procMulti, 1, 1024) ? procMulti : "");
    ini.Set(S, "LastLoadThreads", std::to_string(loadThreads));
    ini.Set(S, "LastSaveThreads", std::to_string(saveThreads));
    ini.Set(S, "LastVerboseLog", verboseLog ? "1" : "0");
    ini.Set(S, "LastSyncGap", std::to_string(syncgap));
    {
        const FileBrowser::Prefs& bp = fileBrowser.prefs;
        ini.Set(S, "LastBrowserSort", std::to_string(bp.sortColumn));
        ini.Set(S, "LastBrowserSortDesc", bp.sortDescending ? "1" : "0");
        // written by an earlier version (explorer features since removed): dropped from the file
        for (const char* old : LegacyBrowserKeys) ini.Remove(S, old);
        ini.Set(S, "LastBrowserFolderIn", lastBrowseIn);
        ini.Set(S, "LastBrowserFolderOut", lastBrowseOut);
    }
    if (!ini.Save(SettingPath()))
    {
        // read-only install folder (e.g. /opt on linux): nothing else to do, settings are just not kept
    }
}

void DialogEvent::SetWindowTextLang()
{
    // labels are looked up every frame; only the dependent texts need a refresh
    SetQuality(false);
}

void DialogEvent::SetQuality(const bool SetDefaultQuality)
{
    if (outExtIndex < 0 || outExtIndex >= g_output_format_count) return;
    const OutputFormatInfo& elm = g_output_formats[outExtIndex];

    if (!elm.has_quality)
    {
        qualityEnabled = false;
        editQuality[0] = 0;
        qualityTitle = L("IDC_STATIC_OUTPUT_QUALITY");
        output_quality = -1;
    }
    else
    {
        qualityEnabled = true;
        if (SetDefaultQuality)
            CopyStr(editQuality, sizeof(editQuality), std::to_string(elm.quality_default));
        qualityTitle = L("IDC_STATIC_OUTPUT_QUALITY") + " (" + std::to_string(elm.quality_min) + "-" + std::to_string(elm.quality_max) + ")"; // e.g. (0-100), (1-101), (0-1)
    }
}

// ============================================================================ processing

void DialogEvent::ProcessWaifu2x(Job job)
{
    std::vector<std::pair<std::string, std::string>> file_paths;
    bool failed = false;

    auto failDir = [&](const std::string& dir) {
        Event e;
        e.type = EvFailCreateDir;
        e.a = dir;
        PostEvent(e);
        failed = true;
    };

    auto collect = [&](const std::string& input_path, const std::string& output_base, bool multi) {
        std::vector<std::pair<std::string, std::string>> list;
        std::error_code ec;
        // an output folder inside the input folder must not be read back as input (second run)
        const std::string outKey = ToLowerAscii(PathU8(U8Path(AbsolutePath(output_base)).lexically_normal()));
        fs::recursive_directory_iterator it(U8Path(input_path), fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code ec2;
            if (it->is_directory(ec2))
            {
                const std::string d = ToLowerAscii(PathU8(it->path().lexically_normal()));
                if (d == outKey || (d.size() > outKey.size() && d.compare(0, outKey.size(), outKey) == 0 && (d[outKey.size()] == '/' || d[outKey.size()] == '\\')))
                    it.disable_recursion_pending();
                // the output folder itself (multi mode: output_base/<input name>) is skipped as well
                continue;
            }
            const std::string p = PathU8(it->path());
            const std::string ext = ToLowerAscii(Extension(p));
            if (std::find(job.extList.begin(), job.extList.end(), ext) == job.extList.end()) continue;
            const std::string out_relative = RelativePath(p, input_path);
            std::string out_absolute = multi ? JoinPath(JoinPath(output_base, FileName(input_path)), out_relative) : JoinPath(output_base, out_relative);
            const std::string out = JoinPath(ParentPath(out_absolute), Stem(out_absolute)) + job.outputExt;
            list.emplace_back(p, out);
        }
        std::sort(list.begin(), list.end());
        return list;
    };

    const auto inputFunc = [&](const std::string& input) {
        const std::string input_path = AbsolutePath(input);
        if (IsDirectory(input_path))
        {
            const std::string output_path = AbsolutePath(job.outputStr);
            if (!PathExists(output_path) && !CreateDirs(output_path)) { failDir(output_path); return; }
            auto list = collect(input_path, output_path, false);
            file_paths.insert(file_paths.end(), list.begin(), list.end());
            for (const auto& p : file_paths)
            {
                const std::string out_dir = ParentPath(p.second);
                if (!PathExists(out_dir) && !CreateDirs(out_dir)) { failDir(out_dir); return; }
            }
        }
        else
        {
            const std::string outDir = ParentPath(job.outputStr);
            if (!outDir.empty() && !PathExists(outDir) && !CreateDirs(outDir)) { failDir(outDir); return; }
            file_paths.emplace_back(input, job.outputStr);
        }
    };

    const auto inputFuncMulti = [&](const std::string& input) {
        const std::string input_path = AbsolutePath(input);
        const std::string output_path = AbsolutePath(job.outputStr);
        if (IsDirectory(input_path))
        {
            if (!PathExists(output_path) && !CreateDirs(output_path)) { failDir(output_path); return; }
            auto list = collect(input_path, output_path, true);
            for (const auto& p : list)
            {
                const std::string out_dir = ParentPath(p.second);
                if (!PathExists(out_dir) && !CreateDirs(out_dir)) { failDir(out_dir); return; }
            }
            file_paths.insert(file_paths.end(), list.begin(), list.end());
        }
        else
        {
            if (!PathExists(output_path) && !CreateDirs(output_path)) { failDir(output_path); return; }
            file_paths.emplace_back(input_path, JoinPath(output_path, Stem(input_path) + job.outputExt));
        }
    };

    if (job.inputMulti.empty())
        inputFunc(job.inputStr);
    else
        for (const auto& path : job.inputMulti)
        {
            inputFuncMulti(path);
            if (failed) break;
        }

    if (failed || cancelFlag)
    {
        Event e;
        e.type = EvEnd;
        e.n = failed ? -2 : -1000;
        PostEvent(e);
        return;
    }

    // nothing to convert (empty folder / no file with the listed extensions): say so instead of "success"
    if (file_paths.empty())
    {
        Event e;
        e.type = EvFatal;
        e.a = Format(L2("MessageNoInputFiles", "No image files found (input file extensions: %s)").c_str(), Trim(editExtList).c_str());
        PostEvent(e);
        Event end;
        end.type = EvEnd;
        end.n = -2;
        PostEvent(end);
        return;
    }

    // two inputs mapping to the same output (photo.jpg + photo.png, or same names from different selected
    // folders) would overwrite each other: later ones get the source extension added, e.g. photo_jpg.png
    {
        std::set<std::string> used;
        auto key = [](const std::string& s) { return ToLowerAscii(PathU8(U8Path(s).lexically_normal())); };
        for (auto& p : file_paths)
        {
            if (used.insert(key(p.second)).second) continue;
            const std::string dir = ParentPath(p.second), stem = Stem(p.second), ext = Extension(p.second);
            std::string srcExt = ToLowerAscii(Extension(p.first));
            if (!srcExt.empty() && srcExt[0] == '.') srcExt.erase(0, 1);
            std::string cand = JoinPath(dir, stem + "_" + srcExt + ext);
            for (int n = 2; !used.insert(key(cand)).second; n++)
                cand = JoinPath(dir, stem + "_" + srcExt + "_" + std::to_string(n) + ext);
            Event e;
            e.type = EvLog;
            e.n = 0;
            e.a = Format(L2("MessageNameClash", "%s: same output name as another file, saved as %s").c_str(), FileName(p.first).c_str(), FileName(cand).c_str());
            PostEvent(e);
            p.second = cand;
        }
    }

    // hand the exact file list to the converter (keeps caffe's naming rules for every file)
    std::string list;
    for (const auto& p : file_paths) list += p.first + "\t" + p.second + "\n";
    if (!WriteTextFileUtf8(job.listPath, list))
    {
        Event e;
        e.type = EvFatal;
        e.a = T("cannot write") + " " + job.listPath;
        PostEvent(e);
        Event end;
        end.type = EvEnd;
        end.n = -2;
        PostEvent(end);
        return;
    }

    std::vector<std::string> args = job.args;
    args.insert(args.begin(), {"--file_list", job.listPath, "--progress"});

    {
        Event e;
        e.type = EvLog;
        e.a = Subprocess::QuoteForDisplay(job.exe, args);
        e.n = 1; // verbose only
        PostEvent(e);
    }

    int code = -2;
    {
        std::unique_ptr<Subprocess> p(new Subprocess());
        std::string err;
        bool started;
        {
            std::lock_guard<std::mutex> g(procLock);
            started = !cancelFlag && p->Start(job.exe, args, [this](int stream, const std::string& line) {
                Event e;
                if (stream == 1)
                {
                    auto c = Split(line, '\t', false);
                    if (c[0] == "TOTAL" && c.size() >= 2) { e.type = EvTotal; e.n = atoi(c[1].c_str()); }
                    else if (c[0] == "READY") { e.type = EvReady; }
                    else if (c[0] == "DEVICE" && c.size() >= 3) { e.type = EvDevice; e.a = c[2]; }
                    else if (c[0] == "PROGRESS" && c.size() >= 3) { e.type = EvProgress; e.n = atoi(c[1].c_str()); e.m = atoi(c[2].c_str()); }
                    else if (c[0] == "FAIL" && c.size() >= 5) { e.type = EvFail; e.a = c[1]; e.b = c[2]; e.c = c[3] + "\t" + (c.size() >= 5 ? c[4] : ""); }
                    else if (c[0] == "SKIP" && c.size() >= 3) { e.type = EvSkip; e.a = c[1]; e.b = c[2]; }
                    else if (c[0] == "PLAN" && c.size() >= 3) { e.type = EvPlan; e.a = c[1]; e.b = c[2]; }
                    else if (c[0] == "ANIM" && c.size() >= 4) { e.type = EvAnim; e.a = c[1]; e.b = c[2]; e.c = c[3]; }
                    else if (c[0] == "FRAME" && c.size() >= 4) { e.type = EvFrame; e.a = c[1]; e.n = atoi(c[2].c_str()); e.m = atoi(c[3].c_str()); }
                    else { e.type = EvLog; e.a = line; e.n = 1; }
                }
                else
                {
                    e.type = EvLog;
                    e.a = line;
                    e.n = 2;
                }
                PostEvent(e);
            }, err);
            if (started) proc = std::move(p);
        }
        if (started)
        {
            code = proc->Wait();
            std::lock_guard<std::mutex> g(procLock);
            proc.reset();
        }
        else if (!cancelFlag)
        {
            Event e;
            e.type = EvFatal;
            e.a = TranslateDetail(err); // could not start the converter
            PostEvent(e);
        }
    }

    std::error_code ec;
    fs::remove(U8Path(job.listPath), ec);

    Event e;
    e.type = EvEnd;
    e.n = cancelFlag ? -1000 : code;
    PostEvent(e);
}

void DialogEvent::HandleEvents()
{
    std::deque<Event> evs;
    {
        std::lock_guard<std::mutex> g(eventLock);
        evs.swap(events);
    }
    for (const auto& e : evs)
    {
        switch (e.type)
        {
        case EvTotal:
            progressMax = e.n;
            break;
        case EvReady:
            readyReceived = true;
            readyTime = std::chrono::steady_clock::now();
            break;
        case EvDevice:
            usedDevices.push_back(e.a);
            break;
        case EvProgress:
            progressNow = e.n;
            progressMax = e.m;
            frameNow = frameTotal = 0;
            if (e.n == 1) firstDoneTime = std::chrono::steady_clock::now();
            break;
        case EvFail:
            failCount++;
        {
            // e.a kind, e.b input, e.c "output\tdetail"
            const auto tab = e.c.find('\t');
            const std::string out = e.c.substr(0, tab);
            const std::string detail = tab == std::string::npos ? "" : e.c.substr(tab + 1);
            std::string msg;
            if (e.a == "open" || e.a == "decode")
                msg = Format(L("MessageFailedOpenInputFileError").c_str(), e.b.c_str());
            else if (e.a == "write" || e.a == "encode")
                msg = Format(L("MessageFailedOpenOutputFileError").c_str(), out.c_str());
            else
                msg = L("MessageFailedProcessCaffeError") + ": " + e.b;
            if (!detail.empty()) msg += " (" + TranslateDetail(detail) + ")";
            AddLogMessage(msg);
            isLastError = true;
            break;
        }
        case EvAnim:
            AddLogMessage(Format(L2("MessageAnimated", "%s: animated (%s) -> %s").c_str(), FileName(e.a).c_str(), TranslateDetail(e.c).c_str(), e.b.c_str()));
            break;
        case EvFrame:
            frameNow = e.n;
            frameTotal = e.m;
            break;
        case EvPlan:
            // the networks that really ran: shown for the first image, for every image with the verbose log
            if (true) // every file is listed
                AddLogMessage(Format(L2("MessagePlan", "%s: %s").c_str(), FileName(e.a).c_str(), TranslatePlan(e.b).c_str()));
            planShown++;
            break;
        case EvSkip:
            skipCount++;
            AddLogMessage(Format(L("MessageNoOverwrite").c_str(), e.b.c_str()));
            break;
        case EvLog:
            if (e.n == 2)
            {
                stderrTail.push_back(e.a);
                if (stderrTail.size() > 30) stderrTail.erase(stderrTail.begin());
            }
            if ((verboseLog || e.n == 0) && !e.a.empty()) AddLogMessage(e.a); // n 0 = always shown
            break;
        case EvFailCreateDir:
            MessageBox(Format(L("MessageCreateOutDirError").c_str(), e.a.c_str()), L("MessageTitleError"), IconError);
            isLastError = true;
            break;
        case EvFatal:
            AddLogMessage(e.a);
            isLastError = true;
            break;
        case EvEnd:
            endTime = std::chrono::steady_clock::now();
            if (e.n == -1000)
                AddLogMessage(L("MessageCancelError"));
            else if (e.n != 0 && e.n != 1 && e.n != -2)
            {
                // the converter refused to run (bad option, no model, no device ...): show what it said
                isLastError = true;
                for (const auto& l : stderrTail)
                    if (verboseLog == false) AddLogMessage(TranslateDetail(l));
            }
            else if (e.n == 1)
                isLastError = true;
            else if (e.n == -2)
                isLastError = true;
            WaitThreadExit();
            break;
        }
    }

    if (gpuState == 2 && !gpuApplied)
    {
        std::lock_guard<std::mutex> g(eventLock);
        gpus = gpusPending;
        gpuError = gpuErrorPending;
        gpuApplied = true;
    }
    if (gpuState == 2 && gpuApplied && gpuShowResult)
    {
        gpuShowResult = false;
        if (gpus.empty())
            MessageBox(L2("MessageGpuNone", "No Vulkan capable GPU was found.\nConversion will fall back to the CPU (slow).\nInstall or update the GPU driver to use the GPU.") +
                (gpuError.empty() ? "" : "\n\n" + TranslateDetail(gpuError)), L("MessageTitleResult"), IconWarning);
        else
        {
            std::string list;
            for (const auto& g : gpus) list += Format("GPU %d: %s (%s, %d MB)\n", g.index, g.name.c_str(), T(g.type.c_str()).c_str(), g.heapMB);
            MessageBox(Format(L2("MessageGpuFound", "Vulkan can be used\n\n%s").c_str(), list.c_str()), L("MessageTitleResult"), IconInfo);
        }
    }
}

void DialogEvent::Exec()
{
    if (processThread.joinable()) return;
    if (!SyncMember(false)) return;

    const std::string& errTitle = L("MessageTitleError");
    if (input_str.empty() && input_str_multi.empty())
    {
        MessageBox(L("MessageInputPathCheckError"), errTitle, IconError);
        return;
    }
    if (output_str.empty())
    {
        MessageBox(L("MessageOutputPathCheckError"), errTitle, IconError);
        return;
    }
    if (outputExt.empty())
    {
        MessageBox(L("MessageOutputExtCheckError"), errTitle, IconError);
        return;
    }
    {
        const std::vector<std::string> in = input_str_multi.empty() ? std::vector<std::string>{input_str} : input_str_multi;
        std::string err = CheckInputPaths(in);
        const bool single = input_str_multi.empty() && !IsDirectory(input_str);
        if (err.empty()) err = CheckOutputPath(output_str, single, single ? input_str : std::string());
        if (!err.empty())
        {
            MessageBox(err, errTitle, IconError);
            return;
        }
    }
    if (!PathExists(CliPath()))
    {
        MessageBox(Format(L2("MessageExeNotFound", "repicturer-cli was not found:\n%s").c_str(), CliPath().c_str()), errTitle, IconError);
        return;
    }
    if (modelType < 0 || modelType >= (int)models.size())
    {
        MessageBox(L("MessageFailedOpenModelFileError"), errTitle, IconError);
        return;
    }
    {
        // the model must have the networks the selected mode needs
        const ModelEntry& m = models[modelType];
        const bool needNoise = mode != eModeScale;
        const bool needScale = mode != eModeNoise;
        bool ok = true;
        if (m.engine == EngineRealESRGAN)
            ok = m.CanMagnify(); // denoise level is not used by Real-ESRGAN
        else
        {
            if (needNoise && !m.NoiseLevelAvailable(noise_level)) ok = false;
            if (needScale && !m.CanMagnify() && !(needNoise && m.engine == EngineWaifu2x && m.noiseScale[noise_level])) ok = false;
        }
        if (!ok)
        {
            MessageBox(Format(L2("MessageModelModeCheckError", "The selected model cannot be used with this conversion mode / denoise level (%s).").c_str(), m.folder.c_str()), errTitle, IconError);
            return;
        }
    }

    // validate ncnn advanced settings
    auto validList = [](const std::string& s, int minv) {
        if (Trim(s).empty()) return true;
        for (const auto& t : Split(Trim(s), ',', false))
        {
            long v;
            if (!ParseInt(Trim(t), v) || v < minv) return false;
        }
        return true;
    };
    if (!validList(gpuIdsMulti, -1))
    {
        MessageBox(L2("MessageMultiGpuCheckError", "GPU ids must be a comma separated list such as 0,1 (-1 = CPU)"), errTitle, IconError);
        return;
    }
    const size_t ngpu = Trim(gpuIdsMulti).empty() ? 1 : Split(Trim(gpuIdsMulti), ',', false).size();
    if (!validList(tileMulti, 0) || !validList(procMulti, 1) ||
        (!Trim(tileMulti).empty() && Split(Trim(tileMulti), ',', false).size() != ngpu) ||
        (!Trim(procMulti).empty() && Split(Trim(procMulti), ',', false).size() != ngpu) || loadThreads < 1 || saveThreads < 1)
    {
        MessageBox(L2("MessageThreadsCheckError", "Split sizes / thread counts per GPU must be integers, one per GPU id"), errTitle, IconError);
        return;
    }

    SaveIni(true);

    Job job;
    job.exe = CliPath();
    job.inputStr = input_str;
    job.inputMulti = input_str_multi;
    job.outputStr = output_str;
    job.outputExt = outputExt;
    job.extList = extList;
    static int counter = 0;
#ifdef _WIN32
    const unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif
    job.listPath = JoinPath(GetTempDir(), Format("repicturer-%lu-%d.txt", pid, ++counter));

    std::vector<std::string>& a = job.args;
    a.push_back("-M"); a.push_back(modeStr);
    a.push_back("-n"); a.push_back(std::to_string(noise_level));
    if (mode != eModeNoise)
    {
        switch (scaleType)
        {
        case eScaleTypeRatio: a.push_back("-s"); a.push_back(Format("%.17g", scale_ratio)); break;
        case eScaleTypeWidth: a.push_back("-W"); a.push_back(std::to_string(scale_width)); break;
        case eScaleTypeHeight: a.push_back("-H"); a.push_back(std::to_string(scale_height)); break;
        default:
            a.push_back("-W"); a.push_back(std::to_string(scale_width));
            a.push_back("-H"); a.push_back(std::to_string(scale_height));
            break;
        }
    }
    a.push_back("-m"); a.push_back(model_dir);
    if (models[modelType].engine == EngineRealCUGAN) { a.push_back("-c"); a.push_back(std::to_string(syncgap)); }

    std::string gpuArg, tileArg, procArg;
    if (!Trim(gpuIdsMulti).empty())
    {
        gpuArg = Trim(gpuIdsMulti);
        tileArg = Trim(tileMulti);
        procArg = Trim(procMulti);
        if (tileArg.empty()) { for (size_t i = 0; i < ngpu; i++) tileArg += (i ? "," : "") + std::to_string(crop_size); }
        if (procArg.empty() || models[modelType].heavy)
        {
            // heavy models: one image at a time on every GPU
            procArg.clear();
            for (size_t i = 0; i < ngpu; i++) procArg += (i ? "," : "") + std::to_string(models[modelType].heavy ? 1 : batch_size);
        }
    }
    else
    {
        if (process == "cpu") gpuArg = "-1";
        else if (gpu_no >= 0) gpuArg = std::to_string(gpu_no);
        tileArg = std::to_string(crop_size);
        procArg = std::to_string(models[modelType].heavy ? 1 : batch_size); // heavy models: one image at a time
    }
    if (!gpuArg.empty()) { a.push_back("-g"); a.push_back(gpuArg); }
    a.push_back("-t"); a.push_back(tileArg);
    a.push_back("-j"); a.push_back(std::to_string(loadThreads) + ":" + procArg + ":" + std::to_string(saveThreads));
    if (use_tta) a.push_back("-x");
    if (output_quality >= 0) { a.push_back("-q"); a.push_back(std::to_string(output_quality)); }
    if (isOutputNoOverwrite) a.push_back("-k");
    if (verboseLog) a.push_back("-v");

    progressNow = 0;
    progressMax = 0;
    cancelFlag = false;
    isLastError = false;
    readyReceived = false;
    usedDevices.clear();
    planShown = 0;
    failCount = skipCount = 0;
    frameNow = frameTotal = 0;
    stderrTail.clear();
    logMessage.clear();
    startTime = std::chrono::steady_clock::now();
    firstDoneTime = startTime;

    processThread = std::thread(&DialogEvent::ProcessWaifu2x, this, job);
}

void DialogEvent::WaitThreadExit()
{
    if (processThread.joinable()) processThread.join();

    bool endFlag = false;
    if (!isLastError)
    {
        if (!cancelFlag)
        {
            AddLogMessage(Format(L2("MessageSummary", "%d of %d files converted (%d failed, %d skipped)").c_str(),
                std::max(0, progressNow - failCount - skipCount), progressMax, failCount, skipCount));
            AddLogMessage(L("MessageTransSuccess"));
            if (isCommandLineStart && isArgStartSuccessFinish) endFlag = true;
        }
        Waifu2xTime();
#ifdef _WIN32
        MessageBeep(MB_ICONASTERISK);
#endif
    }
    else
    {
        Waifu2xTime();
        MessageBox(L("MessageErrorHappen"), L("MessageTitleError"), IconError);
    }

    isCommandLineStart = false;

    if (endFlag || closeAfterProcess) closeRequested = true;
}

void DialogEvent::Cancel()
{
    cancelFlag = true;
    std::lock_guard<std::mutex> g(procLock);
    if (proc) proc->Kill();
}

void DialogEvent::RequestClose()
{
    SaveIni();
    if (!processThread.joinable())
        closeRequested = true;
    else
    {
#ifdef _WIN32
        MessageBeep(MB_ICONEXCLAMATION);
#endif
    }
}

void DialogEvent::OnModeChange()
{
    // enable states are derived from radioMode while drawing; this only refreshes the file name
    ReplaceAddString();
}

void DialogEvent::ScaleRadio()
{
    ReplaceAddString();
}

void DialogEvent::CheckGPU()
{
    StartGpuDetect(true);
}

void DialogEvent::OnModelChange()
{
    FixModeForModel();
    OnSetInputFilePath();
    ReplaceAddString();
}

void DialogEvent::OnSetInputFilePath(const std::string& tPath)
{
    if (!PathExists(tPath))
    {
        MessageBox(L("MessageInputCheckError"), L("MessageTitleError"), IconError);
        return;
    }

    input_str_multi.clear();
    SyncMember(true, true);

    std::string outpath = output_dir;

    if (IsDirectory(tPath))
    {
        if (output_dir.empty()) outpath = ParentPath(tPath);
        const std::string addstr(AddName());
        autoSetAddName = addstr;
        std::string dirname = FileName(tPath);
        if (dirname.empty()) dirname = Stem(tPath);
        CopyStr(editOutput, sizeof(editOutput), NativeSeparators(JoinPath(outpath, dirname + addstr)));
        CopyStr(editInput, sizeof(editInput), NativeSeparators(tPath));
    }
    else
    {
        if (output_dir.empty()) outpath = ParentPath(tPath);
        std::string outputFileName = FileName(tPath);
        const auto tailDot = outputFileName.find_last_of('.');
        if (tailDot != std::string::npos) outputFileName.erase(tailDot);
        const std::string addstr(AddName());
        autoSetAddName = addstr;
        CopyStr(editOutput, sizeof(editOutput), NativeSeparators(JoinPath(outpath, outputFileName + addstr + outputExt)));
        CopyStr(editInput, sizeof(editInput), NativeSeparators(tPath));
    }
    input_str = editInput;
    output_str = editOutput;

    SetCropSizeList(tPath);
}

void DialogEvent::OnSetInputFilePath()
{
    SyncMember(true, true);

    if (!input_str_multi.empty())
    {
        CopyStr(editInput, sizeof(editInput), T(MultiFileStr));

        const std::string addstr(AddName());
        autoSetAddName = addstr;

        std::string outpath = output_dir;
        if (output_dir.empty()) outpath = ParentPath(input_str_multi[0]);

        const std::string baseDir = input_str_multi[0];
        std::string filename = IsDirectory(baseDir) ? FileName(baseDir) : Stem(baseDir);
        CopyStr(editOutput, sizeof(editOutput), NativeSeparators(JoinPath(outpath, filename + " multi" + addstr)));
        output_str = editOutput;
    }

    SetCropSizeList("");
}

void DialogEvent::OnSetOutputFilePath(const std::string& tPath)
{
    SyncMember(true, true);

    if (!input_str.empty() || !input_str_multi.empty())
    {
        std::string path = input_str;
        if (!input_str_multi.empty()) path = input_str_multi[0];

        if (IsDirectory(path))
        {
            const std::string addstr(AddName());
            autoSetAddName = addstr;
            output_str = JoinPath(tPath, Stem(path) + addstr);
        }
        else
        {
            std::string outputFileName = FileName(path);
            const auto tailDot = outputFileName.find_last_of('.');
            if (tailDot != std::string::npos) outputFileName.erase(tailDot);
            const std::string addstr(AddName());
            autoSetAddName = addstr;
            output_str = JoinPath(tPath, outputFileName + addstr + outputExt);
        }
        CopyStr(editOutput, sizeof(editOutput), NativeSeparators(output_str));
    }
    else
    {
        CopyStr(editOutput, sizeof(editOutput), NativeSeparators(tPath));
        output_str = tPath;
    }

    output_dir = tPath;
}

void DialogEvent::OnDrop(const std::vector<std::string>& paths, float mx, float my)
{
    if (paths.empty() || ModalKind() != ModalNone) return; // a dialog window is open
    // drop on the output edit sets the output path (DropOutput), anywhere else is an input drop (DropInput)
    if (mx >= outputRectMin.x && mx <= outputRectMax.x && my >= outputRectMin.y && my <= outputRectMax.y)
    {
        // a dropped output is a folder: the file name is made from the input like Browse does
        const std::string err = IsDirectory(paths[0]) ? CheckOutputFolder(paths[0]) : T("For the output, drop a folder.");
        if (!err.empty()) { MessageBox(err, L("MessageTitleError"), IconWarning); return; }
        OnSetOutputFilePath(paths[0]);
        return;
    }
    {
        const std::string err = CheckInputPaths(paths);
        if (!err.empty()) { MessageBox(err, L("MessageTitleError"), IconWarning); return; }
    }

    if (paths.size() == 1)
        OnSetInputFilePath(paths[0]);
    else
    {
        input_str.clear();
        input_str_multi = paths;
        OnSetInputFilePath();
    }

    if (tAutoMode == "one" || (tAutoMode == "multi" && (!input_str_multi.empty() || IsDirectory(paths[0]))))
        Exec();
}

// ---- input / output rules (explorer, drag and drop, typed paths and Start use the same ones). "" = fine

// an input: existing files with one of the input extensions, or folders with at least one such file inside
std::string DialogEvent::CheckInputPaths(const std::vector<std::string>& paths) const
{
    if (paths.empty()) return L("MessageInputPathCheckError");
    std::string extStr;
    for (const auto& e : extList) extStr += (extStr.empty() ? "" : " ") + e;
    for (const auto& p : paths)
    {
        if (!PathExists(p)) return Format(T("This file or folder does not exist:\n%s").c_str(), NativeSeparators(p).c_str());
        if (IsDirectory(p))
        {
            bool any = false;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(U8Path(p), fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator() && !any; it.increment(ec))
            {
                std::error_code e2;
                if (!it->is_directory(e2) && std::find(extList.begin(), extList.end(), ToLowerAscii(Extension(PathU8(it->path())))) != extList.end())
                    any = true;
            }
            if (!any) return Format(T("This folder has no images with the input file extensions (%s):\n%s").c_str(), extStr.c_str(), NativeSeparators(p).c_str());
        }
        else if (std::find(extList.begin(), extList.end(), ToLowerAscii(Extension(p))) == extList.end())
            return Format(T("This file type is not in the input file extensions (%s):\n%s").c_str(), extStr.c_str(), NativeSeparators(p).c_str());
    }
    return "";
}

// the nearest folder of a path that exists (the output folder may still have to be made)
static std::string ExistingAncestor(const std::string& p)
{
    std::string d = p;
    for (int i = 0; i < 64 && !d.empty(); i++)
    {
        if (IsDirectory(d)) return d;
        const std::string up = ParentPath(d);
        if (up == d) break;
        d = up;
    }
    return "";
}

// a folder the program can write to: a test file is made and removed
static bool CanWrite(const std::string& dir)
{
    const std::string t = JoinPath(dir, ".w2x-write-test-" + std::to_string((unsigned long long)GetCurrentProcessIdPortable()));
    FILE* fp = OpenFileU8(t, "wb");
    if (!fp) return false;
    fclose(fp);
    std::error_code ec;
    fs::remove(U8Path(t), ec);
    return true;
}

// an output: a file name with the output extension for one image, a folder for a folder / several files
std::string DialogEvent::CheckOutputPath(const std::string& out, bool singleFile, const std::string& inputFile) const
{
    if (Trim(out).empty()) return L("MessageOutputPathCheckError");
    const std::string path = AbsolutePath(Trim(out));
#ifdef _WIN32
    // < > " | ? * are never allowed; ':' only after the drive letter
    std::string bad;
    for (size_t i = 0; i < path.size(); i++)
    {
        const char c = path[i];
        if (strchr("<>\"|?*", c) || (c == ':' && i != 1) || (unsigned char)c < 32)
            if (bad.find(c) == std::string::npos) bad += c;
    }
    if (!bad.empty()) return Format(T("The output path contains characters that are not allowed: %s").c_str(), bad.c_str());
#endif
    if (singleFile)
    {
        if (IsDirectory(path)) return T("For one image the output must be a file name, not a folder.");
        if (ToLowerAscii(Extension(path)) != ToLowerAscii(outputExt))
            return Format(T("The output file name must end with %s (the output extension).").c_str(), outputExt.c_str());
        if (!inputFile.empty() && AbsolutePath(inputFile) == path) return T("The output would overwrite the input image.");
    }
    else if (PathExists(path) && !IsDirectory(path))
        return T("For a folder or several files the output must be a folder, not a file.");
    const std::string base = ExistingAncestor(singleFile ? ParentPath(path) : path);
    if (base.empty()) return Format(T("This location does not exist:\n%s").c_str(), NativeSeparators(path).c_str());
    if (!CanWrite(base)) return Format(T("The program cannot write to this folder:\n%s").c_str(), NativeSeparators(base).c_str());
    return "";
}

// an output folder chosen in the explorer
std::string DialogEvent::CheckOutputFolder(const std::string& dir) const
{
    if (!IsDirectory(dir)) return Format(T("This location does not exist:\n%s").c_str(), NativeSeparators(dir).c_str());
    if (!CanWrite(dir)) return Format(T("The program cannot write to this folder:\n%s").c_str(), NativeSeparators(dir).c_str());
    return "";
}

// ---- file / folder selection: the program's own explorer (FileBrowser), the same on windows and linux.
//      targets: 1 input, 2 output, 3 / 4 app setting fixed folders. start folder: the given one, else the home folder
//      (C:\Users\<name> on windows, /home/<name> on linux)

void DialogEvent::StartPicker(int target, bool folder, const std::string& title, const std::string& initialDir)
{
    if (fileBrowser.IsOpen()) return; // one explorer at a time (input, output or an App Setting folder)
    fileBrowserTarget = target;
    if (target == 1) fileBrowser.validate = [this](const std::vector<std::string>& p) { return CheckInputPaths(p); };
    else if (target == 2) fileBrowser.validate = [this](const std::vector<std::string>& p) { return p.empty() ? std::string() : CheckOutputFolder(p[0]); };
    else fileBrowser.validate = nullptr;
    fileBrowser.warn = [this](const std::string& text) { MessageBox(text, L("MessageTitleError"), IconWarning); };
    fileBrowser.Open(folder ? FileBrowser::ModeFolder : FileBrowser::ModeInput, title + "##picker",
        IsDirectory(initialDir) ? initialDir : HomeFolder(), folder ? std::vector<std::string>() : extList,
        L2("OK", "OK"), L2("Cancel", "Cancel"));
}

// remembered explorer folders that no longer exist are forgotten, in setting.ini too
void DialogEvent::PruneRememberedFolders()
{
    bool changed = false;
    for (std::string* p : {&lastBrowseIn, &lastBrowseOut})
        if (!p->empty() && !IsDirectory(*p)) { p->clear(); changed = true; }
    if (changed) SaveIni(false);
}

// the first folder of the list that exists; none = the user folder (C:\Users\<name> / /home/<name>)
std::string DialogEvent::FirstExistingFolder(const std::vector<std::string>& candidates) const
{
    for (const auto& c : candidates)
        if (!c.empty() && IsDirectory(c)) return c;
    return HomeFolder();
}

void DialogEvent::InputRef()
{
    // the explorer picks files and / or a folder in one go
    SyncMember(false, true);
    PruneRememberedFolders();
    // first one that still exists: fixed folder, folder of the input, last folder used, else the user folder
    const std::string init = FirstExistingFolder({tInputDirFix, IsDirectory(input_str) ? input_str : ParentPath(input_str), lastBrowseIn});
    StartPicker(1, false, L("MessageTitleInputDialog"), init);
}

void DialogEvent::OutputRef()
{
    SyncMember(false, true);
    PruneRememberedFolders();
    // first one that still exists: fixed folder, output folder, folder of the output, last folder used, else the user folder
    const std::string init = FirstExistingFolder({tOutputDirFix, output_dir, output_str.empty() ? std::string() : ParentPath(output_str), lastBrowseOut});
    StartPicker(2, true, L2("MessageTitleOutputDialog", "Please select the output folder"), init);
}

void DialogEvent::LangChange()
{
    const auto& list = lang.GetLangList();
    if (comboLang < 0 || comboLang >= (int)list.size()) return;
    lang.SetLang(list[comboLang]);
    LangName = list[comboLang].LangName;
    SetWindowTextLang();
    // texts that sit in edit boxes follow the language too
    if (!input_str_multi.empty()) CopyStr(editInput, sizeof(editInput), T(MultiFileStr));
    SaveIni(); // remembered right away (LastLanguage), not only when the program closes
}

void DialogEvent::OutExtChange()
{
    SetQuality();
    ReplaceAddString();
}

void DialogEvent::ClearOutputDir()
{
    output_dir.clear();
    SyncMember(true, true);
    if (!input_str.empty()) OnSetInputFilePath(input_str);
    else OnSetInputFilePath();
}

void DialogEvent::AppSetting()
{
    app.autoMode = tAutoMode == "one" ? 1 : tAutoMode == "multi" ? 2 : 0;
    app.processor = process == "cpu" ? 1 : 0;
    app.argStartAuto = isArgStartAuto;
    app.argStartFinish = isArgStartSuccessFinish;
    app.noOverwrite = isOutputNoOverwrite;
    CopyStr(app.inputDirFix, sizeof(app.inputDirFix), tInputDirFix);
    CopyStr(app.outputDirFix, sizeof(app.outputDirFix), tOutputDirFix);
    app.gpuCombo = 0;
    for (size_t i = 0; i < gpus.size(); i++)
        if (gpus[i].index == gpu_no) app.gpuCombo = (int)i + 1;
    CopyStr(app.gpuIds, sizeof(app.gpuIds), gpuIdsMulti);
    CopyStr(app.tiles, sizeof(app.tiles), tileMulti);
    CopyStr(app.procs, sizeof(app.procs), procMulti);
    app.loadThreads = loadThreads;
    app.saveThreads = saveThreads;
    app.verbose = verboseLog;
    app.syncgap = syncgap;
    appSettingOpen = true;
    if (gpuState == 0) StartGpuDetect(false);
}

// ============================================================================ startup (WM_INITDIALOG)

// setting.ini check at start (silent):
//   returns false = broken: a line that is not "key=value", a section other than [Setting], a key this program does
//                   not write (renamed / typo), a key twice, or a value outside its rules -> replaced by a first start file
//   true + missing = some keys were deleted: they get their defaults when the file is written
//   no file at all = true, missing (first start)
bool DialogEvent::CheckSettingFile(bool& missing) const
{
    missing = false;
    std::string data;
    if (!ReadTextFileUtf8(SettingPath(), data)) { missing = true; return true; }
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);

    // ---- rules of every key SaveIni writes
    auto oneOf = [](const std::string& v, std::initializer_list<const char*> l) {
        for (const char* x : l) if (v == x) return true;
        return false;
    };
    auto rate = [](const std::string& v) {
        // "1.00", "2.5", "3": digits, at most 2 decimals, more than 0
        double d;
        return IsValidRateText(v.c_str()) && !v.empty() && v != "." && ParseDouble(v, d) && d > 0.0;
    };
    auto size = [](const std::string& v) { return v.empty() || IsIntIn(v, 1, 999999999); };
    auto bool01 = [&](const std::string& v) { return oneOf(v, {"0", "1"}); };
    std::map<std::string, std::string> kv;
    std::map<std::string, std::function<bool(const std::string&)>> rules = {
        {"LastScaleMode", [&](const std::string& v) { return oneOf(v, {"Ratio", "Width", "Height", "WidthHeight"}); }},
        {"LastScale", rate},
        {"LastScaleWidth", size},
        {"LastScaleHeight", size},
        {"LastScaleWidthHeight", [&](const std::string& v) {
            const size_t x = v.find('x');
            return v.empty() || (x != std::string::npos && size(v.substr(0, x)) && size(v.substr(x + 1)) && x > 0 && x + 1 < v.size());
        }},
        {"LastOutputExt", [](const std::string& v) {
            for (int i = 0; i < g_output_format_count; i++) if (v == std::string(".") + g_output_formats[i].ext) return true;
            return false;
        }},
        {"LastMode", [&](const std::string& v) { return oneOf(v, {"noise", "scale", "noise_scale", "auto_scale"}); }},
        {"LastNoiseLevel", [](const std::string& v) { return IsIntIn(v, 0, 3); }},
        {"LastProcess", [&](const std::string& v) { return oneOf(v, {"gpu", "cpu"}); }},
        {"LastModelType", [this](const std::string& v) {
            if (models.empty()) return v == DefaultModelType; // no models-* folder: SaveIni writes the default name
            for (const auto& m : models) if (m.type == v) return true;
            return false;
        }},
        {"LastUseTTA", bool01},
        {"LastDeviceNo", [](const std::string& v) { return IsIntIn(v, -1, 63); }},
        {"LastOutputQuality", [](const std::string&) { return true; }}, // checked with the format below
        {"LastLanguage", [this](const std::string& v) {
            for (const auto& l : lang.GetLangList()) if (l.LangName == v) return true;
            return false;
        }},
        {"LastAutoMode", [&](const std::string& v) { return oneOf(v, {"none", "one", "multi"}); }},
        {"LastIsArgStartAuto", bool01},
        {"LastIsArgStartSuccessFinish", bool01},
        {"LastIsOutputNoOverwrite", bool01},
        {"LastInputDirFix", [](const std::string&) { return true; }},  // any folder
        {"LastOutputDirFix", [](const std::string&) { return true; }},
        {"LastBatchSize", [](const std::string& v) { return IsIntIn(v, 1, 1024); }},
        {"LastCropSize", [](const std::string& v) { return v == "0" || IsIntIn(v, 32, 65536); }},
        {"LastGpuIds", [](const std::string& v) { return IsValidIntList(v, -1, 63); }},
        {"LastTileSizes", [](const std::string& v) { return IsValidTileList(v); }},
        {"LastProcThreads", [](const std::string& v) { return IsValidIntList(v, 1, 1024); }},
        {"LastLoadThreads", [](const std::string& v) { return IsIntIn(v, 1, 64); }},
        {"LastSaveThreads", [](const std::string& v) { return IsIntIn(v, 1, 64); }},
        {"LastVerboseLog", bool01},
        {"LastSyncGap", [](const std::string& v) { return IsIntIn(v, 0, 3); }},
        {"LastBrowserSort", [](const std::string& v) { return IsIntIn(v, 0, 1); }},
        {"LastBrowserSortDesc", bool01},
        {"LastBrowserFolderIn", [](const std::string&) { return true; }},
        {"LastBrowserFolderOut", [](const std::string&) { return true; }},
    };

    // ---- the file: [Setting] and key=value lines only, known keys, each once
    std::string section;
    std::istringstream is(data);
    std::string line;
    while (std::getline(is, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[')
        {
            if (line != "[Setting]") return false;
            section = "Setting";
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || section != "Setting") return false;
        const std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (std::find(std::begin(LegacyBrowserKeys), std::end(LegacyBrowserKeys), key) != std::end(LegacyBrowserKeys)) { missing = true; continue; } // dropped on save
        auto r = rules.find(key);
        if (r == rules.end() || kv.count(key) || !r->second(value)) return false;
        kv[key] = value;
    }

    // ---- values that depend on each other: the quality must fit the output format
    const auto ext = kv.find("LastOutputExt");
    const OutputFormatInfo* fmt = ext != kv.end() ? find_output_format(ext->second.c_str()) : find_output_format("png");
    const auto q = kv.find("LastOutputQuality");
    if (q != kv.end())
    {
        if (fmt->has_quality ? !IsIntIn(q->second, fmt->quality_min, fmt->quality_max) : !q->second.empty()) return false;
    }

    missing = missing || kv.size() != rules.size();
    return true;
}

void DialogEvent::Create(int argc, char** argv)
{
    exeDir = GetExeDir();

    fileBrowser.tr = [this](const char* s) { return T(s); };
    lang.SetLangBaseDir(JoinPath(exeDir, LangDir));
    lang.ReadLangList(JoinPath(exeDir, LangListFileName));
    ScanModels(); // before the setting check: LastModelType must name an installed model

    // broken setting.ini (renamed key, wrong value ...): start over from a first start file; deleted keys: their
    // defaults are added when the file is written below. silent, no message
    bool settingMissing = false;
    if (!CheckSettingFile(settingMissing))
    {
        std::error_code ec;
        fs::remove(U8Path(SettingPath()), ec);
        if (PathExists(SettingPath())) WriteTextFileUtf8(SettingPath(), ""); // could not delete: empty it
        settingMissing = true;
    }

    IniFile ini;
    ini.Load(SettingPath());
    const char* S = "Setting";

    const std::string langName = ini.Get(S, "LastLanguage", "");
    {
        const auto& list = lang.GetLangList();
        size_t defaultListIndex = 0;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].FileName == lang.GetLang().FileName) { defaultListIndex = i; LangName = list[i].LangName; break; }
        if (!langName.empty())
            for (size_t i = 0; i < list.size(); i++)
                if (list[i].LangName == langName)
                {
                    defaultListIndex = i;
                    lang.SetLang(list[i]);
                    LangName = langName;
                    break;
                }
        comboLang = (int)defaultListIndex;
    }

#ifndef _WIN32
    {
        // executable bit lost (zip / ntfs copy): restore it so the converter can be started
        std::error_code ec;
        const fs::path cli = U8Path(CliPath());
        if (fs::exists(cli, ec) && access(CliPath().c_str(), X_OK) != 0)
            fs::permissions(cli, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    }
#endif
    StartGpuDetect(false);

    {
        std::string s;
        if (ReadTextFileUtf8(JoinPath(exeDir, CropSizeListName), s))
            for (const auto& line : Split(s, '\n', true))
            {
                long n;
                if (ParseInt(Trim(line), n)) CropSizeList.push_back((int)n);
            }
    }

    std::string tScaleRatio = ini.Get(S, "LastScale", "1.00");
    std::string tScaleWidth = ini.Get(S, "LastScaleWidth", "0");
    std::string tScaleHeight = ini.Get(S, "LastScaleHeight", "0");
    std::string tScaleWidthHeight = ini.Get(S, "LastScaleWidthHeight", "0");
    std::string tScaleMode = ini.Get(S, "LastScaleMode", "Ratio");
    outputExt = ini.Get(S, "LastOutputExt", "png");
    std::string tmode = ini.Get(S, "LastMode", "noise_scale");
    noise_level = ini.GetInt(S, "LastNoiseLevel", 0);
    std::string tprocess = ini.Get(S, "LastProcess", "gpu");
    std::string tModelType = ini.Get(S, "LastModelType", DefaultModelType);
    use_tta = ini.GetInt(S, "LastUseTTA", 0) != 0;
    gpu_no = ini.GetInt(S, "LastDeviceNo", -1);
    output_quality = -1;
    {
        const int num = ini.GetInt(S, "LastOutputQuality", -100);
        if (num != -100) output_quality = num;
    }
    tAutoMode = ini.Get(S, "LastAutoMode", "none");
    isArgStartAuto = ini.GetInt(S, "LastIsArgStartAuto", 1) != 0;
    isArgStartSuccessFinish = ini.GetInt(S, "LastIsArgStartSuccessFinish", 1) != 0;
    isOutputNoOverwrite = ini.GetInt(S, "LastIsOutputNoOverwrite", 0) != 0;
    tInputDirFix = ini.Get(S, "LastInputDirFix", "");
    tOutputDirFix = ini.Get(S, "LastOutputDirFix", "");
    batch_size = ini.GetInt(S, "LastBatchSize", 2);
    std::string tCrop = ini.Get(S, "LastCropSize", "0");
    gpuIdsMulti = ini.Get(S, "LastGpuIds", "");
    tileMulti = ini.Get(S, "LastTileSizes", "");
    procMulti = ini.Get(S, "LastProcThreads", "");
    loadThreads = std::max(1, ini.GetInt(S, "LastLoadThreads", 1));
    saveThreads = std::max(1, ini.GetInt(S, "LastSaveThreads", 2));
    verboseLog = ini.GetInt(S, "LastVerboseLog", 0) != 0;
    syncgap = std::min(3, std::max(0, ini.GetInt(S, "LastSyncGap", 3)));
    {
        // the explorer (FileBrowser) as it was left
        FileBrowser::Prefs& bp = fileBrowser.prefs;
        bp.sortColumn = std::min(1, std::max(0, ini.GetInt(S, "LastBrowserSort", 0)));
        bp.sortDescending = ini.GetInt(S, "LastBrowserSortDesc", 0) != 0;
        lastBrowseIn = ini.Get(S, "LastBrowserFolderIn", "");
        lastBrowseOut = ini.Get(S, "LastBrowserFolderOut", "");
    }

    double tempScale;
    if (!ParseDouble(tScaleRatio, tempScale) || tempScale <= 0.0) tScaleRatio = "1.00";
    long tl;
    if (!ParseInt(tScaleWidth, tl) || tl <= 0) tScaleWidth = "";
    if (!ParseInt(tScaleHeight, tl) || tl <= 0) tScaleHeight = "";
    if (!outputExt.empty() && outputExt[0] != '.') outputExt = "." + outputExt;
    if (!(0 <= noise_level && noise_level <= 3)) noise_level = 0;
    process = tprocess == "cpu" ? "cpu" : "gpu";

    modelType = 0;
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].type == FallbackModelType) modelType = (int)i;
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].type == DefaultModelType) modelType = (int)i;
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].type == tModelType) modelType = (int)i;
    comboModel = modelType;

    if (tScaleMode == "Ratio") radioScale = eScaleTypeRatio;
    else if (tScaleMode == "Width") radioScale = eScaleTypeWidth;
    else if (tScaleMode == "Height") radioScale = eScaleTypeHeight;
    else radioScale = eScaleTypeWidthHeight;

    radioNoise = noise_level;

    if (tmode == "noise") radioMode = 2;
    else if (tmode == "scale") radioMode = 1;
    else if (tmode == "auto_scale") radioMode = 3;
    else radioMode = 0;

    // crop (split) size list without an input image
    isSetInitCrop = false;
    comboCrop[0] = 0;
    SetCropSizeList("");
    CopyStr(comboCrop, sizeof(comboCrop), tCrop);

    checkTTA = use_tta;
    if (!(1 <= batch_size && batch_size <= 64)) batch_size = 2;
    CopyStr(comboBatch, sizeof(comboBatch), std::to_string(batch_size));

    SetScaleRatioText(tScaleRatio); // older settings stored 6 decimals: shown rounded to 2
    CopyStr(editScaleWidth, sizeof(editScaleWidth), tScaleWidth);
    CopyStr(editScaleHeight, sizeof(editScaleHeight), tScaleHeight);
    SetScaleWHText(tScaleWidthHeight);
    CopyStr(editExtList, sizeof(editExtList), ExtListDisplay());

    if (!tOutputDirFix.empty() && PathExists(tOutputDirFix))
    {
        output_dir = tOutputDirFix;
        CopyStr(editOutput, sizeof(editOutput), output_dir);
    }

    outExtIndex = 0;
    for (int i = 0; i < g_output_format_count; i++)
        if (std::string(".") + g_output_formats[i].ext == outputExt) { outExtIndex = i; break; }

    if (output_quality >= 0)
        CopyStr(editQuality, sizeof(editQuality), std::to_string(output_quality));

    SetQuality(output_quality < 0);

    FixModeForModel();

    ParseCommandLine(argc, argv);

    // first start, a replaced (broken) file or deleted keys: write setting.ini now with every key, so it holds the
    // language in use (English on a first start) and all defaults from the beginning, not only after closing
    if (langName != LangName || settingMissing) SaveIni();
    PruneRememberedFolders();

    isSetInitCrop = false;
}

void DialogEvent::ParseCommandLine(int argc, char** argv)
{
    // same options as waifu2x-caffe-gui (TCLAP definitions in MainDialog.cpp) + ncnn extras
    input_str_multi.clear();
    if (argc <= 1) return;

    std::vector<std::string> inputs;
    std::map<std::string, std::string> v;
    std::set<std::string> isSet;
    struct Opt { const char* s; const char* l; };
    static const Opt opts[] = {
        {"o", "output_folder"}, {"l", "input_extention_list"}, {"e", "output_extention"}, {"m", "mode"},
        {"n", "noise_level"}, {"s", "scale_ratio"}, {"w", "scale_width"}, {"h", "scale_height"}, {"p", "process"},
        {"q", "output_quality"}, {"d", "output_depth"}, {"c", "crop_size"}, {"b", "batch_size"}, {"", "gpu"},
        {"t", "tta"}, {"", "auto_start"}, {"", "auto_exit"}, {"", "no_overwrite"}, {"y", "model_type"},
        {"j", "jobs"}, {"", "verbose_log"},
    };
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        const Opt* found = nullptr;
        std::string val;
        bool inlineVal = false;
        if (a.size() >= 2 && a[0] == '-' && !(a.size() > 1 && isdigit((unsigned char)a[1])))
        {
            std::string name = a[1] == '-' ? a.substr(2) : a.substr(1);
            size_t eq = name.find('=');
            if (eq != std::string::npos) { val = name.substr(eq + 1); name = name.substr(0, eq); inlineVal = true; }
            for (const auto& o : opts)
                if ((a[1] == '-' && name == o.l) || (a[1] != '-' && name == o.s && o.s[0])) found = &o;
            if (!found) continue; // TCLAPW::Arg::enableIgnoreMismatched()
            if (!inlineVal)
            {
                if (i + 1 >= argc) break;
                val = argv[++i];
            }
            v[found->l] = val;
            isSet.insert(found->l);
        }
        else
            inputs.push_back(a);
    }

    bool isSetParam = false;
    auto has = [&](const char* k) { return isSet.count(k) != 0; };

    if (has("output_folder")) { OnSetOutputFilePath(v["output_folder"]); isSetParam = true; }
    // input_extention_list: accepted for old command lines, the types are fixed
    if (has("output_extention"))
    {
        std::string e = v["output_extention"];
        const OutputFormatInfo* f = find_output_format(e.c_str());
        if (f)
            for (int i = 0; i < g_output_format_count; i++)
                if (&g_output_formats[i] == f) { outExtIndex = i; SetQuality(); }
        isSetParam = true;
    }
    if (has("mode"))
    {
        const std::string m = v["mode"];
        radioMode = m == "noise" ? 2 : m == "scale" ? 1 : m == "auto_scale" ? 3 : 0;
        isSetParam = true;
    }
    if (has("noise_level"))
    {
        long n;
        if (ParseInt(v["noise_level"], n) && n >= 0 && n <= 3) radioNoise = (int)n;
        isSetParam = true;
    }
    if (has("scale_width") && has("scale_height"))
    {
        radioScale = eScaleTypeWidthHeight;
        if (!SetScaleWHText(v["scale_width"] + "x" + v["scale_height"])) ShowScaleWHWarning();
        isSetParam = true;
    }
    else if (has("scale_width"))
    {
        radioScale = eScaleTypeWidth;
        if (IsValidWHText(v["scale_width"].c_str()) && v["scale_width"].find('x') == std::string::npos) CopyStr(editScaleWidth, sizeof(editScaleWidth), v["scale_width"]);
        else ShowScaleNumWarning();
        isSetParam = true;
    }
    else if (has("scale_height"))
    {
        radioScale = eScaleTypeHeight;
        if (IsValidWHText(v["scale_height"].c_str()) && v["scale_height"].find('x') == std::string::npos) CopyStr(editScaleHeight, sizeof(editScaleHeight), v["scale_height"]);
        else ShowScaleNumWarning();
        isSetParam = true;
    }
    else if (has("scale_ratio"))
    {
        radioScale = eScaleTypeRatio;
        if (!SetScaleRatioText(v["scale_ratio"])) ShowScaleRateWarning();
        isSetParam = true;
    }
    if (has("process")) { process = v["process"] == "cpu" ? "cpu" : "gpu"; isSetParam = true; }
    if (has("output_quality")) { CopyStr(editQuality, sizeof(editQuality), v["output_quality"]); isSetParam = true; }
    // output_depth (-d): accepted for waifu2x-caffe command lines, the output is always 8 bit
    if (has("crop_size")) { CopyStr(comboCrop, sizeof(comboCrop), v["crop_size"]); isSetParam = true; isSetInitCrop = true; }
    if (has("batch_size")) { CopyStr(comboBatch, sizeof(comboBatch), v["batch_size"]); isSetParam = true; }
    if (has("gpu"))
    {
        const std::string g = v["gpu"];
        if (g.find(',') != std::string::npos) gpuIdsMulti = g;
        else
        {
            long n;
            if (ParseInt(g, n)) { if (n < 0) process = "cpu"; else { process = "gpu"; gpu_no = (int)n; } }
        }
        isSetParam = true;
    }
    if (has("tta")) { checkTTA = v["tta"] != "0"; isSetParam = true; }
    if (has("auto_start")) { isArgStartAuto = v["auto_start"] != "0"; isSetParam = true; }
    if (has("auto_exit")) { isArgStartSuccessFinish = v["auto_exit"] != "0"; isSetParam = true; }
    if (has("model_type"))
    {
        for (size_t i = 0; i < models.size(); i++)
            if (models[i].type == v["model_type"] || models[i].folder == v["model_type"]) comboModel = (int)i;
        isSetParam = true;
    }
    if (has("no_overwrite")) { isOutputNoOverwrite = v["no_overwrite"] != "0"; isSetParam = true; }
    if (has("jobs"))
    {
        auto c = Split(v["jobs"], ':', false);
        if (c.size() == 3)
        {
            loadThreads = std::max(1, atoi(c[0].c_str()));
            if (c[1].find(',') != std::string::npos) procMulti = c[1];
            else CopyStr(comboBatch, sizeof(comboBatch), c[1]);
            saveThreads = std::max(1, atoi(c[2].c_str()));
        }
        isSetParam = true;
    }
    if (has("verbose_log")) { verboseLog = v["verbose_log"] != "0"; isSetParam = true; }

    if (isSetParam) isNotSaveParam = true;

    if (inputs.size() > 1)
    {
        input_str_multi = inputs;
        OnSetInputFilePath();
    }
    else if (inputs.size() == 1)
        OnSetInputFilePath(inputs[0]);

    if (!inputs.empty() && isArgStartAuto)
    {
        isCommandLineStart = true;
        Exec();
    }
}

// ============================================================================ drawing helpers

namespace
{
    float g_s = 1.0f;

    ImVec2 P(float x, float y) { return ImVec2(x * DLU_X * g_s, y * DLU_Y * g_s); }
    float W(float w) { return w * DLU_X * g_s; }
    float H(float h) { return h * DLU_Y * g_s; }

    ImU32 LineColor() { return ImGui::GetColorU32(ImGuiCol_Border); }

    // ---- labels fit their space in every language: a text that is too long first gets a smaller font (down to 75 %),
    //      then it is cut with "..." and the full text is shown as a tooltip
    float g_fitRight = 0; // dialog-unit right edge for radio / check labels: inner edge of the last group box

    float TextW(const std::string& s, float size) { return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x; }

    struct FitLabel { std::string text; float size; bool cut; bool fits; };
    FitLabel Fit(const std::string& s, float avail)
    {
        const float fs = ImGui::GetFontSize();
        FitLabel f{s, fs, false, true};
        if (avail <= 0 || TextW(s, fs) <= avail) return f;
        f.fits = false;
        f.size = std::max(std::floor(fs * 0.75f), std::floor(fs * avail / TextW(s, fs)));
        if (TextW(s, f.size) <= avail) return f;
        std::string t = s;
        while (!t.empty() && TextW(t + "...", f.size) > avail)
        {
            size_t n = t.size() - 1; // drop one utf-8 character
            while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;
            t.erase(n);
        }
        f.text = t + "...";
        f.cut = true;
        return f;
    }

    // a fitted text drawn at a screen position (left aligned, top)
    void DrawFit(const FitLabel& f, ImVec2 pos, const std::string& full)
    {
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), f.size, pos, ImGui::GetColorU32(ImGuiCol_Text), f.text.c_str());
        if (f.cut && ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + TextW(f.text, f.size), pos.y + f.size)))
            ImGui::SetTooltip("%s", full.c_str());
    }

    void GroupBox(const std::string& title, float x, float y, float w, float h)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetWindowPos();
        ImVec2 p0 = ImVec2(o.x + x * DLU_X * g_s, o.y + y * DLU_Y * g_s);
        ImVec2 p1 = ImVec2(p0.x + W(w), p0.y + H(h));
        const float fh = ImGui::GetFontSize();
        const float top = p0.y + fh * 0.5f;
        const float tx = p0.x + 6 * g_s;
        const FitLabel f = Fit(title, (p1.x - 6 * g_s) - tx);
        const float tw = TextW(f.text, f.size);
        dl->AddLine(ImVec2(p0.x, top), ImVec2(tx - 2 * g_s, top), LineColor());
        if (tx + tw + 2 * g_s < p1.x)
            dl->AddLine(ImVec2(tx + tw + 2 * g_s, top), ImVec2(p1.x, top), LineColor());
        dl->AddLine(ImVec2(p0.x, top), ImVec2(p0.x, p1.y), LineColor());
        dl->AddLine(ImVec2(p1.x, top), ImVec2(p1.x, p1.y), LineColor());
        dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), LineColor());
        DrawFit(f, ImVec2(tx, top - f.size * 0.5f), title);
        g_fitRight = x + w - 4;
    }

    // mouse over a dialog-unit rectangle (tooltips for composite controls)
    bool HoverRect(float x, float y, float w, float h)
    {
        ImVec2 o = ImGui::GetWindowPos();
        ImVec2 a(o.x + x * DLU_X * g_s, o.y + y * DLU_Y * g_s);
        return ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
               ImGui::IsMouseHoveringRect(a, ImVec2(a.x + W(w), a.y + H(h)));
    }

    // static text centered in a box (CTEXT)
    void CText(const std::string& text, float x, float y, float w, float h)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetWindowPos();
        std::vector<std::string> lines = Split(text, '\n', false);
        const float lh = ImGui::GetTextLineHeight();
        float total = lh * lines.size();
        float cy = o.y + y * DLU_Y * g_s + (H(h) - total) * 0.5f;
        for (const auto& l : lines)
        {
            const FitLabel f = Fit(l, W(w));
            const float tw = TextW(f.text, f.size);
            (void)dl;
            DrawFit(f, ImVec2(o.x + x * DLU_X * g_s + (W(w) - tw) * 0.5f, cy + (lh - f.size) * 0.5f), l);
            cy += lh;
        }
    }

    // left aligned static text; right = dialog-unit edge it must not cross (0 = no limit)
    void LText(const std::string& text, float x, float y, float right = 0)
    {
        const FitLabel f = Fit(text, right > 0 ? W(right - x) : 0);
        ImVec2 o = ImGui::GetWindowPos();
        const float fs = ImGui::GetFontSize();
        DrawFit(f, ImVec2(o.x + P(x, y).x, o.y + P(x, y).y + (fs - f.size) * 0.5f), text);
    }

    // label of a field row (14 high, like Edit / Combo): vertically centred on the field; right = edge it must not cross
    void RowLabel(const std::string& text, float x, float y, float right)
    {
        const FitLabel f = Fit(text, W(right - x));
        const ImVec2 o = ImGui::GetWindowPos();
        DrawFit(f, ImVec2(o.x + P(x, 0).x, o.y + P(0, y).y + (H(14) - f.size) * 0.5f), text);
    }

    // vertically center a frame of the current font inside a dialog-unit box
    void At(float x, float y, float h)
    {
        ImVec2 p = P(x, y);
        p.y += (H(h) - ImGui::GetFrameHeight()) * 0.5f;
        ImGui::SetCursorPos(p);
    }

    // h = 0: a field-high button, vertically centred in a 12 high row at y (like At)
    bool Button(const std::string& label, const char* id, float x, float y, float w, float h, bool enabled = true)
    {
        if (h > 0) ImGui::SetCursorPos(P(x, y));
        else At(x, y, 12);
        ImGui::BeginDisabled(!enabled);
        const FitLabel f = Fit(label, W(w) - ImGui::GetStyle().FramePadding.x * 2);
        if (!f.fits) ImGui::PushFont(nullptr, f.size);
        bool r = ImGui::Button((f.text + "##" + id).c_str(), ImVec2(W(w), h > 0 ? H(h) : ImGui::GetFrameHeight()));
        if (!f.fits) ImGui::PopFont();
        if (f.cut && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", label.c_str());
        ImGui::EndDisabled();
        return r;
    }

    // "?" button: square, exactly as tall as the drop-down / edit box beside it, right edge at x + w (dialog units)
    bool HelpButton(const char* id, float x, float y, float w)
    {
        const float fh = ImGui::GetFrameHeight();
        At(x, y, 14);
        ImGui::SetCursorPosX(P(x + w, 0).x - fh);
        return ImGui::Button((std::string("?##") + id).c_str(), ImVec2(fh, fh));
    }

    // small button beside an edit box: as tall as the edit box
    bool InlineButton(const char* label, const char* id, float x, float y, float w)
    {
        At(x, y, 14);
        return ImGui::Button((std::string(label) + "##" + id).c_str(), ImVec2(W(w), ImGui::GetFrameHeight()));
    }

    // radio button / check box whose label is fitted to the space up to `right` (dialog units, 0 = last group box)
    template <class Widget>
    bool FittedToggle(const std::string& label, const char* id, float x, float y, float right, Widget widget)
    {
        ImGui::SetCursorPos(ImVec2(P(x, y).x, P(x, y).y + (H(10) - ImGui::GetFrameHeight()) * 0.5f));
        const float r = right > 0 ? right : g_fitRight;
        const float avail = r > 0 ? W(r) - P(x, y).x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x : 0;
        const FitLabel f = Fit(label, avail);
        if (f.fits) return widget((label + "##" + id).c_str());
        // smaller / cut label drawn next to the box; clicking it works like clicking the box
        bool changed = widget((std::string("##") + id).c_str());
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float fh = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), f.size, ImVec2(pos.x, pos.y + (fh - f.size) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), f.text.c_str());
        if (ImGui::InvisibleButton((std::string("##lbl") + id).c_str(), ImVec2(std::max(1.0f, TextW(f.text, f.size)), fh)))
            changed |= widget(nullptr);
        if (f.cut && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", label.c_str());
        return changed;
    }

    bool Edit(const char* id, char* buf, size_t n, float x, float y, float w, float h, bool enabled = true, ImGuiInputTextFlags flags = 0)
    {
        At(x, y, h);
        ImGui::SetNextItemWidth(W(w));
        ImGui::BeginDisabled(!enabled);
        bool r = ImGui::InputText(id, buf, n, flags);
        ImGui::EndDisabled();
        return r;
    }

    // widget(nullptr) = the label was clicked
    // locked: drawn like a normal option but it does not light up on hover / press (the only choice, always selected)
    bool Radio(const std::string& label, const char* id, int* v, int value, float x, float y, bool enabled = true, float right = 0, bool locked = false)
    {
        ImGui::BeginDisabled(!enabled);
        if (locked)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        }
        bool r = FittedToggle(label, id, x, y, right, [&](const char* l) {
            if (!l) { const bool c = *v != value; *v = value; return c; }
            return ImGui::RadioButton(l, v, value);
        });
        if (locked) { ImGui::PopStyleColor(2); *v = value; r = false; }
        ImGui::EndDisabled();
        return r;
    }

    bool Check(const std::string& label, const char* id, bool* v, float x, float y, bool enabled = true, float right = 0)
    {
        ImGui::BeginDisabled(!enabled);
        bool r = FittedToggle(label, id, x, y, right, [&](const char* l) {
            if (!l) { *v = !*v; return true; }
            return ImGui::Checkbox(l, v);
        });
        ImGui::EndDisabled();
        return r;
    }

    // previewText: what the closed box shows instead of the selected item (nullptr = the item)
    bool Combo(const char* id, int* cur, const std::vector<std::string>& items, float x, float y, float w, bool enabled = true,
               const std::string* previewText = nullptr, const std::vector<std::string>* shortItems = nullptr)
    {
        At(x, y, 14);
        ImGui::SetNextItemWidth(W(w));
        ImGui::BeginDisabled(!enabled);
        bool changed = false;
        const std::string preview = previewText ? *previewText : (*cur >= 0 && *cur < (int)items.size()) ? items[*cur] : std::string();
        // the selected text is drawn by hand: centred in the box left of the arrow, fitted when it is too long
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float fh = ImGui::GetFrameHeight();
        const float padX = ImGui::GetStyle().FramePadding.x;
        const float textL = p0.x + padX, textR = p0.x + W(w) - fh - padX;
        if (ImGui::BeginCombo(id, "", ImGuiComboFlags_HeightLarge))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
            for (int i = 0; i < (int)items.size(); i++)
            {
                if (ImGui::Selectable(items[i].c_str(), i == *cur)) { if (*cur != i) changed = true; *cur = i; }
                if (i == *cur) ImGui::SetItemDefaultFocus();
            }
            ImGui::PopStyleVar();
            ImGui::EndCombo();
        }
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        // too long at the normal size: the short form (full name in the list and the tooltip)
        const bool useShort = shortItems && !previewText && *cur >= 0 && *cur < (int)shortItems->size() &&
            ImGui::CalcTextSize(preview.c_str()).x > textR - textL;
        const std::string shown = useShort ? (*shortItems)[*cur] : preview;
        const FitLabel f = Fit(shown, textR - textL);
        const float tw = TextW(f.text, f.size);
        dl->PushClipRect(ImVec2(textL, p0.y), ImVec2(textR, p0.y + fh), true);
        dl->AddText(ImGui::GetFont(), f.size, ImVec2(std::max(textL, (textL + textR - tw) * 0.5f), p0.y + (fh - f.size) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), f.text.c_str());
        dl->PopClipRect();
        if ((f.cut || useShort) && hovered && !ImGui::IsPopupOpen(id)) ImGui::SetTooltip("%s", preview.c_str());
        ImGui::EndDisabled();
        return changed;
    }

    // editable combo box (CBS_DROPDOWN): text field + list
    bool EditCombo(const char* id, char* buf, size_t n, const std::vector<std::string>& items, float x, float y, float w, bool enabled = true)
    {
        At(x, y, 14);
        ImGui::BeginDisabled(!enabled);
        const float bw = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(W(w) - bw);
        bool changed = ImGui::InputText((std::string("##edit") + id).c_str(), buf, n, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine(0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginCombo(id, nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLarge))
        {
            for (const auto& it : items)
            {
                const bool sep = it == CropSeparator;
                if (sep) { ImGui::Separator(); continue; }
                if (ImGui::Selectable(it.c_str(), it == buf)) { snprintf(buf, n, "%s", it.c_str()); changed = true; }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        return changed;
    }
}

// ============================================================================ frame

void DialogEvent::Draw(float scale)
{
    g_s = scale;
    g_fitRight = 0;
    HandleEvents();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    // content column: 665 dialog units wide at the current scale, centred horizontally, pinned to the top
    {
        const float colW = std::min(vp->Size.x, 665.0f * DLU_X * scale);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + std::floor((vp->Size.x - colW) * 0.5f), vp->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(colW, vp->Size.y));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    // a dialog (app setting, message, browser ...) is open in its own window: the main window waits for it
    ImGui::BeginDisabled(ModalKind() != ModalNone);
    DrawMain(scale);
    ImGui::EndDisabled();

    if (!fileBrowser.IsOpen() && (fileBrowserTarget == 3 || fileBrowserTarget == 4))
    {
        // browser closed (ok or cancel) while editing the app settings: the settings window comes back
        fileBrowserTarget = 0;
    }

    ImGui::End();
}

// ============================================================================ converter messages in the ui language

// "%s (tile %s)" against "GeForce GTX 1050 (tile auto)": the parts in place of %s, false when the text does not fit
static bool MatchPattern(const std::string& pat, const std::string& text, std::vector<std::string>& caps)
{
    caps.clear();
    std::vector<std::string> pieces;
    for (size_t pos = 0;;)
    {
        const size_t q = pat.find("%s", pos);
        pieces.push_back(pat.substr(pos, q == std::string::npos ? std::string::npos : q - pos));
        if (q == std::string::npos) break;
        pos = q + 2;
    }
    if (pieces.size() < 2) return false;
    const std::string& first = pieces.front();
    const std::string& last = pieces.back();
    if (text.size() < first.size() + last.size() || text.compare(0, first.size(), first) != 0 ||
        text.compare(text.size() - last.size(), last.size(), last) != 0)
        return false;
    size_t pos = first.size();
    const size_t end = text.size() - last.size();
    for (size_t i = 1; i + 1 < pieces.size(); i++)
    {
        const size_t q = text.find(pieces[i], pos + 1); // every %s is at least one character
        if (q == std::string::npos || q > end) return false;
        caps.push_back(text.substr(pos, q - pos));
        pos = q + pieces[i].size();
    }
    if (end <= pos) return false;
    caps.push_back(text.substr(pos, end - pos));
    return true;
}

// a message of the converter (failure detail, error, device name ...) in the ui language: fixed texts are looked up,
// texts with names / numbers in them are matched against the "...%s..." patterns of the language files
std::string DialogEvent::TranslateDetail(const std::string& d) const
{
    if (d.empty()) return d;
    const std::string t = T(d.c_str());
    if (t != d) return t;
    // the patterns: english keys with %s, the longest (most specific) first
    std::vector<std::string> patterns;
    for (const auto& kv : lang.DefaultMap())
        if (kv.first.find("%s") != std::string::npos && kv.first != "%s: %s") patterns.push_back(kv.first);
    std::sort(patterns.begin(), patterns.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    std::vector<std::string> caps;
    for (const auto& p : patterns)
    {
        if (!MatchPattern(p, d, caps)) continue;
        std::string out = T(p.c_str()), res;
        size_t i = 0;
        for (size_t pos = 0;;)
        {
            const size_t q = out.find("%s", pos);
            if (q == std::string::npos) { res += out.substr(pos); break; }
            res += out.substr(pos, q - pos) + (i < caps.size() ? TranslateDetail(caps[i]) : std::string());
            i++;
            pos = q + 2;
        }
        return res;
    }
    return d;
}

// "models-cunet/noise1_scale2.0x (x2) -> resize 100x100 to 200x200 (jpeg: denoised)"
std::string DialogEvent::TranslatePlan(std::string d) const
{
    const std::string nonet = "no network";
    if (d.rfind(nonet, 0) == 0) d = T(nonet.c_str()) + d.substr(nonet.size());
    const size_t r = d.find("resize ");
    if (r == std::string::npos) return d;
    const size_t to = d.find(" to ", r);
    if (to == std::string::npos) return d;
    const size_t end = d.find(" (", to);
    const std::string a = d.substr(r + 7, to - (r + 7));
    const std::string b = d.substr(to + 4, end == std::string::npos ? std::string::npos : end - (to + 4));
    std::string out = d.substr(0, r) + Format(T("resize %s to %s").c_str(), a.c_str(), b.c_str());
    if (end != std::string::npos) out += " " + T(d.substr(end + 1).c_str());
    return out;
}

// ============================================================================ dialogs in their own os windows

int DialogEvent::ModalKind() const
{
    if (!msgBoxes.empty()) return ModalMessage;
    if (fileBrowser.IsOpen()) return ModalBrowser;
    if (appSettingOpen) return ModalAppSetting;
    if (resetConfirmRequest) return ModalReset;
    return ModalNone;
}

std::string DialogEvent::ModalTitle(int kind) const
{
    switch (kind)
    {
    case ModalMessage: return msgBoxes.empty() ? std::string(APP_NAME) : msgBoxes.front().title;
    case ModalBrowser: return fileBrowser.Title();
    case ModalAppSetting: return L("IDC_BUTTON_APP_SETTING");
    case ModalReset: return L2("MessageResetTitle", "Reset to Default");
    }
    return APP_NAME;
}

// the window's own close button = Cancel / OK of a message
void DialogEvent::CloseModal(int kind)
{
    if (kind == ModalMessage && !msgBoxes.empty()) msgBoxes.pop_front();
    else if (kind == ModalBrowser) fileBrowser.Close();
    else if (kind == ModalAppSetting) appSettingOpen = false;
    else if (kind == ModalReset) resetConfirmRequest = false;
}

void DialogEvent::DrawModal(int kind, float scale, ImVec2& fitSize)
{
    g_s = scale;
    g_fitRight = 0;
    fitSize = ImVec2(0, 0);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    if (kind == ModalBrowser)
    {
        // resizable window, the browser fills it
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("##browserhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        const bool accepted = fileBrowser.Draw(scale);
        if (accepted)
        {
            if (fileBrowserTarget == 1) lastBrowseIn = fileBrowser.CurrentFolder();
            else if (fileBrowserTarget == 2) lastBrowseOut = fileBrowser.CurrentFolder();
        }
        if (!fileBrowser.IsOpen()) SaveIni(false); // sort, last folders kept
        ImGui::End();
        if (accepted) OnBrowserAccepted();
        return;
    }
    // fixed dialogs: the os window is sized to fit the content (fitSize)
    // the window fills the os window; the os window takes the size of the content. (an auto-resize window cannot be
    // used to measure: imgui limits it to the os window it is in, which made the dialog shrink to nothing)
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##modal", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (kind == ModalMessage) DrawMessageBoxes(scale);
    else if (kind == ModalAppSetting) DrawAppSetting(scale);
    else if (kind == ModalReset) DrawResetConfirm(scale);
    const ImGuiWindow* win = ImGui::GetCurrentWindow();
    // ContentSize is the size of everything drawn (last frame), whatever the window size; 0 on the very first frame
    if (win->ContentSize.x > 0 && win->ContentSize.y > 0)
        fitSize = ImVec2(win->ContentSize.x + ImGui::GetStyle().WindowPadding.x * 2, win->ContentSize.y + ImGui::GetStyle().WindowPadding.y * 2);
    ImGui::End();
}

void DialogEvent::OnBrowserAccepted()
{
    ApplyPicked(fileBrowserTarget, fileBrowser.Result());
}

// paths chosen in a system dialog or the built-in browser
void DialogEvent::ApplyPicked(int target, const std::vector<std::string>& r)
{
    {
        const int fileBrowserTarget = target;
        if (fileBrowserTarget == 1 && !r.empty())
        {
            input_str.clear();
            input_str_multi.clear();
            if (r.size() == 1) OnSetInputFilePath(r[0]);
            else { input_str_multi = r; OnSetInputFilePath(); }
            if (tAutoMode == "one" || (tAutoMode == "multi" && (!input_str_multi.empty() || IsDirectory(r[0]))))
                Exec();
        }
        else if (fileBrowserTarget == 2 && !r.empty())
            OnSetOutputFilePath(r[0]);
        else if (fileBrowserTarget == 3 && !r.empty())
            CopyStr(app.inputDirFix, sizeof(app.inputDirFix), NativeSeparators(r[0]));
        else if (fileBrowserTarget == 4 && !r.empty())
            CopyStr(app.outputDirFix, sizeof(app.outputDirFix), NativeSeparators(r[0]));
        if (target == 3 || target == 4) this->fileBrowserTarget = 0;
    }
}

void DialogEvent::DrawMain(float s)
{
    const bool running = processThread.joinable();

    // ---- Input/Output Settings
    GroupBox(L("IDC_STATIC_IO_SETTING"), 7, 7, 651, 86);
    CText(L("IDC_STATIC_INPUT_PATH"), 13, 20, 81, 20);

    {
        At(98, 24, 12);
        ImGui::SetNextItemWidth(W(496));
        if (ImGui::InputText("##input", editInput, sizeof(editInput)))
        {
            input_str_multi.clear();
            input_str = editInput;
        }
        inputRectMin = ImGui::GetItemRectMin();
        inputRectMax = ImGui::GetItemRectMax();
        if (ImGui::IsItemDeactivatedAfterEdit() && PathExists(editInput))
            OnSetInputFilePath(std::string(editInput));
    }
    if (Button(L("IDC_BUTTON_INPUT_REF"), "inref", 600, 24, 54, 0)) InputRef();


    CText(L("IDC_STATIC_OUTPUT_PATH"), 13, 43, 81, 8);
    {
        At(98, 41, 12);
        ImGui::SetNextItemWidth(W(496));
        if (ImGui::InputText("##output", editOutput, sizeof(editOutput))) output_str = editOutput;
        outputRectMin = ImGui::GetItemRectMin();
        outputRectMax = ImGui::GetItemRectMax();
    }
    if (Button(L("IDC_BUTTON_OUTPUT_REF"), "outref", 600, 41, 54, 0)) OutputRef();

    {
        // fixed by the program: plain text, the box wraps it (6 units on each side, at most up to Output extension)
        const float textW = ImGui::CalcTextSize(editExtList).x / (DLU_X * g_s);
        const float boxW = std::min(195.0f, std::ceil(textW) + 12);
        GroupBox(L("IDC_STATIC_TANS_EXT_LIST"), 13, 57, boxW, 31);
        RowLabel(editExtList, 19, 69, 13 + boxW - 5);
    }

    GroupBox(L("IDC_STATIC_OUTPUT_EXT"), 212, 57, 78, 30);
    {
        std::vector<std::string> items;
        for (int i = 0; i < g_output_format_count; i++)
        {
            std::string e = g_output_formats[i].ext; // shown upper case only
            for (char& c : e) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            items.push_back(e);
        }
        if (Combo("##outext", &outExtIndex, items, 218, 69, 66)) OutExtChange();
    }

    GroupBox(qualityTitle.empty() ? L("IDC_STATIC_OUTPUT_QUALITY") : qualityTitle, 295, 57, 94, 30);
    {
        // whole numbers in the format's range only (jpg 0-100, webp 1-101, tga 0-1), no leading zero
        At(300, 69, 14);
        ImGui::SetNextItemWidth(W(84));
        ImGui::BeginDisabled(!qualityEnabled);
        const bool wasActive = ImGui::GetActiveID() == ImGui::GetID("##quality");
        if (!wasActive) qualityLastValid = editQuality; // text set by the program (format change, settings)
        ImGui::InputText("##quality", editQuality, sizeof(editQuality),
            ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit, &DialogEvent::QualityCallback, this);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && outExtIndex >= 0 && outExtIndex < g_output_format_count)
        {
            // what the value means for the chosen format
            const std::string ext = g_output_formats[outExtIndex].ext;
            std::string tip;
            if (ext == "jpg") tip = T("JPG quality: 0-100. Higher = better quality and bigger file. Default 100.");
            else if (ext == "webp") tip = T("WebP quality: 1-100 = lossy (higher = better quality, bigger file), 101 = lossless. Default 101.");
            else if (ext == "tga") tip = T("TGA: 0 = no compression, 1 = RLE compression (lossless, smaller file). Default 0.");
            else
            {
                std::string up = ext;
                for (char& c : up) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                tip = Format(T("%s has no quality setting.").c_str(), up.c_str());
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (qualityWarn)
        {
            qualityWarn = false;
            ShowQualityWarning();
        }
    }

    GroupBox(L("IDC_STATIC_OUTPUT_DEPTH"), 395, 57, 76, 29);
    {
        // fixed at 8 bit: plain text like the input file extensions, the reason in the tooltip
        RowLabel(T("8-bit"), 401, 68, 467);
        if (HoverRect(395, 57, 76, 29))
            ImGui::SetTooltip("%s", T("RePicturer only supports 8-bit output.\n"
                "The AI models work in 8-bit, so a higher bit depth would not add any real color detail.").c_str());
    }

    // ---- two rows right of "Output depth bits", two columns (labels 476-571, controls 575-654), each centred:
    //        Language:      [ English      v ]
    //      VERSION: 1.0     [Reset to Default]
    CText(L2("IDC_STATIC_LANGUAGE", "Language:"), 476, 59, 95, 14);
    {
        std::vector<std::string> items;
        for (const auto& l : lang.GetLangList()) items.push_back(l.LangName);
        if (Combo("##lang", &comboLang, items, 575, 59, 79)) LangChange();
    }
    CText("VERSION: " APP_VERSION_NUM, 476, 75, 95, 14);
    if (Button(L("IDC_BUTTON_CLEAR_OUTPUT_DIR"), "clearout", 575, 75, 79, 14, !running)) resetConfirmRequest = true;

    // ---- Conversion Image Quality & Processing Settings
    GroupBox(L("IDC_STATIC_QUALITY_PROCESS_SETTING"), 7, 97, 495, 94);

    const ModelEntry* cm = CurrentModel();
    const bool isEsrgan = cm && cm->engine == EngineRealESRGAN;
    const bool isCugan = cm && cm->engine == EngineRealCUGAN;
    const bool canDenoise = !cm || cm->AnyDenoise();
    const bool canMagnify = !cm || cm->CanMagnify();
    // the selected model's name for tooltips ("NMKD Siax"): the list title before " - ", or the folder of a custom model
    std::string modelName;
    if (cm)
    {
        modelName = cm->folder;
        for (const auto& b : g_builtinModels)
            if (cm->langKey == b.langKey) modelName = L2(b.langKey, b.fallbackTitle);
        modelName = modelName.substr(0, modelName.find(" - "));
    }

    // models that allow a single mode: that option is locked (drawn normally, always selected) and every option says why
    //   built-in cleanup (Real-ESRGAN x4plus, NMKD, Nomos8kSC ...): the cleanup cannot be turned off -> Scale only
    //   no denoise network (Real-CUGAN NoSE)                                                          -> Scale only
    //   no scale network (custom models)                                                              -> Denoise only
    const bool builtIn = cm && cm->BuiltInCleanup();
    const bool scaleOnly = cm && canMagnify && (builtIn || !canDenoise);
    const bool denoiseOnly = cm && !canMagnify && canDenoise;
    std::string onlyTip;
    if (builtIn && canMagnify) onlyTip = Format(T("%s only allows scaling conversion, denoising is always turned on by default.").c_str(), modelName.c_str());
    else if (scaleOnly) onlyTip = Format(T("%s only allows scaling conversion, it has no denoising.").c_str(), modelName.c_str());
    else if (denoiseOnly) onlyTip = Format(T("%s only allows denoising, it has no scaling.").c_str(), modelName.c_str());
    auto onlyModeTip = [&]() {
        if (!onlyTip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", onlyTip.c_str());
    };
    GroupBox(L("IDC_STATIC_TRANS_MODE"), 13, 108, 121, 76);
    bool modeChanged = false;
    modeChanged |= Radio(L("IDC_RADIO_MODE_NOISE_SCALE"), "m0", &radioMode, 0, 21, 121, canDenoise && canMagnify && !builtIn);
    onlyModeTip();
    modeChanged |= Radio(L("IDC_RADIO_MODE_SCALE"), "m1", &radioMode, 1, 21, 137, canMagnify, 0, scaleOnly);
    onlyModeTip();
    modeChanged |= Radio(L("IDC_RADIO_MODE_NOISE"), "m2", &radioMode, 2, 21, 153, canDenoise && !builtIn, 0, denoiseOnly);
    if (!onlyTip.empty()) onlyModeTip();
    else if (isEsrgan && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", Format(L2("TipModelDenoiseOnly", "%s: the image is cleaned up at the network scale and shrunk back to the original size").c_str(), modelName.c_str()).c_str());
    modeChanged |= Radio(L("IDC_RADIO_AUTO_SCALE"), "m3", &radioMode, 3, 21, 169, canDenoise && canMagnify && !builtIn);
    if (!onlyTip.empty()) onlyModeTip();
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", L2("TipAutoScale", "Scales every image but denoises only JPEG sources (like waifu2x-caffe).\n"
            "PNG and other images are only scaled - use \"Denoise & Scale\" to always do both.").c_str());
    if (modeChanged) OnModeChange();

    const bool isNoise = radioMode != 1;
    const bool isScale = radioMode != 2;

    // models with detail levels (NMKD Siax): "Detail level" with its own four options, in every mode
    const bool detailLevels = cm && cm->HasDetailLevels();
    GroupBox(detailLevels ? T("Detail level") : L("IDC_STATIC_JPEG_NOISE_LEVEL"), 139, 109, 65, 75);
    bool noiseChanged = false;
    for (int lv = 0; lv < 4; lv++)
    {
        static const char* keys[4] = {"IDC_RADIONOISE_LEVEL0", "IDC_RADIONOISE_LEVEL1", "IDC_RADIONOISE_LEVEL2", "IDC_RADIONOISE_LEVEL3"};
        const std::string detailName = lv == 0 ? T("Faithful") : lv == 1 ? T("Balanced") : lv == 2 ? T("Detailed") : T("Maximum");
        static const char* ids[4] = {"n0", "n1", "n2", "n3"};
        const bool esrLevels = isEsrgan && cm->NoiseLevelAvailable(0);
        const bool en = detailLevels || (isNoise && (!isEsrgan || esrLevels) && (!cm || cm->NoiseLevelAvailable(lv)));
        noiseChanged |= Radio(detailLevels ? detailName : L(keys[lv]), ids[lv], &radioNoise, lv, 148, 121.f + 16.f * lv, en);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (detailLevels)
                ImGui::SetTooltip("%s", Format(T("%s: the detail level blends it with ESRGAN PSNR, the soft model it was trained from\n"
                    "Faithful = 40 %%, Balanced = 60 %%, Detailed = 80 %%, Maximum = 100 %% (plain %s). Used in every mode").c_str(),
                    modelName.c_str(), modelName.c_str()).c_str());
            else if (esrLevels && cm->type == "lsdir-compact")
                ImGui::SetTooltip("%s", T("LSDIR Compact: the level sets how much it cleans up\n"
                    "Level 0 = JPEG only, Level 1-2 = more, Level 3 = blur, noise and JPEG (Scale only = no cleanup, most detail)").c_str());
            else if (esrLevels)
                ImGui::SetTooltip("%s", T("Real-ESRGAN General: the level sets the denoise strength\n"
                    "Level 0 = 25 %, Level 1 = 50 %, Level 2 = 75 %, Level 3 = 100 % (Scale only = weakest)").c_str());
            else if (isEsrgan)
                ImGui::SetTooltip("%s", Format(L2("TipModelNoNoise", "%s removes noise / JPEG artifacts / blur by itself; there is no denoise level").c_str(), modelName.c_str()).c_str());
            else if (cm && !canDenoise)
                ImGui::SetTooltip("%s", Format(T("%s only allows scaling conversion, it has no denoising.").c_str(), modelName.c_str()).c_str());
            else if (isCugan && lv == 0)
                ImGui::SetTooltip("%s", L2("TipCuganLevel0", "Real-CUGAN: Level 0 = conservative (keeps the most original detail)").c_str());
            else if (lv == 0)
                ImGui::SetTooltip("%s", L2("TipLevel0", "Level 0 is the lightest denoise: on clean images the difference is small.\n"
                    "Use Level 1-3 for visible noise or JPEG artifacts.").c_str());
            else if (cm && !cm->NoiseLevelAvailable(lv))
                ImGui::SetTooltip("%s", L2("TipLevelMissing", "this model has no network for this denoise level").c_str());
        }
    }
    if (noiseChanged) ReplaceAddString();

    GroupBox(L("IDC_STATIC_SCALE_RATE"), 209, 109, 155, 75);
    bool scaleChanged = false;
    scaleChanged |= Radio(L("IDC_RADIO_SCALE_RATIO"), "s0", &radioScale, 0, 213, 121, isScale, 309);
    scaleChanged |= Radio(L("IDC_RADIO_SCALE_WIDTH"), "s1", &radioScale, 1, 213, 137, isScale, 309);
    scaleChanged |= Radio(L("IDC_RADIO_SCALE_HEIGHT"), "s2", &radioScale, 2, 213, 153, isScale, 309);
    scaleChanged |= Radio(L("IDC_RADIO_SCALE_WIDTH_HEIGHT"), "s3", &radioScale, 3, 213, 169, isScale, 309);
    if (scaleChanged) ScaleRadio();
    {
        // rate: digits and one point, at most 2 decimals; anything else is blocked with a warning
        At(311, 119, 14);
        ImGui::SetNextItemWidth(W(49));
        ImGui::BeginDisabled(!(isScale && radioScale == 0));
        if (ImGui::InputText("##sratio", editScaleRatio, sizeof(editScaleRatio),
                ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit, &DialogEvent::ScaleRatioCallback, this))
            ReplaceAddString();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L2("TipScaleRate", "numbers only, up to 2 decimals (e.g. 1.5, 2.25)").c_str());
        if (scaleRateWarn)
        {
            scaleRateWarn = false;
            ShowScaleRateWarning();
        }
    }
    {
        // <Width> / <Height>: digits only, anything else is blocked with a warning
        const char* ids[2] = {"##swidth", "##sheight"};
        char* bufs[2] = {editScaleWidth, editScaleHeight};
        const size_t sizes[2] = {sizeof(editScaleWidth), sizeof(editScaleHeight)};
        for (int k = 0; k < 2; k++)
        {
            At(311, k == 0 ? 135.f : 151.f, 14);
            ImGui::SetNextItemWidth(W(49));
            ImGui::BeginDisabled(!(isScale && (radioScale == k + 1 || radioScale == 3))); // <Width>x<Height> uses both
            if (ImGui::InputText(ids[k], bufs[k], sizes[k], ImGuiInputTextFlags_CallbackCharFilter, &DialogEvent::DigitsOnlyCallback, this))
                ReplaceAddString();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", L2("TipScaleNum", "whole numbers only (0-9), in pixels").c_str());
        }
        if (scaleNumWarn)
        {
            scaleNumWarn = false;
            ShowScaleNumWarning();
        }
    }
    {
        // <Width>x<Height> has no boxes of its own: it takes the Width and Height boxes above
        // two small lines, centred in the place of a box (x 311, width 49) on the row (y 167, height 14)
        const bool active = isScale && radioScale == 3;
        const std::vector<std::string> lines = Split(T("Enter your\nvalues above"), '\n', false);
        const float fs = ImGui::GetFontSize() * 0.8f, lh = fs * 1.05f;
        const ImVec2 o = ImGui::GetWindowPos(), p = P(311, 167);
        float y = o.y + p.y + (H(14) - lh * lines.size()) * 0.5f;
        for (const auto& line : lines)
        {
            const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, line.c_str()).x;
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fs, ImVec2(o.x + p.x + (W(49) - tw) * 0.5f, y),
                ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), line.c_str());
            y += lh;
        }
        if (HoverRect(311, 167, 51, 14))
            ImGui::SetTooltip("%s", T("Enter your desired <Width> and <Height> values in two text fields above.").c_str());
    }

    GroupBox(L("IDC_STATIC_MODEL"), 368, 109, 130, 33);
    {
        std::vector<std::string> items;
        for (const auto& m : models)
        {
            std::string t;
            if (!m.langKey.empty())
            {
                const BuiltinModel* b = nullptr;
                for (const auto& bm : g_builtinModels) if (m.langKey == bm.langKey) b = &bm;
                t = L2(m.langKey.c_str(), b ? b->fallbackTitle : m.folder.c_str());
            }
            else
                t = m.folder;
            items.push_back(t);
        }
        if (items.empty()) items.push_back(T("(no models-* folder)"));
        // the list entries are longer than the box: let the popup grow
        // and at most 10 rows high: more models scroll (the selected one is scrolled into view)
        const float listMaxH = ImGui::GetTextLineHeightWithSpacing() * 10 + ImGui::GetStyle().WindowPadding.y * 2;
        ImGui::SetNextWindowSizeConstraints(ImVec2(W(215), 0), ImVec2(FLT_MAX, listMaxH));
        // the closed box shows the model name only ("Real-CUGAN SE"), the list shows name - description
        std::string shortName;
        if (comboModel >= 0 && comboModel < (int)items.size())
            shortName = items[comboModel].substr(0, items[comboModel].find(" - "));
        if (Combo("##model", &comboModel, items, 372, 120, 110, true, &shortName)) OnModelChange();
        if (comboModel >= 0 && comboModel < (int)items.size() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", items[comboModel].c_str());
        if (HelpButton("modelhelp", 483, 120, 13)) modelHelpRequest = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L2("TipModelHelp", "Which model for which picture? Every model explained").c_str());
    }

    // ncnn: device selection (vulkan gpu / cpu) right on the main window
    GroupBox(L2("IDC_STATIC_DEVICE", "Device"), 368, 143, 130, 29);
    {
        int idx = DeviceComboIndex();
        std::vector<std::string> items = DeviceComboItems();
        std::vector<std::string> shortItems = items; // "GPU 0" when the name does not fit
        for (size_t i = 0; i < gpus.size(); i++) shortItems[i + 1] = Format("GPU %d", gpus[i].index);
        if (gpuState == 1)
        {
            items[0] += " - " + L2("MessageDetecting", "detecting...");
            shortItems[0] = L2("MessageDetecting", "detecting...");
        }
        if (Combo("##device", &idx, items, 372, 154, 122, true, nullptr, &shortItems)) SetDeviceFromCombo(idx);
        if (!Trim(gpuIdsMulti).empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %s", L2("IDC_STATIC_MULTI_GPU", "Multi GPU ids").c_str(), gpuIdsMulti.c_str());
    }

    if (Check(L("IDC_CHECK_TTA"), "tta", &checkTTA, 371, 176)) ReplaceAddString();

    // ---- Processing Speed Settings
    // speed box as tall as the quality box; split size / thread(s) boxes the same size, controls centred in them
    GroupBox(L2("IDC_STATIC_PROCESS_SPEED_SETTING", "Processing Speed"), 507, 97, 82, 94);

    GroupBox(L("IDC_STATIC_CROP_SIZE"), 512, 107, 72, 36);
    EditCombo("##crop", comboCrop, sizeof(comboCrop), cropItems, 516, 121.5f, 46);
    if (HoverRect(512, 107, 50, 36))
        ImGui::SetTooltip("%s", L2("TipCropSize", "Size of the pieces the image is cut into for the GPU.\n"
            "0 = automatic from the free GPU memory (recommended).\n"
            "Bigger = a little faster but needs more memory. Click ? for details.").c_str());
    if (HelpButton("crophelp", 565, 121.5f, 15)) speedHelpRequest = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L2("TipSpeedHelp", "What do Split size and Thread(s) do?").c_str());

    GroupBox(L2("IDC_STATIC_PROC_THREADS", "Thread(s)"), 512, 151, 72, 36);
    {
        std::vector<std::string> items;
        for (int i = 1; i <= MaxBatchSizeList; i++) items.push_back(std::to_string(i));
        // heavy models: greyed out and shown as 1 (the user's value is kept for the other models)
        const bool heavy = cm && cm->heavy;
        char one[4] = "1";
        if (heavy) EditCombo("##batch", one, sizeof(one), items, 516, 165.5f, 46, false);
        else EditCombo("##batch", comboBatch, sizeof(comboBatch), items, 516, 165.5f, 46);
        if (heavy && HoverRect(512, 151, 50, 36))
            ImGui::SetTooltip("%s", Format(T("Thread(s) is locked at 1 for heavy models like %s:\n"
                "each GPU converts one image at a time, so it does not run out of GPU memory").c_str(), modelName.c_str()).c_str());
        else if (HoverRect(512, 151, 50, 36))
            ImGui::SetTooltip("%s", L2("TipThreads", "GPU: how many images are converted at the same time (helps folders / many files).\n"
                "CPU fallback: how many CPU cores are used.\n"
                "Default 2. Click ? for details.").c_str());
        if (HelpButton("threadhelp", 565, 165.5f, 15)) speedHelpRequest = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L2("TipSpeedHelp", "What do Split size and Thread(s) do?").c_str());
    }

    // one column, same gap (8) between the buttons: from the top of "Split size" to the bottom of "Thread(s)"
    if (Button(L("IDC_BUTTON_APP_SETTING"), "appset", 604, 107, 54, 14)) AppSetting();
    if (Button(L2("IDC_BUTTON_CHECK_GPU", "Check GPU"), "checkgpu", 604, 129, 54, 14, !running)) CheckGPU();
    if (Button(L("IDC_BUTTON_EXEC"), "exec", 604, 151, 54, 14, !running)) Exec();
    if (Button(L("IDC_BUTTON_CANCEL"), "cancel", 604, 173, 54, 14, running && !cancelFlag)) Cancel();

    // ---- progress
    {
        ImGui::SetCursorPos(P(7, 196));
        float frac = progressMax > 0 ? (float)progressNow / (float)progressMax : 0.0f;
        // animated file in progress: move within the current file by frames
        if (running && progressMax > 0 && frameTotal > 0)
            frac = std::min(1.0f, (progressNow + (float)frameNow / (float)frameTotal) / (float)progressMax);
        std::string overlay;
        if (running)
        {
            overlay = Format("%d / %d", progressNow, progressMax);
            if (frameTotal > 0)
                overlay += Format("   %s %d/%d", L2("MessageFrame", "frame").c_str(), frameNow, frameTotal);
            // time left, estimated from the second file on like caffe
            if (progressNow >= 2 && progressMax > progressNow)
            {
                const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - firstDoneTime).count();
                const double avg = el / (progressNow - 1);
                const long long left = (long long)ceil(avg * (progressMax - progressNow));
                overlay += Format("   %s: %02lld:%02lld:%02lld", L("MessageTimeLeft").c_str(), left / 3600, (left / 60) % 60, left % 60);
            }
            else if (progressMax != progressNow)
                overlay += "   " + L("MessageTimeLeftUnknown");
        }
        ImGui::ProgressBar(frac, ImVec2(W(651), H(14)), overlay.empty() ? "" : overlay.c_str());
    }

    // ---- log
    {
        ImGui::SetCursorPos(P(7, 214));
        ImGui::BeginChild("##log", ImVec2(W(651), H(logHeightDlu)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
        static size_t lastLen = 0;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::TextUnformatted(logMessage.c_str());
        ImGui::PopStyleVar();
        if (logMessage.size() != lastLen)
        {
            ImGui::SetScrollHereY(1.0f);
            lastLen = logMessage.size();
        }
        if (ImGui::BeginPopupContextWindow("##logctx"))
        {
            if (ImGui::MenuItem(T("Copy").c_str())) ImGui::SetClipboardText(logMessage.c_str());
            if (ImGui::MenuItem(T("Clear").c_str())) logMessage.clear();
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }
    (void)s;
}

void DialogEvent::DrawAppSetting(float s)
{
    // drawn inside its own os window (DrawModal); child with the dialog-unit coordinate system of IDD_DIALOG_APP_SETTING
    ImGui::BeginChild("##appsetbody", ImVec2(W(309), H(284)), 0, ImGuiWindowFlags_NoScrollbar);

    GroupBox(L("IDC_STATIC_AUTO_START"), 7, 7, 144, 62);
    Radio(L("IDC_RADIO_AUTO_START_NONE"), "as0", &app.autoMode, 0, 14, 19);
    Radio(L("IDC_RADIO_AUTO_START_ONE"), "as1", &app.autoMode, 1, 14, 34);
    {
        std::string t = L("IDC_RADIO_AUTO_START_MULTI");
        Radio(t, "as2", &app.autoMode, 2, 14, 49);
    }

    GroupBox(L("IDC_STATIC_PROCESSOR"), 158, 7, 144, 38);
    Radio(L2("IDC_RADIO_MODE_VULKAN", "GPU (Vulkan)"), "pgpu", &app.processor, 0, 165, 19);
    Radio(L("IDC_RADIO_MODE_CPU"), "pcpu", &app.processor, 1, 165, 32);

    Check(L("IDC_CHECK_OUTPUT_NO_OVERWRITE"), "noow", &app.noOverwrite, 14, 76, true, 152); // same row as "Exit when finished"

    GroupBox(L("IDC_STATIC_ARG_START"), 158, 51, 144, 38); // ends at 89, above the GPU row
    Check(L("IDC_CHECK_ARG_START_AUTO"), "argauto", &app.argStartAuto, 165, 63);
    Check(L("IDC_CHECK_ARG_START_SUCCESS_FINISH"), "argfin", &app.argStartFinish, 165, 76);

    // rows of 14 with a gap of 5; label column 7-75 (vertically centred on the field), fields from 77
    RowLabel(L("IDC_STATIC_USE_GPU_NO"), 7, 97, 75);
    {
        std::vector<std::string> items;
        items.push_back(L2("DeviceAuto", "Auto (GPU, CPU if no GPU)"));
        for (const auto& g : gpus) items.push_back(Format("%d: %s", g.index, g.name.c_str()));
        std::vector<std::string> shortItems = items;
        for (size_t i = 0; i < gpus.size(); i++) shortItems[i + 1] = Format("GPU %d", gpus[i].index);
        if (gpuState == 1)
        {
            items[0] += " - " + L2("MessageDetecting", "detecting...");
            shortItems[0] = L2("MessageDetecting", "detecting...");
        }
        Combo("##gpuno", &app.gpuCombo, items, 77, 97, 225, app.processor == 0, nullptr, &shortItems);
    }

    RowLabel(L("IDC_STATIC_INPUT_DIR_FIX"), 7, 116, 75);
    Edit("##indirfix", app.inputDirFix, sizeof(app.inputDirFix), 77, 116, 201, 14);
    if (InlineButton("...", "indirfixbtn", 282, 116, 20))
    {
        StartPicker(3, true, L("IDC_STATIC_INPUT_DIR_FIX"), app.inputDirFix);
    }
    RowLabel(L("IDC_STATIC_OUTPUT_DIR_FIX"), 7, 135, 75);
    Edit("##outdirfix", app.outputDirFix, sizeof(app.outputDirFix), 77, 135, 201, 14);
    if (InlineButton("...", "outdirfixbtn", 282, 135, 20))
    {
        StartPicker(4, true, L("IDC_STATIC_OUTPUT_DIR_FIX"), app.outputDirFix);
    }

    // ncnn-vulkan extras
    GroupBox(L2("IDC_STATIC_NCNN_ADVANCED", "ncnn-vulkan (advanced)"), 7, 156, 295, 104);
    RowLabel(L2("IDC_STATIC_MULTI_GPU", "Multi GPU ids"), 14, 170, 118);
    Edit("##gpuids", app.gpuIds, sizeof(app.gpuIds), 120, 170, 70, 14);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L2("TipMultiGpu", "e.g. 0,1 to use two GPUs at once, -1 = CPU. Empty = use the device above").c_str());
    RowLabel(L2("IDC_STATIC_TILE_PER_GPU", "Split size per GPU"), 14, 189, 118);
    Edit("##tiles", app.tiles, sizeof(app.tiles), 120, 189, 70, 14);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L2("TipTilePerGpu", "one value per GPU id, e.g. 0,200. Empty = main window split size").c_str());
    RowLabel(L2("IDC_STATIC_PROC_PER_GPU", "Thread(s) per GPU"), 14, 208, 118);
    Edit("##procs", app.procs, sizeof(app.procs), 120, 208, 70, 14);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L2("TipProcPerGpu", "one value per GPU id, e.g. 2,2. Empty = Thread(s) of the main window").c_str());

    RowLabel(L2("IDC_STATIC_LOAD_THREADS", "Load threads"), 198, 170, 253);
    At(255, 170, 14);
    ImGui::SetNextItemWidth(W(40));
    ImGui::InputInt("##loadthr", &app.loadThreads, 0, 0);
    RowLabel(L2("IDC_STATIC_SAVE_THREADS", "Save threads"), 198, 189, 253);
    At(255, 189, 14);
    ImGui::SetNextItemWidth(W(40));
    ImGui::InputInt("##savethr", &app.saveThreads, 0, 0);
    app.loadThreads = std::max(1, std::min(64, app.loadThreads));
    app.saveThreads = std::max(1, std::min(64, app.saveThreads));

    Check(L2("IDC_CHECK_VERBOSE_LOG", "Show repicturer-cli output in the log"), "verbose", &app.verbose, 14, 228);

    RowLabel(L2("IDC_STATIC_SYNCGAP", "Real-CUGAN sync gap"), 14, 241, 118);
    {
        std::vector<std::string> items = {
            L2("SyncGap0", "0 - off (fastest, tiles may differ in tone)"),
            L2("SyncGap1", "1 - accurate (slowest)"),
            L2("SyncGap2", "2 - rough"),
            L2("SyncGap3", "3 - very rough (default)")};
        Combo("##syncgap", &app.syncgap, items, 120, 241, 175);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L2("TipSyncGap", "Real-CUGAN SE / Pro: how the colour statistics are shared between tiles of a big image").c_str());
    }

    bool ok = Button(L2("OK", "OK"), "appok", 198, 267, 50, 14);
    bool cancel = Button(L2("Cancel", "Cancel"), "appcancel", 252, 267, 50, 14);
    ImGui::EndChild();

    if (ok)
    {
        tAutoMode = app.autoMode == 1 ? "one" : app.autoMode == 2 ? "multi" : "none";
        process = app.processor == 1 ? "cpu" : "gpu";
        isArgStartAuto = app.argStartAuto;
        isArgStartSuccessFinish = app.argStartFinish;
        isOutputNoOverwrite = app.noOverwrite;
        tInputDirFix = app.inputDirFix;
        tOutputDirFix = app.outputDirFix;
        gpu_no = app.gpuCombo <= 0 || app.gpuCombo > (int)gpus.size() ? -1 : gpus[app.gpuCombo - 1].index;
        gpuIdsMulti = Trim(app.gpuIds);
        tileMulti = Trim(app.tiles);
        procMulti = Trim(app.procs);
        loadThreads = app.loadThreads;
        saveThreads = app.saveThreads;
        verboseLog = app.verbose;
        syncgap = app.syncgap;
        if (!tOutputDirFix.empty() && PathExists(tOutputDirFix))
            output_dir = tOutputDirFix;
        SaveIni();
    }
    if (ok || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
        appSettingOpen = false;
}

void DialogEvent::DrawMessageBoxes(float s)
{
    if (msgBoxes.empty()) return;
    // drawn inside its own os window (DrawModal), titled with m.title
    const MsgBox m = msgBoxes.front();
    ImGui::Dummy(ImVec2(280 * s, 0)); // minimum width
    const char* icon = m.icon == IconError ? "(x)" : m.icon == IconWarning ? "(!)" : "(i)";
    ImGui::TextUnformatted(icon);
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560 * s);
    ImGui::TextUnformatted(m.text.c_str());
    ImGui::PopTextWrapPos();
    const float right = std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x, ImGui::GetCursorStartPos().x + 280 * s);
    ImGui::Spacing();
    const float bw = 90 * s;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - bw));
    if (ImGui::Button((L2("OK", "OK") + "##msgok").c_str(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        msgBoxes.pop_front();
}

// ============================================================================ model guide ("?" next to the model list)

void DialogEvent::FixModeForModel()
{
    const ModelEntry* cm = CurrentModel();
    if (!cm) return;
    if (!cm->CanMagnify() && radioMode != 2) radioMode = 2;   // denoise only
    if (!cm->AnyDenoise() && radioMode != 1) radioMode = 1;   // magnify only
    // built-in cleanup (cannot be turned off): only "Scale only"
    if (cm->BuiltInCleanup() && cm->CanMagnify()) radioMode = 1;
    if ((cm->engine != EngineRealESRGAN || cm->NoiseLevelAvailable(0)) && radioMode != 1 && !cm->NoiseLevelAvailable(radioNoise))
    {
        // nearest available level
        for (int d = 1; d < 4; d++)
        {
            if (radioNoise - d >= 0 && cm->NoiseLevelAvailable(radioNoise - d)) { radioNoise -= d; break; }
            if (radioNoise + d <= 3 && cm->NoiseLevelAvailable(radioNoise + d)) { radioNoise += d; break; }
        }
    }
}

namespace
{
    struct UseCase { const char* content; const char* best; const char* also; const char* avoid; };
    const UseCase g_useCases[] = {
        {"Anime / manga / 2D illustration / digital painting",
         "Real-CUGAN SE",
         "waifu2x CUnet (most faithful, softer), Real-ESRGAN x4plus anime (strongest cleanup)",
         "the photo models (they add photo texture)"},
        {"Line art, manga / comic scans, sketches",
         "Real-CUGAN Pro",
         "Real-CUGAN SE, waifu2x CUnet",
         "the photo models (can thicken or bend lines)"},
        {"Anime screenshots, video frames, cel animation",
         "Real-ESRGAN animevideo v3 (fast)",
         "Real-CUGAN SE (more detail, slower)",
         "-"},
        {"Real-life photos (clean or lightly compressed)",
         "NMKD Superscale (natural detail)",
         "NMKD Siax (sharper, adjustable detail), LSDIR Compact (fast), Real-ESRGAN General (fast, adjustable denoise), RealESRNet x4plus (most faithful), Real-ESRGAN x4plus (strong denoise)",
         "the anime / illustration models (painted look)"},
        {"Noisy or grainy photos, low-light shots",
         "Real-ESRGAN General (denoise Level 2-3)",
         "NMKD Superscale, Real-ESRGAN x4plus",
         "NMKD Siax (keeps and sharpens the noise)"},
        {"JPEG-damaged, web or social media photos",
         "Nomos8kSC (repairs JPEG / compression)",
         "Real-ESRGAN x4plus, Real-ESRGAN General (denoise Level 2-3), LSDIR Compact (denoise Level 0-1)",
         "waifu2x (cannot restore lost detail)"},
        {"Old or blurry photos",
         "Real-ESRGAN x4plus (strongest restoration)",
         "Nomos8kSC, NMKD Superscale",
         "waifu2x (cannot restore lost detail)"},
        {"Photorealistic 3D renders (PBR, archviz, product, games)",
         "NMKD Siax (crisp textures, adjustable detail)",
         "NMKD Superscale, RealESRNet x4plus (no invented texture), LSDIR Compact (Scale only, fast), Real-ESRGAN General (denoise Level 0-1)",
         "the anime / illustration models, Real-ESRGAN x4plus (can wax skin and fabric)"},
        {"Noisy 3D renders (low samples)",
         "Real-ESRGAN General (denoise Level 2-3)",
         "NMKD Superscale, Real-ESRGAN x4plus",
         "NMKD Siax (sharpens the noise)"},
        {"Stylized / toon / cel-shaded 3D, game art, Pixar-like",
         "Real-CUGAN SE",
         "waifu2x CUnet, Real-ESRGAN x4plus anime",
         "the photo models (they add photo texture)"},
        {"Screenshots with text, UI, charts, diagrams",
         "waifu2x CUnet",
         "waifu2x UpRGB",
         "Real-ESRGAN and the photo models (can deform letters)"},
        {"Pixel art",
         "none - these models smooth the pixels",
         "use a nearest-neighbour integer resize instead",
         "all models"},
        {"Weak GPU / huge batches / speed first",
         "waifu2x UpRGB (2D), Real-ESRGAN General or LSDIR Compact (photo)",
         "Real-ESRGAN animevideo v3, waifu2x UpPhoto",
         "the heavy 4x models (Real-ESRGAN x4plus, RealESRNet x4plus, Nomos8kSC, NMKD), TTA mode"},
    };

    struct ModelDoc { const char* title; const char* engine; const char* scales; const char* denoise; const char* speed; const char* notes; };
    const ModelDoc g_modelDocs[] = {
        {"Real-CUGAN SE", "Real-CUGAN (Bilibili, 2022)", "2x, 3x, 4x",
         "Level 0 = conservative (2x/3x/4x), Level 1 and 2 (2x only), Level 3 (2x/3x/4x); Scale only = no denoise",
         "medium (about like waifu2x CUnet)",
         "Best general model for 2D art: sharper and cleaner than waifu2x CUnet while keeping the drawing style. "
         "The 'Real-CUGAN sync gap' option (App Setting) keeps the colour statistics of big images consistent between tiles."},
        {"Real-CUGAN Pro", "Real-CUGAN (Bilibili, 2022)", "2x, 3x",
         "Level 0 = conservative, Level 3 = denoise; Scale only = no denoise (no Level 1/2)",
         "medium",
         "Stronger and sharper detail reconstruction than SE, good for line art; can over-sharpen soft paintings. No 4x network (4x = 2x + 2x)."},
        {"Real-CUGAN NoSE", "Real-CUGAN (Bilibili, 2022)", "2x",
         "none (scale only)",
         "fastest Real-CUGAN",
         "No-denoise variant for clean sources; sync gap is not used."},
        {"NMKD Superscale", "ESRGAN (Nmkd, 2020, WTFPL)", "4x only",
         "no levels - built-in cleanup of noise and compression",
         "slow (heavy network, like Real-ESRGAN x4plus)",
         "Made for realistic images and photos with some noise and compression: natural-looking detail and skin, "
         "less over-sharpening than NMKD Siax. A good first choice for photos and photorealistic renders."},
        {"NMKD Siax", "ESRGAN (Nmkd, WTFPL)", "4x only",
         "no denoise levels - light built-in cleanup. Detail level: Faithful / Balanced / Detailed / Maximum = 40 / 60 / 80 / 100 % Siax",
         "slow (heavy network, like Real-ESRGAN x4plus)",
         "Universal model for clean and lightly compressed images (JPEG quality 75 or better): crisp, detailed textures. "
         "It keeps the noise of noisy images - use Real-ESRGAN General or NMKD Superscale for those. "
         "The Detail level blends it with ESRGAN PSNR, the soft model it was trained from: lower levels are softer and invent less texture, Maximum is plain Siax."},
        {"Nomos8kSC", "ESRGAN (Helaman, 2023, CC-BY-4.0)", "4x only",
         "no levels - built-in JPEG / compression repair",
         "slow (heavy network, like Real-ESRGAN x4plus)",
         "Trained on real photos (Nomos8k) with JPEG and compression damage: the best choice for web, chat and social media pictures "
         "and other compressed photos. Can smooth very clean images a little."},
        {"Real-ESRGAN General", "Real-ESRGAN (Tencent ARC, 2022)", "4x only",
         "Level 0-3 = denoise strength 25 / 50 / 75 / 100 %; Scale only = weakest (0 %)",
         "fast (small network, like animevideo v3)",
         "General model for real-world photos (official realesr-general-x4v3). Made from the official general and weak-denoise networks, "
         "blended at five strengths, so the denoise can be turned down - unlike Real-ESRGAN x4plus. "
         "A little less detail than the heavy 4x models, but much faster and lighter on GPU memory."},
        {"LSDIR Compact", "Real-ESRGAN Compact (Helaman, 2023, CC BY 4.0)", "4x only",
         "Scale only = no cleanup (most detail); Level 0 = JPEG cleanup, Level 1-2 = more, Level 3 = strongest (blur, noise and JPEG)",
         "fast (small network, like animevideo v3)",
         "Photo model trained on the large LSDIR photo set, made from the author's three version-3 networks: "
         "Scale only keeps the most detail of clean, good-quality images such as clean 3D renders, and the levels add more and more cleanup "
         "for compressed or noisy images (Level 1 is the author's own blend). Less detail than the heavy 4x models, but much faster."},
        {"RealESRNet x4plus", "Real-ESRGAN (Tencent ARC, 2021)", "4x only",
         "no levels - mild built-in cleanup",
         "slowest (heavy network, same as Real-ESRGAN x4plus)",
         "Real-ESRGAN x4plus before its GAN training: softer, but it does not invent texture. "
         "Good for clean renders and photos when faithfulness matters more than crispness."},
        {"Real-ESRGAN x4plus", "Real-ESRGAN (Tencent ARC, 2021)", "2x (x2plus), 4x",
         "no levels - it always removes noise, JPEG artifacts and blur by itself",
         "slowest (heavy network)",
         "Good for photos and realistic 3D, with strong built-in restoration that helps old, blurry or damaged photos - but its denoise "
         "cannot be turned down: it can wax skin and fabric or invent small details. If it wipes out details, use NMKD Superscale, "
         "Real-ESRGAN General or RealESRNet x4plus. 2x uses the official native 2x network (x2plus), about 4 times faster than 4x; "
         "3x runs the 4x network and shrinks the result. Very slow on CPU (minutes per image)."},
        {"Real-ESRGAN x4plus anime", "Real-ESRGAN (Tencent ARC, 2021)", "4x only",
         "no levels - built-in restoration",
         "fast (small 6-block network)",
         "Strong cleanup for illustrations: very clean lines and flat colours, but loses fine texture and can thicken lines."},
        {"Real-ESRGAN animevideo v3", "Real-ESRGAN (Tencent ARC, 2022)", "2x, 3x, 4x",
         "no levels - built-in restoration",
         "very fast (tiny network)",
         "Made for anime video frames and screenshots; a bit less detail than x4plus anime."},
        {"waifu2x CUnet", "waifu2x (nagadomi, 2018)", "2x (and 1x denoise)",
         "Level 0-3 (higher = stronger)",
         "slow",
         "Most faithful to the original 2D artwork, softer than Real-CUGAN. Has a true denoise-only network."},
        {"waifu2x UpResNet10", "waifu2x (nagadomi, 2018)", "2x",
         "Level 0-3 (denoise only = 2x then shrink, like waifu2x-caffe)",
         "medium",
         "Between UpRGB and CUnet in quality and speed. Converted from waifu2x-caffe for this program."},
        {"waifu2x UpRGB", "waifu2x (nagadomi, 2017)", "2x",
         "Level 0-3 (denoise only = 2x then shrink)",
         "fastest",
         "Quick 2D upscaling; a little softer than CUnet."},
        {"waifu2x UpPhoto", "waifu2x (nagadomi, 2017)", "2x",
         "Level 0-3 (denoise only = 2x then shrink)",
         "fast",
         "Light, fast and faithful, with an adjustable denoise level. The newer photo models (NMKD, Nomos8kSC, Real-ESRGAN General) "
         "restore much more detail; UpPhoto is the light choice for weak GPUs or very large batches."},
    };

    const char* const g_notes[] = {
        "GPU: every model runs on any GPU with Vulkan - NVIDIA GTX 600 series and newer (e.g. GTX 1050), AMD GCN and newer (e.g. RX 470/570), "
        "Intel HD 500 and newer, Iris Xe and Arc. No CUDA is needed. If 'Check GPU' finds nothing, update the graphics driver.",
        "CPU: used only when no Vulkan GPU is found (or when CPU is selected). Every model works on any x86-64 Intel / AMD CPU, "
        "but 10-100x slower than a GPU; the heavy 4x models (Real-ESRGAN x4plus, RealESRNet x4plus, Nomos8kSC, NMKD) can take minutes per image.",
        "Heavy 4x models (Real-ESRGAN x4plus, RealESRNet x4plus, Nomos8kSC, NMKD Siax / Superscale): each GPU converts one image at a time, "
        "whatever Thread(s) says, so they do not run out of GPU memory. They need about half the split size of the other models.",
        "GPU memory: Split size 0 = automatic from the free memory, so 2 GB cards work. If a conversion fails or gives black / broken "
        "output, set a smaller split size (e.g. 100 or 64).",
        "Any scale works: the native passes (x2 / x3 / x4) are run until the target size is reached, then the image is resized exactly "
        "(rate, width, height or width x height).",
        "Denoise level means different things: waifu2x 0-3 = stronger and stronger; Real-CUGAN 0 = conservative, 1-3 = denoise; "
        "Real-ESRGAN General 0-3 = denoise strength 25-100 %; LSDIR Compact 0-3 = more and more cleanup; "
        "the other Real-ESRGAN, NMKD and Nomos8kSC models have none (restoration is built in). "
        "'Scale only' never denoises (Real-ESRGAN General: weakest; LSDIR Compact: no cleanup).",
        "TTA mode averages 8 rotated / mirrored runs: slightly better edges, 8x slower.",
        "Transparency (alpha) is enlarged with bicubic interpolation by all engines; colour is processed by the network.",
        "AI upscalers can invent detail (textures, letters). Check the result before using it for text, documents or anything where accuracy matters.",
    };
}

void DialogEvent::DrawModelHelp(float s, bool& close)
{
    // drawn into its own os window (see AppMain), filling it
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##modelhelpwin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float bh = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##helpbody", ImVec2(0, -bh), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::SeparatorText(T("Which model for which picture").c_str());
    if (ImGui::BeginTable("##usecases", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn(T("Picture").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(T("Best").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(T("Also good").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(T("Avoid").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();
        for (const auto& u : g_useCases)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", T(u.content).c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", T(u.best).c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", T(u.also).c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", T(u.avoid).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText(T("The models").c_str());
    for (const auto& d : g_modelDocs)
    {
        bool installed = false;
        for (const auto& m : models)
            for (const auto& b : g_builtinModels)
                if (m.folder == b.folder && std::string(b.fallbackTitle).rfind(d.title, 0) == 0) installed = true;
        std::string head = std::string(d.title) + (installed ? std::string() : "   " + T("(not installed)"));
        if (ImGui::CollapsingHeader(head.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10 * s);
            if (ImGui::BeginTable(d.title, 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110 * s);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                const char* keys[5] = {"Engine", "Native scales", "Denoise", "Speed", "Notes"};
                const char* vals[5] = {d.engine, d.scales, d.denoise, d.speed, d.notes};
                for (int i = 0; i < 5; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", T(keys[i]).c_str());
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", T(vals[i]).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::Unindent(10 * s);
            ImGui::Spacing();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(T("Good to know").c_str());
    for (const char* n : g_notes)
    {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", T(n).c_str());
    }
    ImGui::EndChild();

    const float bw = 90 * s;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw);
    if (ImGui::Button((L2("OK", "OK") + "##helpok").c_str(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    ImGui::End();
}

// ============================================================================ scale rate box: digits + at most 2 decimals

// "", "2", "2.", "2.5", "2.25", ".5" are accepted while typing; anything else is refused
bool DialogEvent::IsValidRateText(const char* s)
{
    int dots = 0, decimals = 0;
    for (const char* p = s; *p; p++)
    {
        if (*p == '.')
        {
            if (++dots > 1) return false;
        }
        else if (*p >= '0' && *p <= '9')
        {
            if (dots == 1 && ++decimals > 2) return false;
        }
        else
            return false;
    }
    return true;
}

int DialogEvent::ScaleRatioCallback(ImGuiInputTextCallbackData* data)
{
    DialogEvent* self = (DialogEvent*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        // typed or pasted character: only 0-9 and the decimal point get in
        const ImWchar c = data->EventChar;
        if ((c >= '0' && c <= '9') || c == '.') return 0;
        self->scaleRateWarn = true;
        return 1;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        if (IsValidRateText(data->Buf))
            self->scaleRatioLastValid = data->Buf;
        else
        {
            // 3rd decimal or 2nd point: undo the edit
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, self->scaleRatioLastValid.c_str());
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            self->scaleRateWarn = true;
        }
    }
    return 0;
}

bool DialogEvent::SetScaleRatioText(const std::string& s)
{
    std::string t = Trim(s);
    bool exact = IsValidRateText(t.c_str());
    if (!exact)
    {
        double d;
        if (ParseDouble(t, d) && d > 0.0)
            t = Format("%.2f", d);
        else
            t = "1.00";
    }
    CopyStr(editScaleRatio, sizeof(editScaleRatio), t);
    scaleRatioLastValid = t;
    return exact;
}

void DialogEvent::ShowScaleRateWarning()
{
    const std::string text = L2("MessageScaleRateFormatError",
        "The scale rate supports numbers only (0-9) with at most 2 decimal places, e.g. 1.5 or 2.25.\n"
        "Other characters and more decimals are not supported.\n\n"
        "For an exact size, use \"<Width>\", \"<Height>\" or \"<Width>x<Height>\" instead.");
    // one warning at a time, not one per key press
    for (const auto& m : msgBoxes)
        if (m.text == text) return;
    MessageBox(text, L("MessageTitleError"), IconWarning);
}

// ============================================================================ <Width> / <Height> boxes: digits only

int DialogEvent::DigitsOnlyCallback(ImGuiInputTextCallbackData* data)
{
    DialogEvent* self = (DialogEvent*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        const ImWchar c = data->EventChar;
        if (c >= '0' && c <= '9') return 0;
        self->scaleNumWarn = true;
        return 1;
    }
    return 0;
}

// output quality: digits only; the whole text must be empty or a whole number of the format's range without a leading zero
int DialogEvent::QualityCallback(ImGuiInputTextCallbackData* data)
{
    DialogEvent* self = (DialogEvent*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        const ImWchar c = data->EventChar;
        if (c >= '0' && c <= '9') return 0;
        self->qualityWarn = true;
        return 1;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        const int fi = self->outExtIndex;
        const int qmin = fi >= 0 && fi < g_output_format_count ? g_output_formats[fi].quality_min : 0;
        const int qmax = fi >= 0 && fi < g_output_format_count ? g_output_formats[fi].quality_max : 100;
        const std::string t(data->Buf, data->BufTextLen);
        bool ok = t.empty();
        if (!ok && t.size() <= 3 && !(t.size() > 1 && t[0] == '0'))
        {
            const int v = atoi(t.c_str());
            ok = v >= qmin && v <= qmax;
        }
        if (ok)
            self->qualityLastValid = t;
        else
        {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, self->qualityLastValid.c_str());
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            self->qualityWarn = true;
        }
    }
    return 0;
}

void DialogEvent::ShowQualityWarning()
{
    const int fi = outExtIndex;
    const int qmin = fi >= 0 && fi < g_output_format_count ? g_output_formats[fi].quality_min : 0;
    const int qmax = fi >= 0 && fi < g_output_format_count ? g_output_formats[fi].quality_max : 100;
    const std::string text = Format(T("Output quality accepts a whole number from %d to %d only, without leading zeros (e.g. 0, 5, 95).").c_str(), qmin, qmax);
    for (const auto& m : msgBoxes)
        if (m.text == text) return;
    MessageBox(text, L("MessageTitleError"), IconWarning);
}

void DialogEvent::ShowScaleNumWarning()
{
    const std::string text = L2("MessageScaleNumFormatError",
        "<Width> and <Height> accept whole numbers only (0-9), e.g. 1920 or 1080.\n"
        "Other characters are not supported.");
    for (const auto& m : msgBoxes)
        if (m.text == text) return;
    MessageBox(text, L("MessageTitleError"), IconWarning);
}

// ============================================================================ <Width>x<Height> box: "<digits>x<digits>" only

// accepted while typing: "", "1920", "1920x", "1920x1080" (and "x1080")
bool DialogEvent::IsValidWHText(const char* s)
{
    int xs = 0;
    for (const char* p = s; *p; p++)
    {
        if (*p == 'x')
        {
            if (++xs > 1) return false;
        }
        else if (*p < '0' || *p > '9')
            return false;
    }
    return true;
}

int DialogEvent::ScaleWHCallback(ImGuiInputTextCallbackData* data)
{
    DialogEvent* self = (DialogEvent*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        ImWchar c = data->EventChar;
        if (c == 'X') { data->EventChar = 'x'; return 0; } // accept "1920X1080" as typed, stored lower case
        if ((c >= '0' && c <= '9') || c == 'x') return 0;
        self->scaleWHWarn = true;
        return 1;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        if (IsValidWHText(data->Buf))
            self->scaleWHLastValid = data->Buf;
        else
        {
            // a second "x": undo the edit
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, self->scaleWHLastValid.c_str());
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            self->scaleWHWarn = true;
        }
    }
    return 0;
}

bool DialogEvent::SetScaleWHText(const std::string& s)
{
    std::string t = Trim(s);
    for (auto& c : t) if (c == 'X') c = 'x';
    const bool ok = IsValidWHText(t.c_str());
    if (!ok || t == "0") t.clear();
    CopyStr(editScaleWH, sizeof(editScaleWH), t);
    scaleWHLastValid = t;
    // <Width>x<Height> uses the Width and Height boxes
    const size_t xp = t.find('x');
    if (xp != std::string::npos)
    {
        CopyStr(editScaleWidth, sizeof(editScaleWidth), t.substr(0, xp));
        CopyStr(editScaleHeight, sizeof(editScaleHeight), t.substr(xp + 1));
    }
    return ok;
}

void DialogEvent::ShowScaleWHWarning()
{
    const std::string text = L2("MessageScaleWHFormatError",
        "<Width>x<Height> accepts only the form <width>x<height> with numbers, e.g. 1920x1080 or 2560x1440.\n"
        "Other characters are not supported.");
    for (const auto& m : msgBoxes)
        if (m.text == text) return;
    MessageBox(text, L("MessageTitleError"), IconWarning);
}

// ============================================================================ split size / thread(s) guide ("?" buttons)

void DialogEvent::DrawSpeedHelp(float s, bool& close)
{
    // drawn into its own os window (see AppMain), filling it
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##speedhelpwin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float bh = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##speedbody", ImVec2(0, -bh), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    auto bullet = [this](const char* t) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", T(t).c_str());
    };

    ImGui::SeparatorText(T("Split size").c_str());
    bullet("The image is processed in square pieces (tiles) of this size, so that even huge images fit into the GPU memory. "
           "The pieces overlap and are stitched back together, so the size does not change the result.");
    bullet("0 = automatic (recommended): chosen for each model from the free GPU memory "
           "(about 400 with 8 GB or more, 200 with 4 GB, 100 with 2 GB).");
    bullet("Bigger = a little faster (fewer pieces) but needs more GPU memory. Too big and the conversion fails, "
           "gives a black / broken image or the graphics driver resets.");
    bullet("Smaller = less GPU memory, slower. The minimum is 32.");
    bullet("If a conversion fails with 0: try 200, then 100, then 64.");
    bullet("The list shows sizes that divide the picture evenly (like waifu2x-caffe) and the presets from crop_size_list.txt; "
           "any number of 32 or more can be typed.");
    bullet("The heavy 4x models (Real-ESRGAN x4plus, RealESRNet x4plus, Nomos8kSC, NMKD Siax / Superscale) need about half the split size of the others for the same memory.");
    bullet("Real-CUGAN: with 'sync gap' 0 (App Setting) neighbouring pieces can differ slightly in tone on big images; the default 3 avoids it.");
    bullet("CPU fallback: 400 by default, limited by the system memory instead of the GPU memory.");

    ImGui::Spacing();
    ImGui::SeparatorText(T("Thread(s)").c_str());
    bullet("GPU: how many images are converted on each GPU at the same time. Loading and saving run in their own threads "
           "(App Setting: load / save threads).");
    bullet("It speeds up folders and many files: the GPU keeps working while other images are being read or written. "
           "A single image is not converted faster by more threads.");
    bullet("Every thread needs its own GPU memory. With Split size 0 the automatic size gets smaller when more threads run.");
    bullet("Recommended: 1-2 for 2-4 GB cards (e.g. GTX 1050), 2 (default) for most GPUs, 3-4 for strong GPUs with many small images. "
           "Too many = out of memory, or no gain.");
    bullet("CPU fallback: the number of CPU cores used for the network. Set it to your core count for the best CPU speed.");
    bullet("Several GPUs: separate split sizes and thread counts per GPU can be set in App Setting (e.g. GPU ids 0,1 / threads 2,2).");

    ImGui::Spacing();
    ImGui::SeparatorText(T("Neither changes the quality").c_str());
    ImGui::TextWrapped("%s", T("Split size and Thread(s) only change speed and memory use. The picture comes out the same; "
                               "quality is decided by the model, the denoise level and TTA mode.").c_str());
    ImGui::EndChild();

    const float bw = 90 * s;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw);
    if (ImGui::Button((L2("OK", "OK") + "##speedok").c_str(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    ImGui::End();
}

// ============================================================================ "Reset to Default"

void DialogEvent::DrawResetConfirm(float s)
{
    // drawn inside its own os window (DrawModal) while resetConfirmRequest is set
    ImGui::Dummy(ImVec2(320 * s, 0)); // minimum width
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 580 * s);
    ImGui::TextUnformatted(L2("MessageResetConfirm", "Clear the input / output paths and return every setting to the first-start state?").c_str());
    ImGui::PopTextWrapPos();
    const float right = std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x, ImGui::GetCursorStartPos().x + 320 * s);
    ImGui::Spacing();
    const float bw = 90 * s;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - bw * 2 - ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::Button((L2("MessageResetYes", "Reset") + "##resetyes").c_str(), ImVec2(bw, 0)))
    {
        ResetToDefault();
        resetConfirmRequest = false;
    }
    ImGui::SameLine();
    if (ImGui::Button((L2("Cancel", "Cancel") + "##resetno").c_str(), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        resetConfirmRequest = false;
}

// every value back to what a first start (no setting.ini) shows
void DialogEvent::ResetToDefault()
{
    if (processThread.joinable()) return;

    // paths
    input_str.clear();
    input_str_multi.clear();
    output_str.clear();
    output_dir.clear();
    autoSetAddName.clear();
    editInput[0] = 0;
    editOutput[0] = 0;

    // input / output formats
    inputFileExt = DefaultInputExtList;
    CopyStr(editExtList, sizeof(editExtList), ExtListDisplay());
    outExtIndex = 0; // .png
    outputExt = ".png";
    SetQuality(true);

    // conversion
    radioMode = 0;  // Denoise & Scale
    radioNoise = 0;
    radioScale = 0; // <Scale Rate>
    SetScaleRatioText("1.00");
    editScaleWidth[0] = 0;
    editScaleHeight[0] = 0;
    SetScaleWHText("");
    checkTTA = false;
    use_tta = false;

    // model / device / speed
    comboModel = 0;
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].type == FallbackModelType) comboModel = (int)i;
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].type == DefaultModelType) comboModel = (int)i;
    modelType = comboModel;
    process = "gpu";
    gpu_no = -1;
    comboCrop[0] = 0;
    SetCropSizeList("");
    CopyStr(comboCrop, sizeof(comboCrop), "0");
    crop_size = 0;
    CopyStr(comboBatch, sizeof(comboBatch), "2");
    batch_size = 2;

    // app setting
    tAutoMode = "none";
    isArgStartAuto = true;
    isArgStartSuccessFinish = true;
    isOutputNoOverwrite = false;
    tInputDirFix.clear();
    tOutputDirFix.clear();
    gpuIdsMulti.clear();
    tileMulti.clear();
    procMulti.clear();
    loadThreads = 1;
    saveThreads = 2;
    verboseLog = false;
    syncgap = 3;

    // the ui language is kept (not part of the reset); the folders the explorer remembered are forgotten
    lastBrowseIn.clear();
    lastBrowseOut.clear();
    fileBrowser.prefs = FileBrowser::Prefs(); // explorer: sort by name, A to Z

    // log / progress
    logMessage.clear();
    progressNow = progressMax = 0;

    FixModeForModel();
    isNotSaveParam = false;
    SaveIni(true); // setting.ini now holds the defaults
}
