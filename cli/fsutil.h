// filesystem / string helpers shared by repicturer-cli (enhanced waifu2x-ncnn-vulkan)
// (replaces the original filesystem_utils.h, adds recursive listing and mkdir -p)

#ifndef FSUTIL_H
#define FSUTIL_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#if _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#endif

#if _WIN32
typedef std::wstring path_t;
#define PATHSTR(X) L##X
#else
typedef std::string path_t;
#define PATHSTR(X) X
#endif

// ---------------------------------------------------------------- utf8

static inline std::wstring utf8_to_wide(const std::string& s)
{
    std::wstring out;
    size_t i = 0;
    while (i < s.size())
    {
        unsigned int c = (unsigned char)s[i];
        unsigned int cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xFFFD; extra = 0; }
        i++;
        for (int k = 0; k < extra; k++)
        {
            if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            i++;
        }
        if (sizeof(wchar_t) == 2 && cp >= 0x10000)
        {
            cp -= 0x10000;
            out.push_back((wchar_t)(0xD800 + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
        else
            out.push_back((wchar_t)cp);
    }
    return out;
}

static inline std::string wide_to_utf8(const std::wstring& w)
{
    std::string out;
    for (size_t i = 0; i < w.size(); i++)
    {
        unsigned int cp = (unsigned int)w[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size())
        {
            unsigned int lo = (unsigned int)w[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    return out;
}

#if _WIN32
static inline std::string path_to_utf8(const path_t& p) { return wide_to_utf8(p); }
static inline path_t utf8_to_path(const std::string& s) { return utf8_to_wide(s); }
#else
static inline std::string path_to_utf8(const path_t& p) { return p; }
static inline path_t utf8_to_path(const std::string& s) { return s; }
#endif

static inline path_t to_lower(path_t s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = s[i] - 'A' + 'a';
    return s;
}

// ---------------------------------------------------------------- paths

static inline bool is_sep(path_t::value_type c)
{
#if _WIN32
    return c == L'/' || c == L'\\';
#else
    return c == '/';
#endif
}

static inline path_t path_join(const path_t& a, const path_t& b)
{
    if (a.empty()) return b;
    if (is_sep(a[a.size() - 1])) return a + b;
    return a + PATHSTR("/") + b;
}

static inline path_t path_parent(const path_t& p)
{
    size_t i = p.size();
    while (i > 0 && is_sep(p[i - 1])) i--;
    while (i > 0 && !is_sep(p[i - 1])) i--;
    while (i > 1 && is_sep(p[i - 1])) i--;
    return p.substr(0, i);
}

static inline path_t path_filename(const path_t& p)
{
    size_t end = p.size();
    while (end > 0 && is_sep(p[end - 1])) end--;
    size_t i = end;
    while (i > 0 && !is_sep(p[i - 1])) i--;
    return p.substr(i, end - i);
}

static inline path_t get_file_name_without_extension(const path_t& path)
{
    size_t dot = path.rfind(PATHSTR('.'));
    size_t sep = path.size();
    for (size_t i = path.size(); i > 0; i--)
        if (is_sep(path[i - 1])) { sep = i - 1; break; }
    if (dot == path_t::npos || (sep != path.size() && dot < sep))
        return path;
    return path.substr(0, dot);
}

static inline path_t get_file_extension(const path_t& path)
{
    size_t dot = path.rfind(PATHSTR('.'));
    if (dot == path_t::npos)
        return path_t();
    for (size_t i = dot; i < path.size(); i++)
        if (is_sep(path[i])) return path_t();
    return path.substr(dot + 1);
}

// ---------------------------------------------------------------- fs

#if _WIN32
static inline bool path_exists(const path_t& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static inline bool path_is_directory(const path_t& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static inline bool make_one_dir(const path_t& path)
{
    if (CreateDirectoryW(path.c_str(), NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && path_is_directory(path);
}

// list regular files (names relative to dirpath); recursive lists "sub/dir/name"
static int list_directory_impl(const path_t& root, const path_t& rel, std::vector<path_t>& out, bool recursive)
{
    path_t dir = rel.empty() ? root : path_join(root, rel);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path_join(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return rel.empty() ? -1 : 0;

    std::vector<path_t> subdirs;
    std::vector<path_t> files;
    do
    {
        path_t name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        path_t relname = rel.empty() ? name : rel + L"/" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (recursive) subdirs.push_back(relname);
        }
        else
            files.push_back(relname);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(files.begin(), files.end());
    out.insert(out.end(), files.begin(), files.end());
    std::sort(subdirs.begin(), subdirs.end());
    for (size_t i = 0; i < subdirs.size(); i++)
        list_directory_impl(root, subdirs[i], out, recursive);
    return 0;
}
#else
static inline bool path_exists(const path_t& path)
{
    struct stat s;
    return stat(path.c_str(), &s) == 0;
}

static inline bool path_is_directory(const path_t& path)
{
    struct stat s;
    if (stat(path.c_str(), &s) != 0)
        return false;
    return S_ISDIR(s.st_mode);
}

static inline bool make_one_dir(const path_t& path)
{
    if (mkdir(path.c_str(), 0755) == 0) return true;
    return errno == EEXIST && path_is_directory(path);
}

static int list_directory_impl(const path_t& root, const path_t& rel, std::vector<path_t>& out, bool recursive)
{
    path_t dir = rel.empty() ? root : path_join(root, rel);
    DIR* d = opendir(dir.c_str());
    if (!d)
        return rel.empty() ? -1 : 0;

    std::vector<path_t> subdirs;
    std::vector<path_t> files;
    struct dirent* ent = 0;
    while ((ent = readdir(d)))
    {
        path_t name = ent->d_name;
        if (name == "." || name == "..") continue;
        path_t relname = rel.empty() ? name : rel + "/" + name;
        bool isdir = false;
        if (ent->d_type == DT_DIR) isdir = true;
        else if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK) isdir = path_is_directory(path_join(root, relname));
        if (isdir)
        {
            if (recursive) subdirs.push_back(relname);
        }
        else
            files.push_back(relname);
    }
    closedir(d);

    std::sort(files.begin(), files.end());
    out.insert(out.end(), files.begin(), files.end());
    std::sort(subdirs.begin(), subdirs.end());
    for (size_t i = 0; i < subdirs.size(); i++)
        list_directory_impl(root, subdirs[i], out, recursive);
    return 0;
}
#endif

static inline int list_directory(const path_t& dirpath, std::vector<path_t>& files, bool recursive)
{
    files.clear();
    return list_directory_impl(dirpath, path_t(), files, recursive);
}

static inline bool make_directories(const path_t& path)
{
    if (path.empty() || path_is_directory(path)) return true;
    path_t parent = path_parent(path);
    if (!parent.empty() && parent != path && !path_is_directory(parent))
    {
        if (!make_directories(parent)) return false;
    }
    return make_one_dir(path);
}

static inline FILE* open_file(const path_t& path, const char* mode)
{
#if _WIN32
    std::wstring wmode(mode, mode + strlen(mode));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

static inline bool read_file(const path_t& path, std::vector<unsigned char>& data)
{
    FILE* fp = open_file(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len < 0) { fclose(fp); return false; }
    rewind(fp);
    data.resize((size_t)len);
    size_t n = len > 0 ? fread(data.data(), 1, (size_t)len, fp) : 0;
    fclose(fp);
    return n == (size_t)len;
}

static inline bool write_file(const path_t& path, const void* data, size_t size)
{
    FILE* fp = open_file(path, "wb");
    if (!fp) return false;
    size_t n = size > 0 ? fwrite(data, 1, size, fp) : 0;
    int e = fclose(fp);
    return n == size && e == 0;
}

static inline path_t get_executable_directory()
{
#if _WIN32
    wchar_t filepath[4096];
    DWORD n = GetModuleFileNameW(NULL, filepath, 4096);
    path_t p(filepath, n);
    return path_parent(p);
#else
    char filepath[4096];
    ssize_t n = readlink("/proc/self/exe", filepath, sizeof(filepath) - 1);
    if (n <= 0) return ".";
    filepath[n] = 0;
    return path_parent(std::string(filepath));
#endif
}

// relative model paths are resolved against the executable directory like upstream
static inline path_t sanitize_dirpath(const path_t& path)
{
    if (path_is_directory(path)) return path;
    path_t p = path_join(get_executable_directory(), path);
    if (path_is_directory(p)) return p;
    return path;
}

#endif // FSUTIL_H
