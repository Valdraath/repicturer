// small cross platform helpers for the gui (utf-8 strings everywhere)

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

namespace fs = std::filesystem;

// utf-8 <-> std::filesystem::path
inline fs::path U8Path(const std::string& s)
{
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

inline std::string PathU8(const fs::path& p)
{
#if defined(__cpp_char8_t)
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

inline bool PathExists(const std::string& p)
{
    std::error_code ec;
    return !p.empty() && fs::exists(U8Path(p), ec);
}

inline bool IsDirectory(const std::string& p)
{
    std::error_code ec;
    return !p.empty() && fs::is_directory(U8Path(p), ec);
}

inline bool CreateDirs(const std::string& p)
{
    std::error_code ec;
    if (IsDirectory(p)) return true;
    fs::create_directories(U8Path(p), ec);
    return IsDirectory(p);
}

// boost::filesystem semantics used by the caffe gui
inline std::string ParentPath(const std::string& p) { return PathU8(U8Path(p).parent_path()); }
inline std::string FileName(const std::string& p) { return PathU8(U8Path(p).filename()); }
inline std::string Stem(const std::string& p) { return PathU8(U8Path(p).stem()); }
inline std::string Extension(const std::string& p) { return PathU8(U8Path(p).extension()); }
inline std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    return PathU8(U8Path(a) / U8Path(b));
}
inline std::string AbsolutePath(const std::string& p)
{
    std::error_code ec;
    fs::path r = fs::absolute(U8Path(p), ec);
    return ec ? p : PathU8(r);
}
inline std::string NativeSeparators(std::string p)
{
#ifdef _WIN32
    for (auto& c : p) if (c == '/') c = '\\';
#endif
    return p;
}

bool ReadTextFileUtf8(const std::string& path, std::string& out);
unsigned long GetCurrentProcessIdPortable();
FILE* OpenFileU8(const std::string& path, const char* mode); // fopen with a utf-8 path
std::string HomeFolder(); // C:\Users\<name> on windows, $HOME (/home/<name>) on linux
bool WriteTextFileUtf8(const std::string& path, const std::string& data);
std::string GetExeDir();
std::string GetTempDir();
void GetUserUILanguage(int& primary, int& sub);
std::string Format(const char* fmt, ...);
std::string ToLowerAscii(std::string s);
std::string Trim(const std::string& s);
bool ParseInt(const std::string& s, long& v);
bool ParseDouble(const std::string& s, double& v);
std::vector<std::string> Split(const std::string& s, char sep, bool drop_empty);
std::string DoubleToString(double v); // std::to_string compatible ("2.000000")

// ini file, same layout as the caffe gui setting.ini ([Setting] Key=Value), stored as utf-8
class IniFile
{
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    std::string Get(const std::string& section, const std::string& key, const std::string& def) const;
    int GetInt(const std::string& section, const std::string& key, int def) const;
    void Set(const std::string& section, const std::string& key, const std::string& value);
    void Remove(const std::string& section, const std::string& key);

private:
    struct Entry { std::string section, key, value; };
    std::vector<Entry> entries;
};
