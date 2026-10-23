// language string table, port of waifu2x-caffe-gui LangStringList.h
// (same LangList.txt + json files, no boost / rapidjson dependency)

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <stdint.h>

#include "Util.h"

class LangStringList
{
public:
    struct stLangSetting
    {
        std::string LangName;
        int LangID = 0;
        int SubLangID = 0;
        std::string FileName;
    };

private:
    std::string LangBaseDir;
    std::vector<stLangSetting> LangList;
    stLangSetting NowLang;
    std::unordered_map<std::string, std::string> LangStringMap;
    std::unordered_map<std::string, std::string> DefaultLangStringMap; // fallback: the first language in the list

    static int getNum(const std::string& str)
    {
        if (str.length() >= 3 && str.substr(0, 2) == "0x")
            return (int)strtol(str.c_str(), NULL, 16);
        return (int)strtol(str.c_str(), NULL, 10);
    }

    // minimal json reader for a flat {"key":"value", ...} object
    static bool ParseJsonObject(const std::string& s, std::unordered_map<std::string, std::string>& out)
    {
        size_t i = 0;
        auto ws = [&]() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; };
        auto str = [&](std::string& v) -> bool {
            ws();
            if (i >= s.size() || s[i] != '"') return false;
            i++;
            v.clear();
            while (i < s.size() && s[i] != '"')
            {
                char c = s[i++];
                if (c != '\\') { v.push_back(c); continue; }
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e)
                {
                case 'n': v.push_back('\n'); break;
                case 'r': v.push_back('\r'); break;
                case 't': v.push_back('\t'); break;
                case 'b': v.push_back('\b'); break;
                case 'f': v.push_back('\f'); break;
                case 'u':
                {
                    if (i + 4 > s.size()) return false;
                    unsigned int cp = (unsigned int)strtol(s.substr(i, 4).c_str(), NULL, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
                    {
                        unsigned int lo = (unsigned int)strtol(s.substr(i + 2, 4).c_str(), NULL, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                    }
                    AppendUtf8(v, cp);
                    break;
                }
                default: v.push_back(e); break;
                }
            }
            if (i >= s.size()) return false;
            i++;
            return true;
        };
        ws();
        if (i >= s.size() || s[i] != '{') return false;
        i++;
        for (;;)
        {
            ws();
            if (i < s.size() && s[i] == '}') return true;
            std::string k, v;
            if (!str(k)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return false;
            i++;
            if (!str(v)) return false;
            if (!v.empty()) out[k] = v;
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') return true;
            return false;
        }
    }

    static void AppendUtf8(std::string& v, unsigned int cp)
    {
        if (cp < 0x80) v.push_back((char)cp);
        else if (cp < 0x800) { v.push_back((char)(0xC0 | (cp >> 6))); v.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { v.push_back((char)(0xE0 | (cp >> 12))); v.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); v.push_back((char)(0x80 | (cp & 0x3F))); }
        else { v.push_back((char)(0xF0 | (cp >> 18))); v.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); v.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); v.push_back((char)(0x80 | (cp & 0x3F))); }
    }

    // win32 resource strings use "&&" for "&" and "\r\n" for new lines
    static std::string Normalize(const std::string& s)
    {
        std::string o;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '&' && i + 1 < s.size() && s[i + 1] == '&') { o.push_back('&'); i++; continue; }
            if (s[i] == '\r') continue;
            o.push_back(s[i]);
        }
        return o;
    }

    void ReadLangFile(const stLangSetting& lang, std::unordered_map<std::string, std::string>& map) const
    {
        map.clear();
        if (lang.FileName.empty()) return;
        std::string data;
        if (!ReadTextFileUtf8(LangBaseDir + lang.FileName, data)) return;
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);
        std::unordered_map<std::string, std::string> raw;
        ParseJsonObject(data, raw);
        for (auto& kv : raw) map[kv.first] = Normalize(kv.second);
    }

public:
    void SetLangBaseDir(const std::string& dir)
    {
        LangBaseDir = dir;
        if (!LangBaseDir.empty() && LangBaseDir.back() != '\\' && LangBaseDir.back() != '/')
            LangBaseDir += '/';
    }

    bool ReadLangList(const std::string& path)
    {
        std::string data;
        if (!ReadTextFileUtf8(path, data)) return false;
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);

        LangList.clear();
        std::istringstream is(data);
        std::string line;
        while (std::getline(is, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == ';') continue;
            std::vector<std::string> cols;
            size_t pos = 0;
            while (pos <= line.size())
            {
                size_t t = line.find('\t', pos);
                if (t == std::string::npos) t = line.size();
                std::string c = line.substr(pos, t - pos);
                if (!c.empty()) cols.push_back(c);
                pos = t + 1;
            }
            if (cols.size() != 4) continue;
            stLangSetting ls;
            ls.LangName = cols[0];
            ls.LangID = getNum(cols[1]);
            ls.SubLangID = getNum(cols[2]);
            ls.FileName = cols[3];
            LangList.push_back(ls);
        }
        if (LangList.empty()) return false;

        // default: english (first entry), not the system language
        SetLang(LangList[0]);

        ReadLangFile(LangList[0], DefaultLangStringMap);
        return true;
    }

    void SetLang(int primary, int sub)
    {
        int findPrimary = -1, findSub = -1;
        for (size_t i = 0; i < LangList.size(); i++)
        {
            if (LangList[i].LangID == primary)
            {
                if (findPrimary < 0) findPrimary = (int)i;
                if (LangList[i].SubLangID == sub) { findSub = (int)i; break; }
            }
        }
        if (findSub >= 0) NowLang = LangList[findSub];
        else if (findPrimary >= 0) NowLang = LangList[findPrimary];
        else if (!LangList.empty()) NowLang = LangList[0];
        ReadLangFile(NowLang, LangStringMap);
    }

    void SetLang(const stLangSetting& lang)
    {
        NowLang = lang;
        ReadLangFile(NowLang, LangStringMap);
    }

    const stLangSetting& GetLang() const { return NowLang; }
    const std::vector<stLangSetting>& GetLangList() const { return LangList; }
    const std::unordered_map<std::string, std::string>& DefaultMap() const { return DefaultLangStringMap; } // english

    const std::string& GetString(const char* key) const
    {
        static const std::string none;
        auto it = LangStringMap.find(key);
        if (it != LangStringMap.end()) return it->second;
        it = DefaultLangStringMap.find(key);
        if (it != DefaultLangStringMap.end()) return it->second;
        return none;
    }

    // GetString with a hard coded english fallback for keys that older json files do not have
    std::string Get(const char* key, const char* fallback) const
    {
        const std::string& s = GetString(key);
        return s.empty() ? std::string(fallback) : s;
    }
};
