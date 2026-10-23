// RePicturer entry point: glfw + opengl3 + dear imgui, dark theme

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "MainDialog.h"
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include "Util.h"
#include "stb_image.h"
#include "app_icon_png.h"
#include "roboto_medium_ttf.h"

#ifdef _WIN32
#define APIENTRYP_W2X __stdcall *
#else
#define APIENTRYP_W2X *
#endif

static DialogEvent* g_dialog = nullptr;

// caffe dialog: 665 x 276 dialog units
static const float BASE_W = 665 * 1.5f;
static const float BASE_H = 276 * 1.625f;
static const float BASE_FONT = 14.0f;

static void ApplyDarkStyle(float scale)
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);

    st.WindowRounding = 0;
    st.ChildRounding = 0;
    st.FrameRounding = 0;
    st.PopupRounding = 0;
    st.ScrollbarRounding = 0;
    st.GrabRounding = 0;
    st.TabRounding = 0;
    st.FrameBorderSize = 1.0f;
    st.WindowBorderSize = 1.0f;
    st.PopupBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FramePadding = ImVec2(4, 3);
    st.ItemSpacing = ImVec2(6, 4);
    st.ScrollbarSize = 14;
    st.ScaleAllSizes(scale);
    // ScaleAllSizes truncates border widths (0.999 -> 0, which hid every frame border in windowed mode):
    // keep them at least 1 px, rounded at bigger scales
    {
        const float border = std::max(1.0f, std::round(scale));
        st.FrameBorderSize = border;
        st.WindowBorderSize = border;
        st.PopupBorderSize = border;
        st.ChildBorderSize = border;
    }

    const ImVec4 bg(0x1c / 255.f, 0x1c / 255.f, 0x1c / 255.f, 1.f);
    const ImVec4 white(1.f, 1.f, 1.f, 1.f);
    const ImVec4 frame(0x2a / 255.f, 0x2a / 255.f, 0x2a / 255.f, 1.f);
    const ImVec4 hover(0x3a / 255.f, 0x3a / 255.f, 0x3a / 255.f, 1.f);
    const ImVec4 active(0x50 / 255.f, 0x50 / 255.f, 0x50 / 255.f, 1.f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = white;
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = bg;
    c[ImGuiCol_PopupBg] = ImVec4(0x24 / 255.f, 0x24 / 255.f, 0x24 / 255.f, 1.f);
    c[ImGuiCol_Border] = white;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = active;
    c[ImGuiCol_TitleBg] = frame;
    c[ImGuiCol_TitleBgActive] = ImVec4(0x33 / 255.f, 0x33 / 255.f, 0x33 / 255.f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = frame;
    c[ImGuiCol_MenuBarBg] = frame;
    c[ImGuiCol_ScrollbarBg] = bg;
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.35f, 0.35f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.f);
    c[ImGuiCol_CheckMark] = white;
    c[ImGuiCol_SliderGrab] = white;
    c[ImGuiCol_SliderGrabActive] = white;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = active;
    c[ImGuiCol_Header] = ImVec4(0x44 / 255.f, 0x44 / 255.f, 0x44 / 255.f, 1.f);
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = active;
    c[ImGuiCol_Separator] = white;
    c[ImGuiCol_SeparatorHovered] = white;
    c[ImGuiCol_SeparatorActive] = white;
    c[ImGuiCol_ResizeGrip] = ImVec4(1, 1, 1, 0.2f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(1, 1, 1, 0.5f);
    c[ImGuiCol_ResizeGripActive] = white;
    c[ImGuiCol_PlotHistogram] = ImVec4(0x6a / 255.f, 0x6a / 255.f, 0x6a / 255.f, 1.f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0x80 / 255.f, 0x80 / 255.f, 0x80 / 255.f, 1.f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(1, 1, 1, 0.30f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.60f);
    c[ImGuiCol_NavCursor] = white;
}

struct FontFile
{
    std::string path;
    int index; // face in a .ttc collection
};

// the han characters differ between chinese (traditional / simplified), japanese and korean, and a font of one of them
// lacks many forms of the others: the font of the ui language goes first ("zh-TW", "zh-CN", "ko", "ja")
static std::string g_fontScript = "ja";

// the program's own fonts (fonts/ next to the executable, SIL Open Font License): Noto Sans for latin / cyrillic /
// vietnamese and Noto Sans JP / SC / TC / KR, the ui language's one first. they are the same on every pc; the system
// fonts below are only used when these files are missing
static std::vector<FontFile> BundledFonts(bool cjk)
{
    const std::string dir = JoinPath(GetExeDir(), "fonts");
    if (!cjk) return {{JoinPath(dir, "NotoSans-Regular.ttf"), 0}};
    std::vector<const char*> order;
    if (g_fontScript == "zh-TW") order = {"NotoSansTC-Regular.ttf", "NotoSansSC-Regular.ttf", "NotoSansJP-Regular.ttf", "NotoSansKR-Regular.ttf"};
    else if (g_fontScript == "zh-CN") order = {"NotoSansSC-Regular.ttf", "NotoSansTC-Regular.ttf", "NotoSansJP-Regular.ttf", "NotoSansKR-Regular.ttf"};
    else if (g_fontScript == "ko") order = {"NotoSansKR-Regular.ttf", "NotoSansJP-Regular.ttf", "NotoSansSC-Regular.ttf", "NotoSansTC-Regular.ttf"};
    else order = {"NotoSansJP-Regular.ttf", "NotoSansSC-Regular.ttf", "NotoSansTC-Regular.ttf", "NotoSansKR-Regular.ttf"};
    std::vector<FontFile> v;
    for (const char* n : order) v.push_back({JoinPath(dir, n), 0});
    return v;
}

static std::vector<FontFile> FontCandidates(bool cjk)
{
    std::vector<FontFile> v = BundledFonts(cjk);
    bool haveAll = true;
    for (const auto& b : v) haveAll = haveAll && PathExists(b.path);
    if (haveAll) return v; // complete set: no system font needed
#ifdef _WIN32
    char windir[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(windir, MAX_PATH);
    std::string f = std::string(windir) + "\\Fonts\\";
    if (!cjk)
    {
        for (const char* n : {"segoeui.ttf", "tahoma.ttf", "arial.ttf"}) v.push_back({f + n, 0});
    }
    else
    {
        // one font per script (the first one installed of each group), so every script renders in every ui
        // language (the language list shows all of them); the ui language's script goes first
        const std::vector<const char*> ja = {"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"};
        const std::vector<const char*> sc = {"msyh.ttc", "simsun.ttc"};
        const std::vector<const char*> tc = {"msjh.ttc", "mingliu.ttc"};
        const std::vector<const char*> ko = {"malgun.ttf", "gulim.ttc"};
        std::vector<std::vector<const char*>> groups;
        if (g_fontScript == "zh-TW") groups = {tc, sc, ja, ko};
        else if (g_fontScript == "zh-CN") groups = {sc, tc, ja, ko};
        else if (g_fontScript == "ko") groups = {ko, ja, sc, tc};
        else groups = {ja, sc, tc, ko};
        for (const auto& g : groups)
            for (const char* n : g)
                if (PathExists(f + n)) { v.push_back({f + n, 0}); break; }
    }
#else
    // noto sans cjk collections: faces 0 japanese, 1 korean, 2 simplified chinese, 3 traditional chinese
    const int noto = g_fontScript == "zh-TW" ? 3 : g_fontScript == "zh-CN" ? 2 : g_fontScript == "ko" ? 1 : 0;
    // ask fontconfig first
    const std::string lang = g_fontScript == "zh-TW" ? "zh-tw" : g_fontScript == "zh-CN" ? "zh-cn" : g_fontScript;
    const std::string cmd = cjk ? "fc-match -f '%{file}\\t%{index}' 'sans:lang=" + lang + "' 2>/dev/null" : "fc-match -f '%{file}' sans 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (p)
    {
        char buf[1024] = {0};
        if (fgets(buf, sizeof(buf), p))
        {
            std::string s = Trim(buf);
            const size_t t = s.find('\t');
            v.push_back({s.substr(0, t), t == std::string::npos ? 0 : atoi(s.c_str() + t + 1)});
        }
        pclose(p);
    }
    if (!cjk)
    {
        for (const char* c : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                              "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
                              "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                              "/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"})
            v.push_back({c, 0});
    }
    else
    {
        for (const char* c : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                              "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc"})
            v.push_back({c, noto});
        for (const char* c : {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
                              "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"})
            v.push_back({c, 0});
    }
#endif
    return v;
}

// a font file read once and shared by every window's imgui context (the cjk files are 5-10 MB each)
static const std::vector<char>* FontData(const std::string& path)
{
    static std::map<std::string, std::vector<char>> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.empty() ? nullptr : &it->second;
    std::vector<char>& d = cache[path];
    std::string bytes;
    if (ReadTextFileUtf8(path, bytes)) d.assign(bytes.begin(), bytes.end()); // binary read (utf-8 path)
    return d.empty() ? nullptr : &d;
}

static ImFont* AddFontShared(const FontFile& f, float size, bool merge)
{
    const std::vector<char>* d = FontData(f.path);
    if (!d) return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // the cache keeps the data
    cfg.MergeMode = merge;
    cfg.FontNo = f.index;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)d->data(), (int)d->size(), size, &cfg);
}

static void LoadFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float size = BASE_FONT * scale;
    ImFont* base = nullptr;
    for (const auto& f : FontCandidates(false))
    {
        if (!PathExists(f.path)) continue;
        // .ttf/.ttc only (fc-match may answer with a .pfb or .otf with cff outlines that stb_truetype still reads)
        base = AddFontShared(f, size, false);
        if (base) break;
    }
    if (!base)
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        base = io.Fonts->AddFontFromMemoryTTF((void*)roboto_medium_ttf, (int)roboto_medium_ttf_len, size, &cfg);
    }
    // merge cjk fonts so every language (and the language list, which shows all of them) renders; a glyph comes from
    // the first font that has it, so the ui language's own font is first and the others fill in (glyphs load on demand)
    int merged = 0;
    std::vector<std::string> seen;
    for (const auto& f : FontCandidates(true))
    {
        if (!PathExists(f.path) || std::find(seen.begin(), seen.end(), f.path) != seen.end()) continue;
        seen.push_back(f.path);
        if (AddFontShared(f, size, true)) merged++;
        if (merged >= 6) break; // one per script (4); linux system fonts: the noto / fontconfig pick and fallbacks
    }
}

static float ContentScale(GLFWwindow* w)
{
    float xs = 1.f, ys = 1.f;
    if (w) glfwGetWindowContentScale(w, &xs, &ys);
    else
    {
        GLFWmonitor* m = glfwGetPrimaryMonitor();
        if (m) glfwGetMonitorContentScale(m, &xs, &ys);
    }
    if (xs < 0.5f || xs > 4.f) xs = 1.f;
    return xs;
}

static void DropCallback(GLFWwindow* w, int count, const char** paths)
{
    if (!g_dialog) return;
    std::vector<std::string> v;
    for (int i = 0; i < count; i++) v.push_back(paths[i]);
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    g_dialog->OnDrop(v, (float)mx, (float)my);
    glfwPostEmptyEvent();
}

static void CloseCallback(GLFWwindow* w)
{
    if (!g_dialog) return;
    g_dialog->RequestClose();
    glfwSetWindowShouldClose(w, g_dialog->WantClose() ? GLFW_TRUE : GLFW_FALSE);
}

static void ErrorCallback(int code, const char* desc)
{
    fprintf(stderr, "glfw error %d: %s\n", code, desc);
}


// ============================================================================ "?" guides in their own os windows
//
// each guide is a separate glfw window with its own gl context and dear imgui context, so it can be moved
// anywhere (also outside the main window) instead of being clipped by the main window's edges.

typedef void (APIENTRYP_W2X PFN_Viewport)(int, int, int, int);
typedef void (APIENTRYP_W2X PFN_ClearColor)(float, float, float, float);
typedef void (APIENTRYP_W2X PFN_Clear)(unsigned int);

struct HelpWindow
{
    GLFWwindow* window = nullptr;
    ImGuiContext* ctx = nullptr;
    int kind = 0;
    float styleScale = 0.f;
    PFN_Viewport viewport = nullptr;
    PFN_ClearColor clearColor = nullptr;
    PFN_Clear clear = nullptr;
    // dialog windows: shown once sized to their content
    bool shown = false;
    bool keepPos = false; // explorer: back at its previous place
    int frames = 0;
    std::string title;
};

static std::vector<HelpWindow> g_helpWindows;

static void DarkTitleBar(GLFWwindow* w)
{
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(w);
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
#else
    (void)w;
#endif
}

static void SetLinuxIcon(GLFWwindow* w)
{
#ifndef _WIN32
    int iw, ih, c;
    unsigned char* px = stbi_load_from_memory(app_icon_png, (int)app_icon_png_len, &iw, &ih, &c, 4);
    if (px)
    {
        GLFWimage img = {iw, ih, px};
        glfwSetWindowIcon(w, 1, &img);
        stbi_image_free(px);
    }
#else
    (void)w;
#endif
}

// centred over the main window
static void CenterOver(GLFWwindow* mainWindow, GLFWwindow* w)
{
    int mx, my, mw, mh, ww, wh;
    glfwGetWindowPos(mainWindow, &mx, &my);
    glfwGetWindowSize(mainWindow, &mw, &mh);
    glfwGetWindowSize(w, &ww, &wh);
    glfwSetWindowPos(w, mx + (mw - ww) / 2, my + std::max(0, (mh - wh) / 2));
}

// a hidden child os window (owned by the main window) with its own imgui context
static bool CreateChildWindow(GLFWwindow* mainWindow, HelpWindow& h, const std::string& title, int w, int hgt, bool resizable, float ui, float dpi)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifndef _WIN32
    glfwWindowHintString(GLFW_X11_CLASS_NAME, APP_NAME);
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, APP_NAME);
#endif
    GLFWwindow* hw = glfwCreateWindow(w, hgt, title.c_str(), nullptr, nullptr);
    if (!hw)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        hw = glfwCreateWindow(w, hgt, title.c_str(), nullptr, nullptr);
    }
    glfwMakeContextCurrent(mainWindow);
    if (!hw) return false;

    if (resizable) glfwSetWindowSizeLimits(hw, (int)(320 * dpi), (int)(200 * dpi), GLFW_DONT_CARE, GLFW_DONT_CARE);
    CenterOver(mainWindow, hw);

    DarkTitleBar(hw);
    SetLinuxIcon(hw);
#ifdef _WIN32
    // owned by the main window: stays in front of it and minimizes with it, but is never clipped by it
    SetWindowLongPtrW(glfwGetWin32Window(hw), GWLP_HWNDPARENT, (LONG_PTR)glfwGetWin32Window(mainWindow));
#endif

    h.window = hw;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    glfwMakeContextCurrent(hw);
    glfwSwapInterval(0); // do not let a second vsync wait slow the main window down
    h.viewport = (PFN_Viewport)glfwGetProcAddress("glViewport");
    h.clearColor = (PFN_ClearColor)glfwGetProcAddress("glClearColor");
    h.clear = (PFN_Clear)glfwGetProcAddress("glClear");

    h.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(h.ctx);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(hw, true);
    ImGui_ImplOpenGL3_Init(nullptr);
    ApplyDarkStyle(ui);
    h.styleScale = ui;
    LoadFonts(dpi);

    ImGui::SetCurrentContext(prev);
    glfwMakeContextCurrent(mainWindow);
    return true;
}

static void OpenHelpWindow(GLFWwindow* mainWindow, DialogEvent& dialog, int kind, float ui, float dpi)
{
    // already open: bring it to the front
    for (auto& h : g_helpWindows)
        if (h.kind == kind)
        {
            glfwShowWindow(h.window);
            glfwFocusWindow(h.window);
            return;
        }

    // same size limits as before (scaled with the program), the text scrolls inside
    const int w = (int)((kind == 1 ? 900.f : 760.f) * ui);
    const int hgt = (int)((kind == 1 ? 520.f : 420.f) * ui);
    HelpWindow h;
    h.title = dialog.HelpTitle(kind);
    if (!CreateChildWindow(mainWindow, h, h.title, w, hgt, true, ui, dpi)) return;
    h.kind = kind;
    glfwShowWindow(h.window);
    g_helpWindows.push_back(h);
}

static void DestroyHelpWindow(HelpWindow& h)
{
    ImGuiContext* prev = ImGui::GetCurrentContext();
    glfwMakeContextCurrent(h.window);
    ImGui::SetCurrentContext(h.ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(h.ctx);
    glfwDestroyWindow(h.window);
    h.window = nullptr;
    ImGui::SetCurrentContext(prev == h.ctx ? nullptr : prev);
}

// one frame for every open guide; closed ones are destroyed
static void DrawHelpWindows(GLFWwindow* mainWindow, DialogEvent& dialog, float ui)
{
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    for (size_t i = 0; i < g_helpWindows.size();)
    {
        HelpWindow& h = g_helpWindows[i];
        bool close = glfwWindowShouldClose(h.window) != 0;
        if (!close)
        {
            // the ui language can change while the guide is open
            const std::string title = dialog.HelpTitle(h.kind);
            if (title != h.title)
            {
                glfwSetWindowTitle(h.window, title.c_str());
                h.title = title;
            }
            glfwMakeContextCurrent(h.window);
            ImGui::SetCurrentContext(h.ctx);
            if (std::fabs(ui - h.styleScale) > 0.001f)
            {
                ApplyDarkStyle(ui);
                h.styleScale = ui;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::PushFont(nullptr, BASE_FONT * ui);
            dialog.DrawHelpWindow(h.kind, ui, close);
            ImGui::PopFont();
            ImGui::Render();
            int fbw, fbh;
            glfwGetFramebufferSize(h.window, &fbw, &fbh);
            if (h.viewport) h.viewport(0, 0, fbw, fbh);
            if (h.clearColor && h.clear)
            {
                h.clearColor(0x1c / 255.f, 0x1c / 255.f, 0x1c / 255.f, 1.f);
                h.clear(0x00004000);
            }
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(h.window);
        }
        if (close)
        {
            DestroyHelpWindow(h);
            g_helpWindows.erase(g_helpWindows.begin() + i);
            continue;
        }
        i++;
    }
    ImGui::SetCurrentContext(mainCtx);
    glfwMakeContextCurrent(mainWindow);
}

static HelpWindow g_modal; // dialog window (app setting, message ...), window == nullptr when none

// fonts of the main window and of every child window, between frames
static void ReloadAllFonts(GLFWwindow* mainWindow, float dpi)
{
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    LoadFonts(dpi);
    std::vector<HelpWindow*> all;
    for (auto& h : g_helpWindows) all.push_back(&h);
    if (g_modal.window) all.push_back(&g_modal);
    for (HelpWindow* h : all)
    {
        glfwMakeContextCurrent(h->window);
        ImGui::SetCurrentContext(h->ctx);
        LoadFonts(dpi);
    }
    ImGui::SetCurrentContext(mainCtx);
    glfwMakeContextCurrent(mainWindow);
}

static void CloseAllHelpWindows(GLFWwindow* mainWindow)
{
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    for (auto& h : g_helpWindows)
        DestroyHelpWindow(h);
    g_helpWindows.clear();
    ImGui::SetCurrentContext(mainCtx);
    glfwMakeContextCurrent(mainWindow);
}

// ---------------------------------------------------------------- dialog window (one at a time, modal)



static void SetMainEnabled(GLFWwindow* mainWindow, bool enabled)
{
#ifdef _WIN32
    // like a win32 modal dialog: the owner ignores input, clicking it flashes the dialog
    EnableWindow(glfwGetWin32Window(mainWindow), enabled ? TRUE : FALSE);
#else
    (void)mainWindow; (void)enabled; // the dialog draws the main window disabled
#endif
}

// the explorer window's place, kept while a warning window is shown in its place, and between uses
static int g_browserX = 0, g_browserY = 0, g_browserW = 0, g_browserH = 0;

static void CloseModalWindow(GLFWwindow* mainWindow)
{
    if (!g_modal.window) return;
    if (g_modal.kind == DialogEvent::ModalBrowser && g_modal.shown)
    {
        glfwGetWindowPos(g_modal.window, &g_browserX, &g_browserY);
        glfwGetWindowSize(g_modal.window, &g_browserW, &g_browserH);
    }
    SetMainEnabled(mainWindow, true); // before destroying, so the focus returns to the main window
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    DestroyHelpWindow(g_modal);
    g_modal = HelpWindow();
    ImGui::SetCurrentContext(mainCtx);
    glfwMakeContextCurrent(mainWindow);
    glfwFocusWindow(mainWindow);
}

static void MainFocusCallback(GLFWwindow* w, int focused)
{
    // linux: the main window got the focus while a dialog is open - hand it to the dialog
    ImGui_ImplGlfw_WindowFocusCallback(w, focused); // imgui's own callback is replaced by this one
    if (focused && g_modal.window && g_modal.shown) glfwFocusWindow(g_modal.window);
}

static void UpdateModalWindow(GLFWwindow* mainWindow, DialogEvent& dialog, float ui, float dpi)
{
    const int kind = dialog.ModalKind();
    if (g_modal.window && g_modal.kind != kind) CloseModalWindow(mainWindow);
    if (kind == DialogEvent::ModalNone) return;

    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    if (!g_modal.window)
    {
        // the browser is resizable; the other dialogs are sized to their content after the first frames
        const bool browser = kind == DialogEvent::ModalBrowser;
        const int w = (int)((browser ? 980.f : 420.f) * ui);
        const int hgt = (int)((browser ? 600.f : 160.f) * ui);
        HelpWindow h;
        const std::string title = dialog.ModalTitle(kind);
        if (!CreateChildWindow(mainWindow, h, title, w, hgt, browser, ui, dpi))
        {
            dialog.CloseModal(kind); // no window possible: do not lock the program
            return;
        }
        h.kind = kind;
        h.title = title;
        if (browser && g_browserW > 0)
        {
            glfwSetWindowSize(h.window, g_browserW, g_browserH);
            h.keepPos = true;
        }
        g_modal = h;
        // the main window is locked only once the dialog is on screen (see below): a locked main window with no
        // visible dialog cannot even be closed
    }

    {
        const std::string title = dialog.ModalTitle(kind);
        if (title != g_modal.title)
        {
            glfwSetWindowTitle(g_modal.window, title.c_str());
            g_modal.title = title;
        }
    }
    if (glfwWindowShouldClose(g_modal.window))
    {
        glfwSetWindowShouldClose(g_modal.window, GLFW_FALSE);
        dialog.CloseModal(kind);
        return; // closed (or the next queued message) on the next frame
    }

    HelpWindow& h = g_modal;
    glfwMakeContextCurrent(h.window);
    ImGui::SetCurrentContext(h.ctx);
    if (std::fabs(ui - h.styleScale) > 0.001f)
    {
        ApplyDarkStyle(ui);
        h.styleScale = ui;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(nullptr, BASE_FONT * ui);
    ImVec2 fit(0, 0);
    dialog.DrawModal(kind, ui, fit);
    ImGui::PopFont();
    ImGui::Render();
    int fbw, fbh;
    glfwGetFramebufferSize(h.window, &fbw, &fbh);
    if (h.viewport) h.viewport(0, 0, fbw, fbh);
    if (h.clearColor && h.clear)
    {
        h.clearColor(0x1c / 255.f, 0x1c / 255.f, 0x1c / 255.f, 1.f);
        h.clear(0x00004000);
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(h.window);
    ImGui::SetCurrentContext(mainCtx);
    glfwMakeContextCurrent(mainWindow);
    h.frames++;

    // fit the os window to the dialog content (imgui needs a couple of frames to measure it)
    bool sized = true;
    if (fit.x > 0 && fit.y > 0)
    {
        const int tw = (int)std::ceil(fit.x), th = (int)std::ceil(fit.y);
        int ww, wh;
        glfwGetWindowSize(h.window, &ww, &wh);
        if (std::abs(tw - ww) > 1 || std::abs(th - wh) > 1)
        {
            glfwSetWindowSize(h.window, tw, th);
            if (!h.shown) CenterOver(mainWindow, h.window);
            sized = false;
        }
    }
    // shown once sized to its content, or after a few frames whatever the size (never stays hidden)
    if (!h.shown && ((sized && h.frames >= 3) || h.frames >= 12))
    {
        if (h.keepPos) glfwSetWindowPos(h.window, g_browserX, g_browserY);
        else CenterOver(mainWindow, h.window);
        glfwShowWindow(h.window);
#ifdef _WIN32
        // in front of the main window and of open guides, and the active window
        HWND hw = glfwGetWin32Window(h.window);
        SetWindowPos(hw, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hw);
#endif
        glfwFocusWindow(h.window);
        h.shown = true;
        SetMainEnabled(mainWindow, false);
    }
    // a dialog that is somehow hidden again while the main window is locked: bring it back
    if (h.shown && !glfwGetWindowAttrib(h.window, GLFW_VISIBLE)) glfwShowWindow(h.window);
    if (!h.shown || !sized) glfwPostEmptyEvent(); // next frame right away, not after the idle timeout
}

static int RealMain(int argc, char** argv)
{
#ifdef __linux__
    // process name for top / system monitor (the kernel keeps at most 15 characters)
    prctl(PR_SET_NAME, "RePicturer", 0, 0, 0);
#endif
    glfwSetErrorCallback(ErrorCallback);
#if defined(__linux__) && defined(GLFW_PLATFORM)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit())
    {
#ifdef _WIN32
        MessageBoxW(NULL, L"Failed to initialize the window system (GLFW).", APP_NAME_W, MB_ICONERROR);
#else
        fprintf(stderr, "failed to initialize GLFW (an X11 display / XWayland is required)\n");
#endif
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifndef _WIN32
    // linux taskbars / docks group and label windows by WM_CLASS
    glfwWindowHintString(GLFW_X11_CLASS_NAME, APP_NAME);
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, APP_NAME);
#endif

    float scale = ContentScale(nullptr);
    GLFWwindow* window = glfwCreateWindow((int)std::ceil(BASE_W * scale), (int)std::ceil(BASE_H * scale), APP_TITLE, nullptr, nullptr);
    if (!window)
    {
        // very old drivers: try a 2.1 context
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifndef _WIN32
        glfwWindowHintString(GLFW_X11_CLASS_NAME, APP_NAME);
        glfwWindowHintString(GLFW_X11_INSTANCE_NAME, APP_NAME);
#endif
        window = glfwCreateWindow((int)std::ceil(BASE_W * scale), (int)std::ceil(BASE_H * scale), APP_TITLE, nullptr, nullptr);
    }
    if (!window)
    {
#ifdef _WIN32
        MessageBoxW(NULL, L"Failed to create an OpenGL window.\nPlease update the graphics driver.", APP_NAME_W, MB_ICONERROR);
#else
        fprintf(stderr, "failed to create an OpenGL window\n");
#endif
        glfwTerminate();
        return 1;
    }

#ifdef _WIN32
    {
        // dark title bar (windows 10 1809+ attribute 19, 20H1+ attribute 20)
        HWND hwnd = glfwGetWin32Window(window);
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        // allow drops from an elevated explorer like the caffe gui
        ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, NULL);
        ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, NULL);
        ChangeWindowMessageFilterEx(hwnd, 0x0049, MSGFLT_ALLOW, NULL);
    }
#else
    {
        int w, h, c;
        unsigned char* px = stbi_load_from_memory(app_icon_png, (int)app_icon_png_len, &w, &h, &c, 4);
        if (px)
        {
            GLFWimage img = {w, h, px};
            glfwSetWindowIcon(window, 1, &img);
            stbi_image_free(px);
        }
    }
#endif

    // center like DS_CENTER
    {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
        int ww, wh;
        glfwGetWindowSize(window, &ww, &wh);
        if (vm)
        {
            int mx = 0, my = 0;
            glfwGetMonitorPos(mon, &mx, &my);
            glfwSetWindowPos(window, mx + (vm->width - ww) / 2, my + (vm->height - wh) / 2);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(nullptr);

    // the framebuffer may be bigger than the window on hidpi setups; layout uses window coordinates
    scale = ContentScale(window);
    {
        int fbw, fbh, ww, wh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glfwGetWindowSize(window, &ww, &wh);
        if (ww > 0 && fbw > ww) scale = 1.0f; // macOS style scaling: coordinates are already logical
    }
    ApplyDarkStyle(scale);
    LoadFonts(scale);

    DialogEvent dialog;
    g_dialog = &dialog;
    dialog.Create(argc, argv);

    glfwSetDropCallback(window, DropCallback);
    glfwSetWindowCloseCallback(window, CloseCallback);
    glfwSetWindowFocusCallback(window, MainFocusCallback);
    glfwShowWindow(window);

    // no gl headers needed (none in the linux cross sysroot): fetch the three calls used here
    typedef void (APIENTRYP_W2X PFN_Viewport)(int, int, int, int);
    typedef void (APIENTRYP_W2X PFN_ClearColor)(float, float, float, float);
    typedef void (APIENTRYP_W2X PFN_Clear)(unsigned int);
    PFN_Viewport pglViewport = (PFN_Viewport)glfwGetProcAddress("glViewport");
    PFN_ClearColor pglClearColor = (PFN_ClearColor)glfwGetProcAddress("glClearColor");
    PFN_Clear pglClear = (PFN_Clear)glfwGetProcAddress("glClear");

    // fill mode: the window can be resized / maximized; the content scales with the window width up to a
    // 1360 px wide column, centred horizontally, pinned to the top
    const float MAX_CONTENT_W = 1360.0f;
    float dpi = scale;
    glfwSetWindowSizeLimits(window, (int)std::ceil(BASE_W * dpi), (int)std::ceil(BASE_H * dpi), GLFW_DONT_CARE, GLFW_DONT_CARE);

    float styleScale = scale;
    while (!glfwWindowShouldClose(window))
    {
        glfwWaitEventsTimeout(dialog.IsBusy() ? 0.1 : 0.25);
        glfwMakeContextCurrent(window);
        ImGui::SetCurrentContext(mainCtx);

        const float s = ContentScale(window);
        if (s != dpi && s > 0)
        {
            // moved to a monitor with another dpi: keep the minimum size in physical pixels right
            dpi = s;
            glfwSetWindowSizeLimits(window, (int)std::ceil(BASE_W * dpi), (int)std::ceil(BASE_H * dpi), GLFW_DONT_CARE, GLFW_DONT_CARE);
        }

        int ww = 0, wh = 0;
        glfwGetWindowSize(window, &ww, &wh);
        float ui = dpi;
        if (ww > 0 && wh > 0)
        {
            ui = std::min(std::min((float)ww / BASE_W, (float)wh / BASE_H), MAX_CONTENT_W * dpi / BASE_W);
            ui = std::max(ui, 0.5f);
        }
        if (std::fabs(ui - styleScale) > 0.001f)
        {
            ApplyDarkStyle(ui);
            styleScale = ui;
        }
        // log: grows into spare height, up to twice its normal size (55 -> 110 dialog units)
        {
            const float availDlu = wh / (ui * 1.625f);
            dialog.SetLogHeight(std::min(110.0f, std::max(55.0f, availDlu - 214.0f - 7.0f)));
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::PushFont(nullptr, BASE_FONT * ui); // fonts are rasterized on demand at this size

        dialog.Draw(ui);

        ImGui::PopFont();
        ImGui::Render();
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (pglViewport) pglViewport(0, 0, fbw, fbh);
        if (pglClearColor && pglClear)
        {
            pglClearColor(0x1c / 255.f, 0x1c / 255.f, 0x1c / 255.f, 1.f);
            pglClear(0x00004000); // GL_COLOR_BUFFER_BIT
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        // "?" guides: open requested ones, draw the open ones in their own windows
        if (int req = dialog.TakeHelpRequest())
            OpenHelpWindow(window, dialog, req, ui, dpi);
        DrawHelpWindows(window, dialog, ui);
        // app setting, messages, folder browser, reset confirm: their own window, the main window waits
        UpdateModalWindow(window, dialog, ui, dpi);

        // another ui language: its cjk font goes first, in every window
        if (dialog.FontScript() != g_fontScript)
        {
            g_fontScript = dialog.FontScript();
            ReloadAllFonts(window, dpi);
        }

        if (dialog.WantClose()) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    CloseModalWindow(window);
    CloseAllHelpWindows(window);
    g_dialog = nullptr;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

#ifdef _WIN32
// utf-8 argv on windows
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, NULL, NULL);
        args.push_back(s);
    }
    LocalFree(wargv);
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    return RealMain(argc, argv.data());
}
#else
int main(int argc, char** argv)
{
    return RealMain(argc, argv);
}
#endif
