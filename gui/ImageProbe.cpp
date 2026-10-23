// image dimension probe for the split size list (caffe SetCropSizeList needs the gcd of width and height)

#include "ImageProbe.h"
#include "Util.h"

#include <fstream>
#include <string.h>

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static uint32_t rd32(const unsigned char* p, bool le) { return le ? (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) : (uint32_t)(((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
static uint16_t rd16(const unsigned char* p, bool le) { return le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]); }

bool ProbeImageSize(const std::string& path, int& w, int& h)
{
    w = h = 0;
    std::ifstream ifs(U8Path(path), std::ios::binary);
    if (!ifs) return false;
    // headers live at the start for every supported format except tiff, read a generous prefix
    std::vector<unsigned char> d(1 << 20);
    ifs.read((char*)d.data(), (std::streamsize)d.size());
    d.resize((size_t)ifs.gcount());
    const size_t n = d.size();
    if (n < 16) return false;
    const unsigned char* p = d.data();

    // webp
    if (memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0 && n >= 30)
    {
        if (memcmp(p + 12, "VP8 ", 4) == 0) { w = rd16(p + 26, true) & 0x3FFF; h = rd16(p + 28, true) & 0x3FFF; return w > 0; }
        if (memcmp(p + 12, "VP8L", 4) == 0)
        {
            uint32_t b = rd32(p + 21, true);
            w = (int)(b & 0x3FFF) + 1;
            h = (int)((b >> 14) & 0x3FFF) + 1;
            return true;
        }
        if (memcmp(p + 12, "VP8X", 4) == 0)
        {
            w = (int)(p[24] | (p[25] << 8) | (p[26] << 16)) + 1;
            h = (int)(p[27] | (p[28] << 8) | (p[29] << 16)) + 1;
            return true;
        }
        return false;
    }

    // tiff: first ifd may be anywhere, read the whole file if needed
    if ((p[0] == 'I' && p[1] == 'I' && p[2] == 42) || (p[0] == 'M' && p[1] == 'M' && p[3] == 42))
    {
        const bool le = p[0] == 'I';
        uint32_t off = rd32(p + 4, le);
        if (off + 2 > n)
        {
            std::string all;
            if (!ReadTextFileUtf8(path, all)) return false;
            d.assign(all.begin(), all.end());
            p = d.data();
        }
        if (off + 2 > d.size()) return false;
        uint16_t cnt = rd16(p + off, le);
        for (uint16_t i = 0; i < cnt; i++)
        {
            size_t e = off + 2 + (size_t)i * 12;
            if (e + 12 > d.size()) break;
            uint16_t tag = rd16(p + e, le), type = rd16(p + e + 2, le);
            uint32_t v = type == 3 ? rd16(p + e + 8, le) : rd32(p + e + 8, le);
            if (tag == 256) w = (int)v;
            if (tag == 257) h = (int)v;
        }
        return w > 0 && h > 0;
    }

    // jpeg 2000 (jp2 ihdr box / j2k SIZ marker)
    static const unsigned char jp2sig[12] = {0, 0, 0, 0x0C, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A};
    if (memcmp(p, jp2sig, 12) == 0)
    {
        for (size_t i = 12; i + 16 < n; i++)
            if (memcmp(p + i, "ihdr", 4) == 0) { h = (int)rd32(p + i + 4, false); w = (int)rd32(p + i + 8, false); return true; }
        return false;
    }
    if (p[0] == 0xFF && p[1] == 0x4F && p[2] == 0xFF && p[3] == 0x51 && n > 20)
    {
        uint32_t xs = rd32(p + 8, false), ys = rd32(p + 12, false), xo = rd32(p + 16, false), yo = rd32(p + 20, false);
        w = (int)(xs - xo);
        h = (int)(ys - yo);
        return true;
    }

    int comp;
    if (stbi_info_from_memory(p, (int)n, &w, &h, &comp)) return true;
    // stb may need the whole file (e.g. jpeg with a huge exif block)
    std::string all;
    if (!ReadTextFileUtf8(path, all)) return false;
    return stbi_info_from_memory((const unsigned char*)all.data(), (int)all.size(), &w, &h, &comp) != 0;
}
