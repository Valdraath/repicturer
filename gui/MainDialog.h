// main window of RePicturer
//
// rebuilt from waifu2x-caffe-gui MainDialog (DialogEvent): same controls, same behaviour, same setting.ini
// keys and command line, but drawn with dear imgui (dark theme) and driving the enhanced
// repicturer-cli command line (based on waifu2x-ncnn-vulkan) instead of caffe.

#pragma once

// the name the os shows (window title, taskbar, task manager, x11 WM_CLASS)
#define APP_NAME "RePicturer"
#define APP_NAME_W L"RePicturer"
#define APP_VERSION_NUM "2.2"
#define APP_VERSION "v" APP_VERSION_NUM
#define APP_TITLE APP_NAME " " APP_VERSION   // title bar text

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>
#include <memory>

#include "imgui.h"
#include "LangStringList.h"
#include "FileBrowser.h"
#include "Subprocess.h"
#include "Util.h"

enum ModelEngine
{
    EngineWaifu2x = 0,
    EngineRealCUGAN = 1,
    EngineRealESRGAN = 2,
};

struct ModelEntry
{
    std::string dir;       // absolute folder
    std::string folder;    // folder name, e.g. models-cunet
    std::string type;      // -y model_type name, e.g. cunet / realcugan-se
    std::string langKey;   // title key ("" for custom folders)
    std::string name;      // short name used in output file names (CUnet, RealCUGAN-SE ...)
    int engine = EngineWaifu2x;
    // waifu2x networks
    bool noise[4] = {false, false, false, false};
    bool noiseScale[4] = {false, false, false, false};
    bool scale = false;
    // realcugan [scale][variant + 2], variant -2 no-denoise, -1 conservative, 1..3 denoise
    bool cugan[5][6] = {};
    // realesrgan [scale]; esrNoise [scale][level]: one network per denoise level (x{S}-denoise{N})
    bool esr[5] = {};
    bool esrNoise[5][4] = {};
    // detail levels [scale][0..2] (x{S}-detail{N}; level 3 = x{S}): not denoise, used in every mode
    bool esrDetail[5][3] = {};
    // heavy network (Real-ESRGAN x4plus, NMKD, Nomos8kSC ...: network file over 20 MB): one image at a time per GPU
    bool heavy = false;
    // Real-ESRGAN-engine model without denoise levels: its cleanup is built in and always on
    bool BuiltInCleanup() const;
    bool HasDetailLevels() const { for (int s = 2; s <= 4; s++) if (esrDetail[s][0]) return true; return false; }

    bool NoiseLevelAvailable(int level) const;
    bool AnyDenoise() const;
    bool CanMagnify() const;
};

struct GpuDevice
{
    int index;
    std::string name;
    std::string type;
    int heapMB;      // free now
    int totalMB = 0; // the card's vram
};

class DialogEvent
{
public:
    DialogEvent();
    ~DialogEvent();

    void Create(int argc, char** argv); // WM_INITDIALOG
    void Draw(float scale);             // one frame
    void OnDrop(const std::vector<std::string>& paths, float mx, float my);
    bool WantClose() const { return closeRequested; }
    void RequestClose();                // WM_CLOSE
    std::string WindowTitle() const { return APP_TITLE; }
    bool IsBusy() const { return processThread.joinable(); }
    void SetLogHeight(float dlu) { logHeightDlu = dlu; } // fill mode: 55 (normal) .. 110 (2x)
    // which cjk font the ui language needs first: "zh-TW", "zh-CN", "ko" or "ja" (also used for the other languages)
    std::string FontScript() const
    {
        const std::string& f = lang.GetLang().FileName;
        if (f == "Chinese_Traditional.json") return "zh-TW";
        if (f == "Chinese_Simplified.json") return "zh-CN";
        if (f == "korean.json") return "ko";
        return "ja";
    }

    // "?" guides live in their own os windows (AppMain): 1 = model guide, 2 = split size / thread(s)
    int TakeHelpRequest()
    {
        int k = modelHelpRequest ? 1 : speedHelpRequest ? 2 : 0;
        if (k == 1) modelHelpRequest = false;
        if (k == 2) speedHelpRequest = false;
        return k;
    }
    std::string HelpTitle(int kind) const
    {
        return (kind == 1 ? L2("ModelHelpTitle", "Model guide") : L2("SpeedHelpTitle", "Split size and Thread(s)")) + " - " + APP_TITLE;
    }
    void DrawHelpWindow(int kind, float s, bool& close)
    {
        if (kind == 1) DrawModelHelp(s, close);
        else DrawSpeedHelp(s, close);
    }

    // dialogs (message, folder browser, app setting, reset confirm) each get their own os window (AppMain),
    // one at a time, and the main window is locked while one is open
    enum { ModalNone = 0, ModalMessage = 10, ModalBrowser, ModalAppSetting, ModalReset };
    int ModalKind() const;
    std::string ModalTitle(int kind) const;
    void DrawModal(int kind, float s, ImVec2& fitSize); // fitSize: size the window should take, 0 = user sized
    void CloseModal(int kind);                          // the window's close button

private:
    enum eScaleType
    {
        eScaleTypeRatio,
        eScaleTypeWidth,
        eScaleTypeHeight,
        eScaleTypeWidthHeight,
        eScaleTypeEnd,
    };

    enum eMode
    {
        eModeNoise,
        eModeScale,
        eModeNoiseScale,
        eModeAutoScale,
    };

    // ------------------------------------------------ caffe DialogEvent members
    std::string exeDir;
    std::vector<int> CropSizeList;

    std::string input_str;
    std::vector<std::string> input_str_multi;
    std::string output_str;
    std::string modeStr;
    eMode mode;
    int noise_level;
    double scale_ratio;
    int scale_width;
    int scale_height;
    std::string model_dir;
    std::string process; // "gpu" / "cpu"
    std::string outputExt;
    std::string inputFileExt;
    int output_quality; // -1 = none
    int crop_size;
    int batch_size;     // ncnn: processing threads per gpu
    int gpu_no;         // -1 = auto
    std::vector<std::string> extList;

    std::string autoSetAddName;
    bool isLastError;
    std::string logMessage;
    std::string output_dir;

    eScaleType scaleType;
    int modelType;      // index into models
    std::string LangName;

    bool isOutputNoOverwrite;
    bool useCustomDirs;      // Preferences: the custom input / output folders are used (off = the main window's paths)
    std::string tInputDirFix;
    std::string tOutputDirFix;
    std::string lastBrowseIn, lastBrowseOut; // last folders of the input / output explorer
    bool isSetInitCrop;

    // ncnn specific settings
    // reading / saving threads of the converter: fixed (1 / 2 suits every GPU and the CPU), not a setting
    static const int loadThreads = 1;
    static const int saveThreads = 2;
    int syncgap;           // Real-CUGAN sync gap mode 0-3
    int vramPercent;       // gpu memory limit: % of the memory that is free when a conversion starts (10-80)
    int ramPercent;        // RAM limit: % of the RAM that is free when a conversion starts (10-80)

    // ------------------------------------------------ ui state (the win32 controls)
    char editInput[4096] = {0};
    char editOutput[4096] = {0};
    char editExtList[1024] = {0};
    char editQuality[64] = {0};
    char editScaleRatio[64] = {0};
    std::string scaleRatioLastValid = "1.00"; // last accepted text of the rate box
    bool scaleRateWarn = false;               // a blocked keystroke / paste happened this frame
    std::string scaleWHLastValid;             // last accepted text of the width x height box
    bool scaleWHWarn = false;
    bool scaleNumWarn = false;                // non digit typed into <Width> / <Height>
    std::string qualityLastValid;             // last accepted text of the output quality box
    bool qualityWarn = false;                 // something outside the format's range was typed
    bool speedHelpRequest = false;            // "?" next to split size / thread(s)
    bool resetConfirmRequest = false;         // "Reset to Default" asks first
    char editScaleWidth[64] = {0};
    char editScaleHeight[64] = {0};
    char editScaleWH[64] = {0};   // "<width>x<height>" (command line / setting.ini); the GUI uses editScaleWidth / Height
    char comboCrop[64] = {0};
    char comboBatch[64] = {0};
    int radioMode = 0;   // 0 noise_scale, 1 scale, 2 noise, 3 auto_scale (dialog order)
    int radioNoise = 0;
    int radioScale = 0;
    int outExtIndex = 0;
    int comboModel = 0;
    int comboLang = 0;
    std::vector<std::string> cropItems;
    bool qualityEnabled = false;
    std::string qualityTitle;

    std::vector<ModelEntry> models;
    std::vector<GpuDevice> gpus;
    std::atomic<int> gpuState; // 0 unknown, 1 detecting, 2 done
    std::string gpuError;
    std::vector<GpuDevice> gpusPending;
    std::string gpuErrorPending;
    bool gpuApplied = false;
    std::thread gpuThread;
    bool gpuShowResult = false;

    LangStringList lang;
    FileBrowser fileBrowser;
    int fileBrowserTarget = 0; // 1 input, 2 output, 3 input fix, 4 output fix


    // message boxes (MessageBox replacement)
    struct MsgBox { std::string title, text; int icon; };
    std::deque<MsgBox> msgBoxes;

    // app setting dialog working copy
    bool appSettingOpen = false;
    struct AppSettingState
    {
        bool noOverwrite = false;
        bool customDirs = false;
        char inputDirFix[2048] = {0};
        char outputDirFix[2048] = {0};
        int syncgap = 3;
        int vramPercent = 60;
        int ramPercent = 60;
        char vramText[3] = "60"; // the fields: 2 digits, 20-80
        char ramText[3] = "60";
        std::string vramLast = "60", ramLast = "60"; // last accepted text (an invalid key is undone)
    } app;

    // model guide popup ("?" next to the model list)
    bool modelHelpRequest = false;

    // the VRAM / RAM share fields of Preferences: invalid input is undone and warned about, like Output quality
    bool percentWarn = false;
    static int PercentCallback(ImGuiInputTextCallbackData* data);
    void ShowPercentWarning();

    // drop targets (screen rects of the edits)
    ImVec2 inputRectMin, inputRectMax, outputRectMin, outputRectMax;

    // ------------------------------------------------ processing thread
    struct Job
    {
        std::vector<std::pair<std::string, std::string>> files;
        std::vector<std::string> args;
        std::string exe;
        std::string listPath;
        std::string inputStr;
        std::vector<std::string> inputMulti;
        std::string outputStr;
        std::string outputExt;
        std::vector<std::string> extList;
    };
    struct Event
    {
        int type;
        std::string a, b, c;
        int n = 0, m = 0;
    };
    enum
    {
        EvTotal, EvReady, EvDevice, EvProgress, EvFail, EvSkip, EvLog, EvFailCreateDir, EvFatal, EvEnd, EvPlan, EvAnim, EvFrame, EvMemory
    };
    std::thread processThread;
    std::atomic<bool> cancelFlag;
    std::mutex eventLock;
    std::deque<Event> events;
    std::unique_ptr<Subprocess> proc;
    std::mutex procLock;
    int progressMax = 0;
    int progressNow = 0;
    std::chrono::steady_clock::time_point startTime, readyTime, endTime, firstDoneTime;
    bool readyReceived = false;
    int planShown = 0;
    int failCount = 0, skipCount = 0;
    int frameNow = 0, frameTotal = 0;     // animated file being converted
    std::vector<std::string> usedDevices;
    std::vector<std::string> stderrTail;
    bool closeRequested = false;
    bool closeAfterProcess = false;
    float logHeightDlu = 55.0f;

    // ------------------------------------------------ caffe methods
    std::string AddName() const;
    bool SyncMember(bool NotSyncCropSize, bool silent = false);
    void SetCropSizeList(const std::string& input_path);
    void ProcessWaifu2x(Job job);
    void ReplaceAddString();
    void AddLogMessage(const std::string& msg);
    void Waifu2xTime();
    void SaveIni(bool isSyncMember = true);
    void SetWindowTextLang();
    void SetQuality(bool SetDefaultQuality = true);

    void Exec();
    void WaitThreadExit();
    void Cancel();
    void OnModeChange();
    void ScaleRadio();
    void CheckGPU();
    void OnModelChange();
    void OnSetInputFilePath(const std::string& path);
    void OnSetInputFilePath();
    void OnSetOutputFilePath(const std::string& path);
    void InputRef();
    void OutputRef();
    void LangChange();
    void OutExtChange();
    void ClearOutputDir();
    void ResetToDefault();
    void DrawResetConfirm(float s);
    void AppSetting();

    // ------------------------------------------------ helpers
    void MessageBox(const std::string& text, const std::string& title, int icon);
    const std::string& L(const char* key) const { return lang.GetString(key); }
    std::string L2(const char* key, const char* fallback) const { return lang.Get(key, fallback); }
    std::string T(const char* en) const { return lang.Get(en, en); } // text keyed by its english wording
    void ScanModels();
    void StartGpuDetect(bool showResult);
    void PostEvent(const Event& e);
    void HandleEvents();
    void SetModeRadio(eMode m);
    void SetNoiseRadio(int level);
    void SetScaleRadio(eScaleType t);
    std::string CliPath() const;
    std::string SettingPath() const;
    int DeviceComboIndex() const;
    void SetDeviceFromCombo(int idx);
    std::vector<std::string> DeviceComboItems() const;

    // drawing
    void DrawMain(float s);
    void DrawAppSetting(float s);
    void DrawMessageBoxes(float s);
    void OnBrowserAccepted();
    void ApplyPicked(int target, const std::vector<std::string>& paths);
    void StartPicker(int target, bool folder, const std::string& title, const std::string& initialDir);
    std::string CheckInputPaths(const std::vector<std::string>& paths) const;
    std::string CheckOutputPath(const std::string& out, bool singleFile, const std::string& inputFile) const;
    std::string CheckOutputFolder(const std::string& dir) const;
    std::string FirstExistingFolder(const std::vector<std::string>& candidates) const;
    void PruneRememberedFolders();
    bool CheckSettingFile(bool& missing) const;
    std::string TranslateDetail(const std::string& d) const;
    std::string TranslatePlan(std::string d) const;
    void DrawModelHelp(float s, bool& close);
    static int ScaleRatioCallback(ImGuiInputTextCallbackData* data);
    static bool IsValidRateText(const char* s);
    bool SetScaleRatioText(const std::string& s); // returns false when the value had to be rounded to 2 decimals
    void ShowScaleRateWarning();
    static int ScaleWHCallback(ImGuiInputTextCallbackData* data);
    static bool IsValidWHText(const char* s);
    bool SetScaleWHText(const std::string& s);   // false when the value was not in <width>x<height> form
    void ShowScaleWHWarning();
    static int DigitsOnlyCallback(ImGuiInputTextCallbackData* data);
    static int QualityCallback(ImGuiInputTextCallbackData* data);
    void ShowQualityWarning();
    void ShowScaleNumWarning();
    void DrawSpeedHelp(float s, bool& close);
    void FixModeForModel();
    const ModelEntry* CurrentModel() const { return comboModel >= 0 && comboModel < (int)models.size() ? &models[comboModel] : nullptr; }
};
