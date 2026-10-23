// the program's own explorer (dark theme, dear imgui) for every file / folder choice, on windows and linux.
//
// layout like windows explorer / kde dolphin: back, forward, up, refresh, address bar (path segments, click to type),
// search (filters as you type); sidebar with the home folders and the drives; one list of name (with extension) and
// size with folder / file icons, folders first, sortable by a click on a column header; status bar; name field and
// OK / Cancel. one item is chosen: a file, or a folder (select it, or OK with nothing selected takes the folder shown).
// keyboard like the 7-Zip file manager: arrows, Page Up / Down, Home / End, Enter, Backspace, Alt+Left / Right,
// F5 / Ctrl+R, Ctrl+F (search), Ctrl+L (type a path), type a name to jump to it.

#pragma once

#include <string>
#include <functional>
#include <vector>

class FileBrowser
{
public:
    enum Mode
    {
        ModeInput,  // one image file or one folder
        ModeFolder, // one folder
    };

    // remembered between uses (setting.ini)
    struct Prefs
    {
        int sortColumn = 0;     // 0 name, 1 size
        bool sortDescending = false;
    };

    void Open(Mode mode, const std::string& title, const std::string& initialDir,
              const std::vector<std::string>& exts, const std::string& okLabel, const std::string& cancelLabel);

    // call every frame; returns true once when the user confirmed
    bool Draw(float scale);

    bool IsOpen() const { return open; }
    void Close() { open = false; }
    std::string Title() const { return title.substr(0, title.find("##")); }
    const std::vector<std::string>& Result() const { return result; }
    std::string CurrentFolder() const { return cwd; }

    std::function<std::string(const char*)> tr;                            // ui language
    std::function<std::string(const std::vector<std::string>&)> validate; // "" = the choice may be used
    std::function<void(const std::string&)> warn;                          // shows a warning in its own window
    Prefs prefs;

private:
    struct Entry
    {
        std::string name;
        bool dir = false, hidden = false;
        unsigned long long size = 0;
    };
    struct Place { std::string label, path; bool drive = false; };

    std::string T(const char* s) const { return tr ? tr(s) : std::string(s); }
    void Warn(const std::string& text) { if (warn) warn(text); else error = text; }
    void BuildPlaces();
    void Navigate(const std::string& dir, bool addHistory = true);
    void GoBack();
    void GoForward();
    void GoUp();
    std::string Parent() const;
    void Refresh();
    void Sort();
    void Select(int v);
    void OpenEntry(int v, bool& accepted);
    bool Accept();
    void JumpToTyped(unsigned int c);
    void DrawNavBar(float s);
    void DrawAddressBar(float s, float width);
    void DrawPlaces(float s, float h);
    void DrawList(float s, float h, bool& accepted);
    void HandleKeys(bool& accepted);

    Mode mode = ModeInput;
    bool open = false;
    std::string title, okLabel, cancelLabel;
    std::vector<std::string> exts;
    std::string cwd;
    std::vector<Entry> entries;
    std::vector<int> view;  // shown entries (search), in order
    int selected = -1;      // index into view
    bool scrollToSelected = false;
    std::vector<Place> places;
    std::vector<std::string> back, forward;
    std::vector<std::string> result;
    std::string error;
    char pathBuf[2048] = {0};
    char nameBuf[2048] = {0};
    char searchBuf[256] = {0};
    bool editingPath = false, focusPath = false, focusSearch = false;
    std::string typed;
    double typedTime = 0;
};
