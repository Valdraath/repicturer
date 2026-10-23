#include "FileBrowser.h"
#include "Util.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imgui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#else
#include <unistd.h>
#endif

// "%s items" -> "12 items" (no printf: a translation cannot break it)
static std::string Fill(std::string f, std::initializer_list<std::string> args)
{
    for (const auto& a : args)
    {
        const size_t p = f.find("%s");
        if (p == std::string::npos) break;
        f.replace(p, 2, a);
    }
    return f;
}

static std::string SizeText(unsigned long long b)
{
    char buf[64];
    if (b >= 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024));
    else snprintf(buf, sizeof(buf), "%llu KB", (b + 1023) / 1024);
    return buf;
}

static std::string Lower(const std::string& s) { return ToLowerAscii(s); }

// ---------------------------------------------------------------- name order (like windows explorer)

static unsigned NextCodePoint(const std::string& s, size_t& i)
{
    const unsigned char c = (unsigned char)s[i++];
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : extra == 1 ? c & 0x1F : c;
    while (extra-- > 0 && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

// the plain letter (A-Z) of a latin letter, accented ones included (vietnamese too); 0 = not a latin letter
static char BaseLetter(unsigned cp)
{
    if (cp >= 'a' && cp <= 'z') return (char)(cp - 32);
    if (cp >= 'A' && cp <= 'Z') return (char)cp;
    static const char latin1[] = "AAAAAACEEEEIIIIDNOOOOO\0OUUUUYTS";          // U+00C0-00DF (\0: multiplication sign)
    static const char extA[] = "AAAAAACCCCCCCCDDDDEEEEEEEEEEGGGGGGGGHHHHIIIIIIIIIIIIJJKKKLLLLLLLLLLNNNNNNNNNOOOOOOOORRRRRRSSSSSSSSTTTTTTUUUUUUUUUUUUWWYYYZZZZZZS"; // U+0100-017F
    static const char viet[] = "AAAAAAAAAAAAAAAAAAAAAAAAEEEEEEEEEEEEEEEEIIIIOOOOOOOOOOOOOOOOOOOOOOOOUUUUUUUUUUUUUUYYYYYYYY"; // U+1EA0-1EF9
    if (cp >= 0xC0 && cp <= 0xDF) return latin1[cp - 0xC0];
    if (cp >= 0xE0 && cp <= 0xFF) return cp == 0xF7 ? 0 : cp == 0xFF ? 'Y' : latin1[cp - 0xE0];
    if (cp >= 0x100 && cp <= 0x17F) return extA[cp - 0x100];
    if (cp == 0x1A0 || cp == 0x1A1) return 'O';
    if (cp == 0x1AF || cp == 0x1B0) return 'U';
    if (cp >= 0x1EA0 && cp <= 0x1EF9) return viet[cp - 0x1EA0];
    return 0;
}

// order of one character: 0 symbols / spaces, 1 digits (handled as numbers), 2 letters
static void CharKey(unsigned cp, int& cls, unsigned& key)
{
    if (const char l = BaseLetter(cp)) { cls = 2; key = (unsigned)(l - 'A'); return; }
    const bool symbol = cp < 0x80 || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFF0F) || cp == 0xD7 || cp == 0xF7 || (cp >= 0xA0 && cp <= 0xBF);
    if (symbol) { cls = 0; key = cp; return; }
    cls = 2; key = 26 + cp; // other alphabets: after latin, in code point order
}

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// < 0: a first. numbers by value, letters alphabetical (case and accents only decide a tie)
static int NameCompare(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    int tie = 0;
    while (i < a.size() && j < b.size())
    {
        if (IsDigit(a[i]) && IsDigit(b[j]))
        {
            size_t ie = i, je = j;
            while (ie < a.size() && IsDigit(a[ie])) ie++;
            while (je < b.size() && IsDigit(b[je])) je++;
            size_t iz = i, jz = j; // leading zeros do not count
            while (iz + 1 < ie && a[iz] == '0') iz++;
            while (jz + 1 < je && b[jz] == '0') jz++;
            if (ie - iz != je - jz) return ie - iz < je - jz ? -1 : 1;
            const int c = a.compare(iz, ie - iz, b, jz, je - jz);
            if (c) return c;
            if (!tie && (ie - i) != (je - j)) tie = (ie - i) < (je - j) ? -1 : 1; // 01 after 1
            i = ie; j = je;
            continue;
        }
        const unsigned ca = NextCodePoint(a, i), cb = NextCodePoint(b, j);
        int clsA, clsB;
        unsigned ka, kb;
        CharKey(ca, clsA, ka);
        CharKey(cb, clsB, kb);
        if (IsDigit((char)ca) && ca < 0x80) clsA = 1;
        if (IsDigit((char)cb) && cb < 0x80) clsB = 1;
        if (clsA != clsB) return clsA < clsB ? -1 : 1;
        if (ka != kb) return ka < kb ? -1 : 1;
        if (!tie && ca != cb)
        {
            // same letter: plain before accented, then lower case before upper case
            const bool plainA = ca < 0x80, plainB = cb < 0x80;
            if (plainA != plainB) tie = plainA ? -1 : 1;
            else tie = ca > cb ? -1 : 1; // 'a' (97) before 'A' (65)
        }
    }
    if (i < a.size() || j < b.size()) return i < a.size() ? 1 : -1; // shorter first
    return tie;
}

static bool IsHidden(const fs::path& p, const std::string& name)
{
#ifdef _WIN32
    (void)name;
    const DWORD a = GetFileAttributesW(p.wstring().c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
    (void)p;
    return !name.empty() && name[0] == '.';
#endif
}

// ---------------------------------------------------------------- places (sidebar)

#ifdef _WIN32
static std::string KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR w = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w)) && w) r = PathU8(fs::path(w));
    if (w) CoTaskMemFree(w);
    return r;
}

static std::string DriveLabel(const std::string& root)
{
    wchar_t name[MAX_PATH + 1] = {0};
    GetVolumeInformationW(U8Path(root).wstring().c_str(), name, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
    return PathU8(fs::path(name));
}
#else
// ~/.config/user-dirs.dirs: XDG_DOWNLOAD_DIR="$HOME/Downloads"
static std::string XdgDir(const std::string& home, const char* key, const char* fallback)
{
    std::string data;
    if (ReadTextFileUtf8(JoinPath(home, ".config/user-dirs.dirs"), data))
        for (const auto& line : Split(data, '\n', true))
        {
            const std::string l = Trim(line);
            if (l.rfind(key, 0) != 0) continue;
            const size_t a = l.find('"'), b = l.rfind('"');
            if (a == std::string::npos || b <= a) continue;
            std::string v = l.substr(a + 1, b - a - 1);
            if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
            return v;
        }
    return JoinPath(home, fallback);
}
#endif

void FileBrowser::BuildPlaces()
{
    places.clear();
#ifdef _WIN32
    const std::pair<const char*, const KNOWNFOLDERID*> kf[] = {
        {"Home", &FOLDERID_Profile}, {"Desktop", &FOLDERID_Desktop}, {"Documents", &FOLDERID_Documents},
        {"Downloads", &FOLDERID_Downloads}, {"Pictures", &FOLDERID_Pictures}, {"Videos", &FOLDERID_Videos}, {"Music", &FOLDERID_Music}};
    for (const auto& k : kf)
    {
        const std::string p = KnownFolder(*k.second);
        if (IsDirectory(p)) places.push_back({T(k.first), p, false});
    }
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
        if (mask & (1u << i))
        {
            const std::string root = std::string(1, (char)('A' + i)) + ":\\";
            const std::string label = DriveLabel(root);
            places.push_back({(label.empty() ? T("Drive") : label) + " (" + root.substr(0, 2) + ")", root, true});
        }
#else
    const std::string home = HomeFolder();
    places.push_back({T("Home"), home, false});
    const std::pair<const char*, std::pair<const char*, const char*>> xd[] = {
        {"Desktop", {"XDG_DESKTOP_DIR", "Desktop"}}, {"Documents", {"XDG_DOCUMENTS_DIR", "Documents"}},
        {"Downloads", {"XDG_DOWNLOAD_DIR", "Downloads"}}, {"Pictures", {"XDG_PICTURES_DIR", "Pictures"}},
        {"Videos", {"XDG_VIDEOS_DIR", "Videos"}}, {"Music", {"XDG_MUSIC_DIR", "Music"}}};
    for (const auto& x : xd)
    {
        const std::string p = XdgDir(home, x.second.first, x.second.second);
        if (IsDirectory(p) && p != home) places.push_back({T(x.first), p, false});
    }
    places.push_back({T("Root"), "/", true});
    for (const char* mnt : {"/media", "/mnt", "/run/media"})
    {
        std::error_code ec;
        if (!IsDirectory(mnt)) continue;
        for (auto it = fs::directory_iterator(mnt, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (!it->is_directory(ec)) continue;
            bool sub = false; // /media/<user>/<disk> or /mnt/<disk>
            if (std::string(mnt) != "/mnt")
                for (auto it2 = fs::directory_iterator(it->path(), ec); !ec && it2 != fs::directory_iterator(); it2.increment(ec))
                    if (it2->is_directory(ec)) { places.push_back({PathU8(it2->path().filename()), PathU8(it2->path()), true}); sub = true; }
            if (!sub) places.push_back({PathU8(it->path().filename()), PathU8(it->path()), true});
            ec.clear();
        }
    }
#endif
}

// ---------------------------------------------------------------- open / navigate / list

void FileBrowser::Open(Mode m, const std::string& t, const std::string& initialDir,
                       const std::vector<std::string>& e, const std::string& ok, const std::string& cancel)
{
    mode = m;
    title = t;
    exts = e;
    okLabel = ok;
    cancelLabel = cancel;
    result.clear();
    error.clear();
    back.clear();
    forward.clear();
    searchBuf[0] = nameBuf[0] = 0; // no search history
    editingPath = false;
    typed.clear();
    BuildPlaces();
    cwd.clear();
    Navigate(IsDirectory(initialDir) ? initialDir : HomeFolder(), false);
    open = true;
}

std::string FileBrowser::Parent() const
{
    const std::string p = ParentPath(cwd);
    return p.empty() ? cwd : p; // a drive root / "/" has no parent
}

void FileBrowser::Navigate(const std::string& dir, bool addHistory)
{
    std::error_code ec;
    fs::path p = fs::absolute(U8Path(dir), ec);
    if (ec) p = U8Path(dir);
    std::string target = PathU8(p.lexically_normal());
    if (target.size() > 1 && (target.back() == '/' || target.back() == '\\') && !(target.size() == 3 && target[1] == ':')) target.pop_back();
#ifdef _WIN32
    if (target.size() == 2 && target[1] == ':') target += "\\";
#endif
    if (addHistory && target != cwd && !cwd.empty())
    {
        back.push_back(cwd);
        forward.clear();
    }
    cwd = target;
    editingPath = false;
    snprintf(pathBuf, sizeof(pathBuf), "%s", NativeSeparators(cwd).c_str());
    searchBuf[0] = 0;
    nameBuf[0] = 0;
    selected = -1;
    error.clear();
    Refresh();
}

void FileBrowser::GoBack()
{
    if (back.empty()) return;
    forward.push_back(cwd);
    const std::string d = back.back();
    back.pop_back();
    Navigate(d, false);
}

void FileBrowser::GoForward()
{
    if (forward.empty()) return;
    back.push_back(cwd);
    const std::string d = forward.back();
    forward.pop_back();
    Navigate(d, false);
}

void FileBrowser::GoUp()
{
    const std::string up = Parent();
    if (up == cwd) return;
    const std::string from = FileName(cwd);
    Navigate(up);
    // like explorer: the folder we came from is selected
    for (int v = 0; v < (int)view.size(); v++)
        if (entries[view[v]].name == from) { Select(v); scrollToSelected = true; break; }
}

void FileBrowser::Refresh()
{
    const std::string keep = selected >= 0 && selected < (int)view.size() ? entries[view[selected]].name : std::string();
    entries.clear();
    std::error_code ec;
    fs::directory_iterator it(U8Path(cwd), fs::directory_options::skip_permission_denied, ec);
    if (ec) error = ec.message();
    for (; !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        Entry e;
        e.name = PathU8(it->path().filename());
        std::error_code ec2;
        e.dir = it->is_directory(ec2);
        e.hidden = IsHidden(it->path(), e.name);
        if (!e.dir)
        {
            // input: the image types of "Input file extensions"; folder dialog: folders only
            if (mode == ModeFolder) continue;
            if (!exts.empty() && std::find(exts.begin(), exts.end(), Lower(Extension(e.name))) == exts.end()) continue;
            e.size = (unsigned long long)it->file_size(ec2);
        }
        entries.push_back(e);
    }
    selected = -1;
    Sort();
    for (int v = 0; v < (int)view.size(); v++)
        if (!keep.empty() && entries[view[v]].name == keep) selected = v;
}

// folders first, then name or size; the search box filters by name as you type
void FileBrowser::Sort()
{
    const int col = prefs.sortColumn;
    const bool desc = prefs.sortDescending;
    const std::string keep = selected >= 0 && selected < (int)view.size() ? entries[view[selected]].name : std::string();
    std::stable_sort(entries.begin(), entries.end(), [=](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        int c = col == 1 ? (a.size < b.size ? -1 : a.size > b.size ? 1 : 0) : 0;
        if (c == 0) c = NameCompare(a.name, b.name);
        return desc ? c > 0 : c < 0;
    });
    view.clear();
    const std::string q = Lower(Trim(searchBuf));
    for (int i = 0; i < (int)entries.size(); i++)
        if (q.empty() || Lower(entries[i].name).find(q) != std::string::npos) view.push_back(i);
    selected = -1;
    for (int v = 0; v < (int)view.size(); v++)
        if (!keep.empty() && entries[view[v]].name == keep) selected = v;
}

// ---------------------------------------------------------------- choosing

void FileBrowser::Select(int v)
{
    selected = v >= 0 && v < (int)view.size() ? v : -1;
    snprintf(nameBuf, sizeof(nameBuf), "%s", selected >= 0 ? entries[view[selected]].name.c_str() : "");
}

// Enter / double click: into a folder, choose a file
void FileBrowser::OpenEntry(int v, bool& accepted)
{
    if (v < 0 || v >= (int)view.size()) return;
    const Entry& e = entries[view[v]];
    if (e.dir) { Navigate(JoinPath(cwd, e.name)); return; }
    Select(v);
    if (Accept()) accepted = true;
}

// the chosen item: the name field (the selected item or a typed name / path), else the folder shown
bool FileBrowser::Accept()
{
    result.clear();
    const std::string text = Trim(nameBuf);
    if (!text.empty())
    {
        const std::string full = U8Path(text).is_absolute() ? text : JoinPath(cwd, text);
        if (!PathExists(full)) { Warn(T("not found:") + " " + text); return false; }
        result.push_back(full);
    }
    else
        result.push_back(cwd);
    if (mode == ModeFolder && !IsDirectory(result[0])) { Warn(T("Choose a folder.")); return false; }
    if (validate)
    {
        const std::string why = validate(result);
        if (!why.empty()) { Warn(why); return false; }
    }
    return true;
}

// 7-Zip: typing letters jumps to the next name that starts with them
void FileBrowser::JumpToTyped(unsigned int c)
{
    const double now = ImGui::GetTime();
    if (now - typedTime > 1.0) typed.clear();
    typedTime = now;
    if (c < 0x80) typed += (char)c;
    else if (c < 0x800) { typed += (char)(0xC0 | (c >> 6)); typed += (char)(0x80 | (c & 0x3F)); }
    else { typed += (char)(0xE0 | (c >> 12)); typed += (char)(0x80 | ((c >> 6) & 0x3F)); typed += (char)(0x80 | (c & 0x3F)); }
    const std::string q = Lower(typed);
    const int n = (int)view.size();
    for (int k = 0; k < n; k++)
    {
        const int v = (std::max(selected, 0) + (typed.size() == 1 ? 1 : 0) + k) % n;
        if (Lower(entries[view[v]].name).rfind(q, 0) == 0) { Select(v); scrollToSelected = true; return; }
    }
}

// ---------------------------------------------------------------- drawing

// icons drawn with lines (no icon font): folder, file, drive
static void DrawIcon(ImDrawList* dl, ImVec2 p, float sz, int kind, float alpha)
{
    const ImVec4 c = kind == 0 ? ImVec4(0.93f, 0.74f, 0.33f, 1.0f) : kind == 2 ? ImVec4(0.55f, 0.72f, 0.95f, 1.0f) : ImVec4(0.78f, 0.80f, 0.84f, 0.85f);
    const ImU32 col = ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
    const float w = sz, h = sz * 0.78f, y = p.y + (sz - h) * 0.5f;
    if (kind == 0)
    {
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w * 0.45f, y + h * 0.25f), col, 1.5f);
        dl->AddRectFilled(ImVec2(p.x, y + h * 0.18f), ImVec2(p.x + w, y + h), col, 1.5f);
    }
    else if (kind == 1)
    {
        const float x0 = p.x + w * 0.15f, x1 = p.x + w * 0.85f, y0 = p.y, y1 = p.y + sz, f = w * 0.25f;
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1 - f, y0), col);
        dl->AddLine(ImVec2(x1 - f, y0), ImVec2(x1, y0 + f), col);
        dl->AddLine(ImVec2(x1, y0 + f), ImVec2(x1, y1), col);
        dl->AddLine(ImVec2(x1, y1), ImVec2(x0, y1), col);
        dl->AddLine(ImVec2(x0, y1), ImVec2(x0, y0), col);
    }
    else
    {
        dl->AddRect(ImVec2(p.x, y + h * 0.2f), ImVec2(p.x + w, y + h), col, 2.0f);
        dl->AddCircleFilled(ImVec2(p.x + w * 0.8f, y + h * 0.6f), sz * 0.08f, col);
    }
}

// refresh symbol: an open circle with an arrow head
static bool RefreshButton(const char* id, float sz)
{
    const bool r = ImGui::Button(id, ImVec2(sz, sz));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float rad = sz * 0.26f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    dl->PathArcTo(c, rad, 0.6f, 5.6f, 16);
    dl->PathStroke(col, 0, 1.5f);
    const ImVec2 tip(c.x + rad * cosf(0.6f), c.y + rad * sinf(0.6f));
    dl->AddTriangleFilled(ImVec2(tip.x + rad * 0.55f, tip.y - rad * 0.1f), ImVec2(tip.x - rad * 0.45f, tip.y - rad * 0.25f), ImVec2(tip.x + rad * 0.1f, tip.y + rad * 0.6f), col);
    return r;
}

static void Tip(const std::string& text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text.c_str());
}

// a width that fits a label in every language (at least minW)
static float ButtonWidth(const std::string& label, float minW)
{
    return std::max(minW, ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2);
}

void FileBrowser::DrawNavBar(float s)
{
    const float fh = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(back.empty());
    if (ImGui::ArrowButton("##back", ImGuiDir_Left)) GoBack();
    ImGui::EndDisabled();
    Tip(T("Back") + " (Alt+Left)");
    ImGui::SameLine(0, 2 * s);
    ImGui::BeginDisabled(forward.empty());
    if (ImGui::ArrowButton("##fwd", ImGuiDir_Right)) GoForward();
    ImGui::EndDisabled();
    Tip(T("Forward") + " (Alt+Right)");
    ImGui::SameLine(0, 2 * s);
    ImGui::BeginDisabled(Parent() == cwd);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) GoUp();
    ImGui::EndDisabled();
    Tip(T("Up") + " (Backspace)");
    ImGui::SameLine(0, 2 * s);
    if (RefreshButton("##refresh", fh)) Refresh();
    Tip(T("Refresh") + " (F5)");
    ImGui::SameLine();
    const float searchW = std::max(170 * s, ImGui::GetContentRegionAvail().x * 0.24f);
    DrawAddressBar(s, ImGui::GetContentRegionAvail().x - searchW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
    // filters on every key stroke
    if (ImGui::InputTextWithHint("##search", (T("Search") + " (Ctrl+F)").c_str(), searchBuf, sizeof(searchBuf))) Sort();
}

// address bar: the folder shown as a normal path; click and type another one, Enter goes there
void FileBrowser::DrawAddressBar(float s, float width)
{
    (void)s;
    if (!editingPath) snprintf(pathBuf, sizeof(pathBuf), "%s", NativeSeparators(cwd).c_str()); // follows the folder shown
    ImGui::SetNextItemWidth(width);
    if (focusPath) { ImGui::SetKeyboardFocusHere(); focusPath = false; }
    const bool enter = ImGui::InputText("##path", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    editingPath = ImGui::IsItemActive();
    if (!enter) return;
    const std::string p = Trim(pathBuf);
    editingPath = false; // typed paths are not kept: the box shows the folder again
    if (p.empty()) return;
    if (IsDirectory(p)) Navigate(p);
    else if (PathExists(p)) // a file: its folder, the file selected
    {
        Navigate(ParentPath(p));
        for (int v = 0; v < (int)view.size(); v++) if (entries[view[v]].name == FileName(p)) { Select(v); scrollToSelected = true; }
    }
    else Warn(Fill(T("This path does not exist or is not a folder:\n%s"), {p}));
}

void FileBrowser::DrawPlaces(float s, float h)
{
    ImGui::BeginChild("##places", ImVec2(190 * s, h), ImGuiChildFlags_Borders);
    const float th = ImGui::GetTextLineHeight(), lh = th + 6 * s;
    const float isz = th * 0.95f;
    bool drives = false;
    for (size_t i = 0; i < places.size(); i++)
    {
        const Place& p = places[i];
        if (p.drive && !drives) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); drives = true; }
        ImGui::PushID((int)i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##place", p.path == cwd, 0, ImVec2(0, lh))) Navigate(p.path);
        DrawIcon(ImGui::GetWindowDrawList(), ImVec2(pos.x + 4 * s, pos.y + (lh - isz) * 0.5f), isz, p.drive ? 2 : 0, 1.0f);
        // a name that does not fit is cut with "..." (full name and path in the tooltip)
        const float maxW = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x - (pos.x + isz + 12 * s);
        std::string label = p.label;
        if (ImGui::CalcTextSize(label.c_str()).x > maxW)
        {
            while (!label.empty() && ImGui::CalcTextSize((label + "...").c_str()).x > maxW)
            {
                size_t k = label.size() - 1;
                while (k > 0 && ((unsigned char)label[k] & 0xC0) == 0x80) k--;
                label.erase(k);
            }
            label += "...";
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", p.label.c_str(), NativeSeparators(p.path).c_str());
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + isz + 12 * s, pos.y + (lh - th) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// the list: icon + name (with extension), size
void FileBrowser::DrawList(float s, float h, bool& accepted)
{
    const ImGuiTableFlags tf = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##list", 2, tf, ImVec2(0, h))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(T("Name").c_str(), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0, 0);
    ImGui::TableSetupColumn(T("Size").c_str(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 100 * s, 1);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs())
        if (ss->SpecsDirty && ss->SpecsCount > 0)
        {
            prefs.sortColumn = (int)ss->Specs[0].ColumnUserID;
            prefs.sortDescending = ss->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            Sort();
            ss->SpecsDirty = false;
        }
    const float th = ImGui::GetTextLineHeight(), lh = th + 6 * s;
    const float isz = th * 0.95f;
    int openRow = -1;
    ImGuiListClipper clipper;
    clipper.Begin((int)view.size());
    if (scrollToSelected && selected >= 0) clipper.IncludeItemByIndex(selected);
    while (clipper.Step())
        for (int v = clipper.DisplayStart; v < clipper.DisplayEnd; v++)
        {
            const Entry& e = entries[view[v]];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(v);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", v == selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, lh)))
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openRow = v;
                else Select(v);
            }
            if (scrollToSelected && v == selected) { ImGui::SetScrollHereY(0.5f); scrollToSelected = false; }
            DrawIcon(ImGui::GetWindowDrawList(), ImVec2(pos.x + 4 * s, pos.y + (lh - isz) * 0.5f), isz, e.dir ? 0 : 1, e.hidden ? 0.5f : 1.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + isz + 12 * s, pos.y + (lh - th) * 0.5f), ImGui::GetColorU32(e.hidden ? ImGuiCol_TextDisabled : ImGuiCol_Text), e.name.c_str());
            ImGui::TableNextColumn();
            if (!e.dir)
            {
                const std::string sz = SizeText(e.size);
                const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(sz.c_str()).x;
                ImGui::SetCursorPos(ImVec2(x, ImGui::GetCursorPosY() + (lh - th) * 0.5f));
                ImGui::TextDisabled("%s", sz.c_str());
            }
            ImGui::PopID();
        }
    scrollToSelected = false;
    // a click on the empty part: nothing selected (OK then takes the folder shown)
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Select(-1);
    ImGui::EndTable();
    if (openRow >= 0) OpenEntry(openRow, accepted);
}

// keyboard (7-Zip / explorer)
void FileBrowser::HandleKeys(bool& accepted)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) { focusSearch = true; return; }
    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L)) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D)) || ImGui::IsKeyPressed(ImGuiKey_F4))
    {
        focusPath = true;
        return;
    }
    if (ImGui::IsAnyItemActive() || io.WantTextInput) return;
    const int n = (int)view.size();
    auto move = [&](int v) {
        if (n == 0) return;
        Select(std::max(0, std::min(n - 1, v)));
        scrollToSelected = true;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !io.KeyAlt) move(selected < 0 ? 0 : selected + 1);
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !io.KeyAlt) move(selected < 0 ? 0 : selected - 1);
    else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move(selected + 15);
    else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move(selected - 15);
    else if (ImGui::IsKeyPressed(ImGuiKey_Home)) move(0);
    else if (ImGui::IsKeyPressed(ImGuiKey_End)) move(n - 1);
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
    {
        if (selected >= 0 && entries[view[selected]].dir) OpenEntry(selected, accepted);
        else if (Accept()) accepted = true;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))) GoUp();
    else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) GoBack();
    else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) GoForward();
    else if (ImGui::IsKeyPressed(ImGuiKey_F5) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R))) Refresh();
    else if (!io.KeyCtrl && !io.KeyAlt)
        for (int i = 0; i < io.InputQueueCharacters.Size; i++)
            if (io.InputQueueCharacters[i] > ' ') JumpToTyped(io.InputQueueCharacters[i]);
}

bool FileBrowser::Draw(float scale)
{
    if (!open) return false;
    bool accepted = false;
    const float s = scale;

    const ImGuiStyle& st = ImGui::GetStyle();
    const bool showTypes = mode == ModeInput && !exts.empty();
    DrawNavBar(s);
    ImGui::Dummy(ImVec2(0, 2 * s));

    // below the list: status + hidden toggle, separator, name, accepted types, OK / Cancel
    const float bottomH = ImGui::GetFrameHeightWithSpacing() * 3 + (showTypes ? ImGui::GetTextLineHeightWithSpacing() : 0) + st.ItemSpacing.y * 3 + 2 * s;
    const float listH = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottomH);
    DrawPlaces(s, listH);
    ImGui::SameLine();
    DrawList(s, listH, accepted);

    // status bar: item count, or why OK did not work
    ImGui::AlignTextToFramePadding();
    if (!error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s", error.c_str());
    else ImGui::TextUnformatted(Fill(T("%s items"), {std::to_string(view.size())}).c_str());
    ImGui::Separator();

    // name field: the selected item, or a typed name / path
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T(mode == ModeFolder ? "Folder:" : "File name:").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", nameBuf, sizeof(nameBuf));

    // the image types listed (fixed by the program), left, on their own row
    if (showTypes)
    {
        std::string list;
        for (const auto& e : exts)
        {
            std::string u = e.substr(e[0] == '.' ? 1 : 0);
            for (char& c : u) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            list += (list.empty() ? "" : " | ") + u;
        }
        ImGui::TextUnformatted((T("Accepted file types:") + " " + list).c_str());
    }

    // OK / Cancel: wide enough for their labels in every language
    const float bw = ButtonWidth(cancelLabel, ButtonWidth(okLabel, 90 * s));
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button((okLabel + "##browserok").c_str(), ImVec2(bw, 0)) && Accept()) accepted = true;
    ImGui::SameLine();
    bool cancel = ImGui::Button((cancelLabel + "##browsercancel").c_str(), ImVec2(bw, 0));

    HandleKeys(accepted);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        if (!editingPath && !ImGui::IsAnyItemActive()) cancel = true; // Esc in the address bar only leaves it
    }
    if (cancel || accepted) open = false;
    return accepted;
}
