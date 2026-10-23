// image decode / resize / encode for repicturer-cli (enhanced waifu2x-ncnn-vulkan)
//
// decoders : stb_image (png jpg bmp tga gif psd hdr pic pnm), libwebp, openjpeg (jp2 j2k), own tiff reader
// encoders : own png (8/16 bit), own tiff (8/16/32 bit, deflate), own exr (half/float, zip), own sun raster,
//            own ppm (8/16 bit), stb_image_write (bmp jpg tga hdr), libwebp, openjpeg (lossless jp2)

#include "image_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <algorithm>

#include <zlib.h>

#include "webp/decode.h"
#include "webp/encode.h"

#ifndef OPJ_STATIC
#define OPJ_STATIC
#endif
#include "openjpeg.h"

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "../common/formats.h"
#include "anim_io.h"

// ============================================================================ helpers

static inline uint16_t rd16(const unsigned char* p, bool le) { return le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const unsigned char* p, bool le) { return le ? (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) : (uint32_t)(((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

static void put_be32(std::vector<unsigned char>& v, uint32_t x) { v.push_back((unsigned char)(x >> 24)); v.push_back((unsigned char)(x >> 16)); v.push_back((unsigned char)(x >> 8)); v.push_back((unsigned char)x); }
static void put_le16(std::vector<unsigned char>& v, uint16_t x) { v.push_back((unsigned char)x); v.push_back((unsigned char)(x >> 8)); }
static void put_le32(std::vector<unsigned char>& v, uint32_t x) { v.push_back((unsigned char)x); v.push_back((unsigned char)(x >> 8)); v.push_back((unsigned char)(x >> 16)); v.push_back((unsigned char)(x >> 24)); }
static void put_le64(std::vector<unsigned char>& v, uint64_t x) { for (int i = 0; i < 8; i++) v.push_back((unsigned char)(x >> (8 * i))); }

static void to_rgb_or_rgba(const unsigned char* src, int w, int h, int sc, Image8& out)
{
    // sc: 1 gray, 2 gray+alpha, 3 rgb, 4 rgba
    out.w = w;
    out.h = h;
    out.c = (sc == 2 || sc == 4) ? 4 : 3;
    out.data.resize((size_t)w * h * out.c);
    const size_t n = (size_t)w * h;
    unsigned char* d = out.data.data();
    for (size_t i = 0; i < n; i++)
    {
        const unsigned char* s = src + i * sc;
        if (sc == 1) { d[0] = d[1] = d[2] = s[0]; d += 3; }
        else if (sc == 2) { d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; d += 4; }
        else if (sc == 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d += 3; }
        else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; d += 4; }
    }

    // fully opaque alpha is dropped so the faster 3 channel path is used
    if (out.c == 4)
    {
        bool opaque = true;
        for (size_t i = 0; i < n && opaque; i++)
            if (out.data[i * 4 + 3] != 255) opaque = false;
        if (opaque)
        {
            for (size_t i = 0; i < n; i++)
            {
                out.data[i * 3 + 0] = out.data[i * 4 + 0];
                out.data[i * 3 + 1] = out.data[i * 4 + 1];
                out.data[i * 3 + 2] = out.data[i * 4 + 2];
            }
            out.c = 3;
            out.data.resize(n * 3);
        }
    }
}

// ============================================================================ tiff reader

namespace {

struct TiffIfd
{
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> bps;
    uint32_t compression = 1, photometric = 2, spp = 1, rows_per_strip = 0xFFFFFFFF, planar = 1, predictor = 1;
    uint32_t sample_format = 1, fill_order = 1;
    uint32_t tile_w = 0, tile_h = 0;
    std::vector<uint32_t> offsets, counts;
    std::vector<uint16_t> colormap;
    std::vector<uint32_t> extra;
};

static bool tiff_lzw_decode(const unsigned char* in, size_t inlen, std::vector<unsigned char>& out, size_t expected)
{
    out.clear();
    out.reserve(expected);
    static const int MAXC = 4096;
    std::vector<int> prefix(MAXC), length(MAXC);
    std::vector<unsigned char> suffix(MAXC), first(MAXC), stack(MAXC);
    for (int i = 0; i < 256; i++) { prefix[i] = -1; suffix[i] = (unsigned char)i; first[i] = (unsigned char)i; length[i] = 1; }
    int next = 258;
    int width = 9;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    size_t pos = 0;
    int old = -1;
    auto emit = [&](int code) {
        int n = length[code];
        size_t base = out.size();
        out.resize(base + n);
        for (int k = n - 1; k >= 0; k--) { out[base + k] = suffix[code]; code = prefix[code]; }
    };
    while (out.size() < expected)
    {
        while (bitcnt < width)
        {
            if (pos >= inlen) return !out.empty();
            bitbuf = (bitbuf << 8) | in[pos++];
            bitcnt += 8;
        }
        int code = (int)((bitbuf >> (bitcnt - width)) & ((1u << width) - 1));
        bitcnt -= width;
        if (code == 257) break;
        if (code == 256) { next = 258; width = 9; old = -1; continue; }
        if (old == -1)
        {
            if (code > 255) return false;
            emit(code);
            old = code;
            continue;
        }
        if (code < next)
        {
            emit(code);
            if (next < MAXC) { prefix[next] = old; suffix[next] = first[code]; first[next] = first[old]; length[next] = length[old] + 1; next++; }
        }
        else if (code == next && next < MAXC)
        {
            prefix[next] = old; suffix[next] = first[old]; first[next] = first[old]; length[next] = length[old] + 1; next++;
            emit(code);
        }
        else
            return false;
        old = code;
        // tiff lzw uses "early change"
        if (next + 1 >= (1 << width) && width < 12) width++;
    }
    return true;
}

static bool tiff_packbits_decode(const unsigned char* in, size_t inlen, std::vector<unsigned char>& out, size_t expected)
{
    out.clear();
    size_t i = 0;
    while (i < inlen && out.size() < expected)
    {
        int n = (signed char)in[i++];
        if (n >= 0)
        {
            size_t cnt = (size_t)n + 1;
            if (i + cnt > inlen) cnt = inlen - i;
            out.insert(out.end(), in + i, in + i + cnt);
            i += cnt;
        }
        else if (n != -128)
        {
            if (i >= inlen) break;
            out.insert(out.end(), (size_t)(1 - n), in[i++]);
        }
    }
    return true;
}

static bool tiff_decode(const unsigned char* data, size_t size, Image8& out, DecodeInfo& info, std::string& err)
{
    if (size < 8) { err = "tiff too small"; return false; }
    bool le = data[0] == 'I';
    if (rd16(data + 2, le) != 42) { err = "bigtiff is not supported"; return false; }
    uint32_t ifdoff = rd32(data + 4, le);
    if (ifdoff + 2 > size) { err = "bad tiff"; return false; }

    TiffIfd t;
    uint16_t n = rd16(data + ifdoff, le);
    for (uint16_t i = 0; i < n; i++)
    {
        size_t e = ifdoff + 2 + (size_t)i * 12;
        if (e + 12 > size) break;
        uint16_t tag = rd16(data + e, le);
        uint16_t type = rd16(data + e + 2, le);
        uint32_t count = rd32(data + e + 4, le);
        int tsize = (type == 3) ? 2 : (type == 4) ? 4 : (type == 1 || type == 2 || type == 6 || type == 7) ? 1 : (type == 5 || type == 10 || type == 12) ? 8 : 4;
        size_t total = (size_t)tsize * count;
        const unsigned char* vp = total <= 4 ? data + e + 8 : data + rd32(data + e + 8, le);
        if (vp + total > data + size) continue;
        std::vector<uint32_t> vals(count);
        for (uint32_t k = 0; k < count; k++)
        {
            if (type == 3) vals[k] = rd16(vp + k * 2, le);
            else if (type == 4) vals[k] = rd32(vp + k * 4, le);
            else if (type == 1 || type == 7) vals[k] = vp[k];
            else vals[k] = 0;
        }
        uint32_t v0 = count > 0 ? vals[0] : 0;
        switch (tag)
        {
        case 256: t.width = v0; break;
        case 257: t.height = v0; break;
        case 258: t.bps = vals; break;
        case 259: t.compression = v0; break;
        case 262: t.photometric = v0; break;
        case 266: t.fill_order = v0; break;
        case 273: t.offsets = vals; break;
        case 277: t.spp = v0; break;
        case 278: t.rows_per_strip = v0; break;
        case 279: t.counts = vals; break;
        case 284: t.planar = v0; break;
        case 317: t.predictor = v0; break;
        case 320: t.colormap.assign(vals.begin(), vals.end()); break;
        case 322: t.tile_w = v0; break;
        case 323: t.tile_h = v0; break;
        case 324: t.offsets = vals; break;
        case 325: t.counts = vals; break;
        case 338: t.extra = vals; break;
        case 339: t.sample_format = v0; break;
        default: break;
        }
    }

    if (t.width == 0 || t.height == 0) { err = "bad tiff size"; return false; }
    const uint32_t bits = t.bps.empty() ? 1 : t.bps[0];
    const uint32_t spp = t.spp;
    if (!(bits == 1 || bits == 8 || bits == 16 || bits == 32)) { err = "unsupported tiff bit depth"; return false; }
    if (spp < 1 || spp > 8) { err = "unsupported tiff samples"; return false; }
    if (!(t.compression == 1 || t.compression == 5 || t.compression == 8 || t.compression == 32946 || t.compression == 32773))
    {
        err = "unsupported tiff compression";
        return false;
    }
    if (t.offsets.empty() || t.offsets.size() != t.counts.size()) { err = "bad tiff strips"; return false; }

    const bool tiled = t.tile_w > 0 && t.tile_h > 0;
    const uint32_t bw = tiled ? t.tile_w : t.width;
    const uint32_t bh = tiled ? t.tile_h : std::min(t.rows_per_strip, t.height);
    const uint32_t planes = t.planar == 2 ? spp : 1;
    const uint32_t spp_in_block = t.planar == 2 ? 1 : spp;
    const size_t block_row_bytes = ((size_t)bw * spp_in_block * bits + 7) / 8;
    const uint32_t across = tiled ? (t.width + bw - 1) / bw : 1;
    const uint32_t down = (t.height + bh - 1) / bh;

    // decoded samples as float 0..1 [h][w][spp]
    std::vector<float> samples((size_t)t.width * t.height * spp, 0.f);

    for (uint32_t pl = 0; pl < planes; pl++)
    {
        for (uint32_t by = 0; by < down; by++)
        {
            for (uint32_t bx = 0; bx < across; bx++)
            {
                size_t idx = (size_t)pl * across * down + (size_t)by * across + bx;
                if (idx >= t.offsets.size()) continue;
                uint32_t off = t.offsets[idx], cnt = t.counts[idx];
                if ((size_t)off + cnt > size) continue;
                const size_t expected = block_row_bytes * bh;
                std::vector<unsigned char> raw;
                const unsigned char* src = data + off;
                if (t.compression == 1)
                    raw.assign(src, src + std::min<size_t>(cnt, expected));
                else if (t.compression == 5)
                    tiff_lzw_decode(src, cnt, raw, expected);
                else if (t.compression == 32773)
                    tiff_packbits_decode(src, cnt, raw, expected);
                else
                {
                    raw.resize(expected);
                    uLongf dl = (uLongf)expected;
                    if (uncompress(raw.data(), &dl, src, cnt) != Z_OK && dl == 0)
                    {
                        err = "tiff deflate error";
                        return false;
                    }
                    raw.resize(dl);
                }
                raw.resize(expected, 0);

                if (t.fill_order == 2)
                    for (size_t k = 0; k < raw.size(); k++)
                    {
                        unsigned char b = raw[k], r = 0;
                        for (int q = 0; q < 8; q++) if (b & (1 << q)) r |= (unsigned char)(0x80 >> q);
                        raw[k] = r;
                    }

                // horizontal predictor
                if (t.predictor == 2)
                {
                    for (uint32_t y = 0; y < bh; y++)
                    {
                        unsigned char* row = raw.data() + y * block_row_bytes;
                        if (bits == 8)
                            for (size_t k = spp_in_block; k < (size_t)bw * spp_in_block; k++) row[k] = (unsigned char)(row[k] + row[k - spp_in_block]);
                        else if (bits == 16)
                            for (size_t k = spp_in_block; k < (size_t)bw * spp_in_block; k++)
                            {
                                uint16_t a = rd16(row + k * 2, le), b = rd16(row + (k - spp_in_block) * 2, le);
                                uint16_t v = (uint16_t)(a + b);
                                if (le) { row[k * 2] = (unsigned char)v; row[k * 2 + 1] = (unsigned char)(v >> 8); }
                                else { row[k * 2] = (unsigned char)(v >> 8); row[k * 2 + 1] = (unsigned char)v; }
                            }
                    }
                }

                for (uint32_t y = 0; y < bh; y++)
                {
                    uint32_t iy = by * bh + y;
                    if (iy >= t.height) break;
                    const unsigned char* row = raw.data() + y * block_row_bytes;
                    for (uint32_t x = 0; x < bw; x++)
                    {
                        uint32_t ix = bx * bw + x;
                        if (ix >= t.width) break;
                        for (uint32_t s = 0; s < spp_in_block; s++)
                        {
                            size_t k = (size_t)x * spp_in_block + s;
                            float v;
                            if (bits == 1) v = (row[k >> 3] >> (7 - (k & 7))) & 1 ? 1.f : 0.f;
                            else if (bits == 8) v = row[k] / 255.f;
                            else if (bits == 16) v = rd16(row + k * 2, le) / 65535.f;
                            else
                            {
                                uint32_t u = rd32(row + k * 4, le);
                                if (t.sample_format == 3) { float f; memcpy(&f, &u, 4); v = f; }
                                else v = (float)(u / 4294967295.0);
                            }
                            uint32_t ch = t.planar == 2 ? pl : s;
                            samples[((size_t)iy * t.width + ix) * spp + ch] = v;
                        }
                    }
                }
            }
        }
    }

    // to 8 bit
    const bool has_alpha = !t.extra.empty() || (t.photometric == 2 && spp >= 4) || ((t.photometric <= 1) && spp >= 2);
    const size_t npx = (size_t)t.width * t.height;
    std::vector<unsigned char> tmp;
    int sc;
    if (t.photometric == 3 && !t.colormap.empty())
    {
        const size_t ncol = t.colormap.size() / 3;
        sc = 3;
        tmp.resize(npx * 3);
        for (size_t i = 0; i < npx; i++)
        {
            size_t index = (size_t)lrintf(samples[i * spp] * (float)((1u << bits) - 1));
            if (index >= ncol) index = ncol - 1;
            tmp[i * 3 + 0] = (unsigned char)(t.colormap[index] >> 8);
            tmp[i * 3 + 1] = (unsigned char)(t.colormap[ncol + index] >> 8);
            tmp[i * 3 + 2] = (unsigned char)(t.colormap[ncol * 2 + index] >> 8);
        }
    }
    else if (t.photometric <= 1)
    {
        sc = has_alpha && spp >= 2 ? 2 : 1;
        tmp.resize(npx * sc);
        for (size_t i = 0; i < npx; i++)
        {
            float g = samples[i * spp];
            if (t.photometric == 0) g = 1.f - g;
            tmp[i * sc] = (unsigned char)lrintf(std::min(std::max(g, 0.f), 1.f) * 255.f);
            if (sc == 2) tmp[i * sc + 1] = (unsigned char)lrintf(std::min(std::max(samples[i * spp + 1], 0.f), 1.f) * 255.f);
        }
    }
    else if (t.photometric == 2 && spp >= 3)
    {
        sc = has_alpha && spp >= 4 ? 4 : 3;
        tmp.resize(npx * sc);
        for (size_t i = 0; i < npx; i++)
            for (int c = 0; c < sc; c++)
                tmp[i * sc + c] = (unsigned char)lrintf(std::min(std::max(samples[i * spp + c], 0.f), 1.f) * 255.f);
    }
    else
    {
        err = "unsupported tiff photometric interpretation";
        return false;
    }

    info.src_channels = (int)spp;
    info.src_depth = (int)bits;
    to_rgb_or_rgba(tmp.data(), (int)t.width, (int)t.height, sc, out);
    return true;
}

// ============================================================================ openjpeg memory streams

struct OpjMem
{
    const unsigned char* rdata = 0;
    size_t rsize = 0;
    std::vector<unsigned char>* wdata = 0;
    size_t pos = 0;
};

static OPJ_SIZE_T opj_mem_read(void* buf, OPJ_SIZE_T n, void* user)
{
    OpjMem* m = (OpjMem*)user;
    if (m->pos >= m->rsize) return (OPJ_SIZE_T)-1;
    size_t k = std::min((size_t)n, m->rsize - m->pos);
    memcpy(buf, m->rdata + m->pos, k);
    m->pos += k;
    return k;
}

static OPJ_SIZE_T opj_mem_write(void* buf, OPJ_SIZE_T n, void* user)
{
    OpjMem* m = (OpjMem*)user;
    if (m->pos + n > m->wdata->size()) m->wdata->resize(m->pos + n);
    memcpy(m->wdata->data() + m->pos, buf, n);
    m->pos += n;
    return n;
}

static OPJ_OFF_T opj_mem_skip(OPJ_OFF_T n, void* user)
{
    OpjMem* m = (OpjMem*)user;
    if (m->wdata)
    {
        if (n < 0 && (OPJ_OFF_T)m->pos + n < 0) return -1;
        m->pos += n;
        if (m->pos > m->wdata->size()) m->wdata->resize(m->pos);
        return n;
    }
    if (n < 0 && (OPJ_OFF_T)m->pos + n < 0) return -1;
    m->pos += n;
    return n;
}

static OPJ_BOOL opj_mem_seek(OPJ_OFF_T n, void* user)
{
    OpjMem* m = (OpjMem*)user;
    if (n < 0) return OPJ_FALSE;
    m->pos = (size_t)n;
    if (m->wdata && m->pos > m->wdata->size()) m->wdata->resize(m->pos);
    return OPJ_TRUE;
}

static void opj_quiet(const char*, void*) {}

static bool jp2_decode(const unsigned char* data, size_t size, bool j2k, Image8& out, DecodeInfo& info, std::string& err)
{
    opj_codec_t* codec = opj_create_decompress(j2k ? OPJ_CODEC_J2K : OPJ_CODEC_JP2);
    opj_set_error_handler(codec, opj_quiet, 0);
    opj_set_warning_handler(codec, opj_quiet, 0);
    opj_set_info_handler(codec, opj_quiet, 0);
    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    opj_setup_decoder(codec, &params);

    OpjMem mem;
    mem.rdata = data;
    mem.rsize = size;
    opj_stream_t* st = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE);
    opj_stream_set_user_data(st, &mem, 0);
    opj_stream_set_user_data_length(st, size);
    opj_stream_set_read_function(st, opj_mem_read);
    opj_stream_set_skip_function(st, opj_mem_skip);
    opj_stream_set_seek_function(st, opj_mem_seek);

    opj_image_t* img = 0;
    bool ok = opj_read_header(st, codec, &img) && opj_decode(codec, st, img) && opj_end_decompress(codec, st);
    opj_stream_destroy(st);
    opj_destroy_codec(codec);
    if (!ok || !img || img->numcomps == 0)
    {
        if (img) opj_image_destroy(img);
        err = "jpeg2000 decode failed";
        return false;
    }

    const int w = (int)img->comps[0].w, h = (int)img->comps[0].h;
    int nc = (int)std::min<OPJ_UINT32>(img->numcomps, 4);
    if (nc == 3 && img->numcomps >= 4) nc = 4;
    std::vector<unsigned char> tmp((size_t)w * h * nc);
    for (int c = 0; c < nc; c++)
    {
        const opj_image_comp_t& cp = img->comps[c];
        const int prec = (int)cp.prec;
        const int sh = cp.sgnd ? (1 << (prec - 1)) : 0;
        const float scale = 255.f / (float)((1u << prec) - 1);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int sx = std::min((int)cp.w - 1, x * (int)img->comps[0].dx / (int)cp.dx);
                int sy = std::min((int)cp.h - 1, y * (int)img->comps[0].dy / (int)cp.dy);
                int v = cp.data[(size_t)sy * cp.w + sx] + sh;
                tmp[((size_t)y * w + x) * nc + c] = (unsigned char)std::min(255, std::max(0, (int)lrintf(v * scale)));
            }
    }
    info.src_channels = nc;
    info.src_depth = (int)img->comps[0].prec;
    // sYCC is rare in practice; treat everything as RGB / gray
    opj_image_destroy(img);
    to_rgb_or_rgba(tmp.data(), w, h, nc, out);
    return true;
}

static bool jp2_encode(const OutImage& img, std::vector<unsigned char>& outbuf, std::string& err)
{
    const int nc = img.c;
    const int prec = img.depth == 16 ? 16 : 8;
    std::vector<opj_image_cmptparm_t> cp(nc);
    for (int c = 0; c < nc; c++)
    {
        memset(&cp[c], 0, sizeof(cp[c]));
        cp[c].dx = cp[c].dy = 1;
        cp[c].w = (OPJ_UINT32)img.w;
        cp[c].h = (OPJ_UINT32)img.h;
        cp[c].prec = (OPJ_UINT32)prec;
        cp[c].sgnd = 0;
    }
    opj_image_t* oimg = opj_image_create((OPJ_UINT32)nc, cp.data(), OPJ_CLRSPC_SRGB);
    if (!oimg) { err = "jpeg2000 image alloc failed"; return false; }
    oimg->x0 = oimg->y0 = 0;
    oimg->x1 = (OPJ_UINT32)img.w;
    oimg->y1 = (OPJ_UINT32)img.h;
    if (nc == 4) oimg->comps[3].alpha = 1;
    const size_t npx = (size_t)img.w * img.h;
    for (int c = 0; c < nc; c++)
    {
        OPJ_INT32* d = oimg->comps[c].data;
        if (prec == 16)
        {
            const uint16_t* s = (const uint16_t*)img.data.data();
            for (size_t i = 0; i < npx; i++) d[i] = s[i * nc + c];
        }
        else
        {
            const unsigned char* s = img.data.data();
            for (size_t i = 0; i < npx; i++) d[i] = s[i * nc + c];
        }
    }

    opj_cparameters_t params;
    opj_set_default_encoder_parameters(&params);
    params.tcp_numlayers = 1;
    params.tcp_rates[0] = 0; // lossless
    params.cp_disto_alloc = 1;
    params.irreversible = 0;
    params.tcp_mct = nc >= 3 ? 1 : 0;
    int minside = std::min(img.w, img.h);
    while (params.numresolution > 1 && (1 << (params.numresolution - 1)) > minside) params.numresolution--;

    opj_codec_t* codec = opj_create_compress(OPJ_CODEC_JP2);
    opj_set_error_handler(codec, opj_quiet, 0);
    opj_set_warning_handler(codec, opj_quiet, 0);
    opj_set_info_handler(codec, opj_quiet, 0);
    bool ok = opj_setup_encoder(codec, &params, oimg) != 0;

    OpjMem mem;
    mem.wdata = &outbuf;
    outbuf.clear();
    opj_stream_t* st = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE);
    opj_stream_set_user_data(st, &mem, 0);
    opj_stream_set_write_function(st, opj_mem_write);
    opj_stream_set_skip_function(st, opj_mem_skip);
    opj_stream_set_seek_function(st, opj_mem_seek);

    ok = ok && opj_start_compress(codec, oimg, st) && opj_encode(codec, st) && opj_end_compress(codec, st);
    opj_stream_destroy(st);
    opj_destroy_codec(codec);
    opj_image_destroy(oimg);
    if (!ok) { err = "jpeg2000 encode failed"; return false; }
    return true;
}

} // namespace

// ============================================================================ decode

bool image_decode(const unsigned char* data, size_t size, Image8& out, DecodeInfo& info, std::string& err)
{
    info = DecodeInfo();
    if (size < 4) { err = "file too small"; return false; }

    // webp
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
    {
        WebPBitstreamFeatures feat;
        if (WebPGetFeatures(data, size, &feat) != VP8_STATUS_OK) { err = "webp header error"; return false; }
        int w = 0, h = 0;
        unsigned char* px = feat.has_alpha ? WebPDecodeRGBA(data, size, &w, &h) : WebPDecodeRGB(data, size, &w, &h);
        if (!px) { err = "webp decode failed"; return false; }
        info.src_channels = feat.has_alpha ? 4 : 3;
        to_rgb_or_rgba(px, w, h, feat.has_alpha ? 4 : 3, out);
        WebPFree(px);
        return true;
    }

    // tiff
    if (size >= 8 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) || (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 42)))
        return tiff_decode(data, size, out, info, err);

    // jpeg 2000
    static const unsigned char jp2sig[12] = {0, 0, 0, 0x0C, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A};
    if (size >= 12 && memcmp(data, jp2sig, 12) == 0)
        return jp2_decode(data, size, false, out, info, err);
    if (size >= 4 && data[0] == 0xFF && data[1] == 0x4F && data[2] == 0xFF && data[3] == 0x51)
        return jp2_decode(data, size, true, out, info, err);

    // everything else through stb_image
    info.is_jpeg = data[0] == 0xFF && data[1] == 0xD8;
    int w = 0, h = 0, comp = 0;
    if (stbi_is_16_bit_from_memory(data, (int)size)) info.src_depth = 16;
    unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 0);
    if (!px)
    {
        const char* reason = stbi_failure_reason();
        err = std::string("decode failed") + (reason ? std::string(" (") + reason + ")" : std::string());
        return false;
    }
    info.src_channels = comp;
    to_rgb_or_rgba(px, w, h, comp, out);
    stbi_image_free(px);
    return true;
}

bool image_probe_size(const unsigned char* data, size_t size, int& w, int& h)
{
    w = h = 0;
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
        return WebPGetInfo(data, size, &w, &h) != 0;
    int comp;
    if (stbi_info_from_memory(data, (int)size, &w, &h, &comp)) return true;
    Image8 img;
    DecodeInfo info;
    std::string err;
    if (!image_decode(data, size, img, info, err)) return false;
    w = img.w;
    h = img.h;
    return true;
}

// ============================================================================ resize / convert

void image_resize_to(const Image8& in, int ow, int oh, int depth, OutImage& out)
{
    out.w = ow;
    out.h = oh;
    out.c = in.c;
    out.depth = depth;
    const size_t n = (size_t)ow * oh * in.c;
    const stbir_pixel_layout layout = in.c == 4 ? STBIR_RGBA : STBIR_RGB;
    const bool same = ow == in.w && oh == in.h;

    if (depth == 8)
    {
        out.data.resize(n);
        if (same)
            memcpy(out.data.data(), in.data.data(), n);
        else
            stbir_resize(in.data.data(), in.w, in.h, 0, out.data.data(), ow, oh, 0, layout, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_CATMULLROM);
        return;
    }

    // > 8 bit output: interpolate in float so the in-between values are kept
    std::vector<float> f(n);
    if (same)
    {
        for (size_t i = 0; i < n; i++) f[i] = in.data[i] * (1.f / 255.f);
    }
    else
    {
        std::vector<float> src((size_t)in.w * in.h * in.c);
        for (size_t i = 0; i < src.size(); i++) src[i] = in.data[i] * (1.f / 255.f);
        stbir_resize(src.data(), in.w, in.h, 0, f.data(), ow, oh, 0, layout, STBIR_TYPE_FLOAT, STBIR_EDGE_CLAMP, STBIR_FILTER_CATMULLROM);
        for (size_t i = 0; i < n; i++) f[i] = std::min(std::max(f[i], 0.f), 1.f);
    }

    if (depth == 16)
    {
        out.data.resize(n * 2);
        uint16_t* d = (uint16_t*)out.data.data();
        for (size_t i = 0; i < n; i++) d[i] = (uint16_t)lrintf(f[i] * 65535.f);
    }
    else
    {
        out.data.resize(n * 4);
        memcpy(out.data.data(), f.data(), n * 4);
    }
}

void image_alpha_clean(OutImage& img)
{
    if (img.c != 4) return;
    const size_t npx = (size_t)img.w * img.h;
    if (img.depth == 8)
    {
        unsigned char* p = img.data.data();
        for (size_t i = 0; i < npx; i++, p += 4) if (p[3] == 0) p[0] = p[1] = p[2] = 0;
    }
    else if (img.depth == 16)
    {
        uint16_t* p = (uint16_t*)img.data.data();
        for (size_t i = 0; i < npx; i++, p += 4) if (p[3] == 0) p[0] = p[1] = p[2] = 0;
    }
    else
    {
        float* p = (float*)img.data.data();
        for (size_t i = 0; i < npx; i++, p += 4) if (p[3] <= 0.f) p[0] = p[1] = p[2] = 0.f;
    }
}

// sample as float 0..1 / 8 bit / 16 bit regardless of the stored depth
static inline float px_f(const OutImage& img, size_t i)
{
    if (img.depth == 8) return img.data[i] * (1.f / 255.f);
    if (img.depth == 16) return ((const uint16_t*)img.data.data())[i] * (1.f / 65535.f);
    return ((const float*)img.data.data())[i];
}

static inline unsigned char px_8(const OutImage& img, size_t i)
{
    if (img.depth == 8) return img.data[i];
    return (unsigned char)lrintf(std::min(std::max(px_f(img, i), 0.f), 1.f) * 255.f);
}

static inline uint16_t px_16(const OutImage& img, size_t i)
{
    if (img.depth == 16) return ((const uint16_t*)img.data.data())[i];
    if (img.depth == 8) return (uint16_t)(img.data[i] * 257);
    return (uint16_t)lrintf(std::min(std::max(px_f(img, i), 0.f), 1.f) * 65535.f);
}

static std::vector<unsigned char> to_8bit(const OutImage& img, int outc)
{
    const size_t npx = (size_t)img.w * img.h;
    std::vector<unsigned char> v(npx * outc);
    for (size_t i = 0; i < npx; i++)
        for (int c = 0; c < outc; c++)
            v[i * outc + c] = c < img.c ? px_8(img, i * img.c + c) : 255;
    return v;
}

// ============================================================================ encoders

static void stbi_vec_write(void* ctx, void* data, int size)
{
    std::vector<unsigned char>* v = (std::vector<unsigned char>*)ctx;
    v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + size);
}

static bool zlib_compress_vec(const unsigned char* src, size_t len, std::vector<unsigned char>& dst, int level)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit(&zs, level) != Z_OK) return false;
    dst.clear();
    unsigned char buf[1 << 16];
    size_t pos = 0;
    int ret;
    do
    {
        size_t chunk = std::min<size_t>(len - pos, 1u << 30);
        zs.next_in = (Bytef*)(src + pos);
        zs.avail_in = (uInt)chunk;
        pos += chunk;
        const int flush = pos >= len ? Z_FINISH : Z_NO_FLUSH;
        do
        {
            zs.next_out = buf;
            zs.avail_out = sizeof(buf);
            ret = deflate(&zs, flush);
            dst.insert(dst.end(), buf, buf + (sizeof(buf) - zs.avail_out));
        } while (zs.avail_out == 0);
        if (flush == Z_FINISH) break;
    } while (true);
    deflateEnd(&zs);
    return ret == Z_STREAM_END;
}

static void png_chunk(std::vector<unsigned char>& out, const char* type, const unsigned char* data, size_t len)
{
    put_be32(out, (uint32_t)len);
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + start, (uInt)(4));
    size_t off = 0;
    while (off < len)
    {
        size_t k = std::min<size_t>(len - off, 1u << 30);
        crc = crc32(crc, data + off, (uInt)k);
        off += k;
    }
    put_be32(out, (uint32_t)crc);
}

static bool png_encode(const OutImage& img, std::vector<unsigned char>& out, std::string& err)
{
    const int bitdepth = img.depth == 16 ? 16 : 8;
    const int bpp = img.c * bitdepth / 8;
    const size_t rowbytes = (size_t)img.w * bpp;
    const size_t npx = (size_t)img.w * img.h;

    std::vector<unsigned char> raw(rowbytes * img.h);
    for (size_t i = 0; i < npx * img.c; i++)
    {
        if (bitdepth == 8) raw[i] = px_8(img, i);
        else { uint16_t v = px_16(img, i); raw[i * 2] = (unsigned char)(v >> 8); raw[i * 2 + 1] = (unsigned char)v; }
    }

    // adaptive filtering, minimum sum of absolute differences heuristic
    std::vector<unsigned char> filtered((rowbytes + 1) * img.h);
    std::vector<unsigned char> cand[5];
    for (int f = 0; f < 5; f++) cand[f].resize(rowbytes);
    for (int y = 0; y < img.h; y++)
    {
        const unsigned char* cur = raw.data() + rowbytes * y;
        const unsigned char* prev = y > 0 ? raw.data() + rowbytes * (y - 1) : 0;
        uint64_t best = UINT64_MAX;
        int bestf = 0;
        for (int f = 0; f < 5; f++)
        {
            unsigned char* o = cand[f].data();
            uint64_t sum = 0;
            for (size_t x = 0; x < rowbytes; x++)
            {
                int a = x >= (size_t)bpp ? cur[x - bpp] : 0;
                int b = prev ? prev[x] : 0;
                int c = (prev && x >= (size_t)bpp) ? prev[x - bpp] : 0;
                int pred = 0;
                if (f == 1) pred = a;
                else if (f == 2) pred = b;
                else if (f == 3) pred = (a + b) / 2;
                else if (f == 4)
                {
                    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                    pred = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
                }
                unsigned char v = (unsigned char)(cur[x] - pred);
                o[x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (sum < best) { best = sum; bestf = f; }
        }
        filtered[(rowbytes + 1) * y] = (unsigned char)bestf;
        memcpy(&filtered[(rowbytes + 1) * y + 1], cand[bestf].data(), rowbytes);
    }

    std::vector<unsigned char> z;
    if (!zlib_compress_vec(filtered.data(), filtered.size(), z, 6)) { err = "png deflate failed"; return false; }

    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.assign(sig, sig + 8);
    unsigned char ihdr[13];
    ihdr[0] = (unsigned char)(img.w >> 24); ihdr[1] = (unsigned char)(img.w >> 16); ihdr[2] = (unsigned char)(img.w >> 8); ihdr[3] = (unsigned char)img.w;
    ihdr[4] = (unsigned char)(img.h >> 24); ihdr[5] = (unsigned char)(img.h >> 16); ihdr[6] = (unsigned char)(img.h >> 8); ihdr[7] = (unsigned char)img.h;
    ihdr[8] = (unsigned char)bitdepth;
    ihdr[9] = img.c == 4 ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(out, "IHDR", ihdr, 13);
    // split IDAT in 1 MiB chunks
    for (size_t off = 0; off < z.size(); off += (1u << 20))
        png_chunk(out, "IDAT", z.data() + off, std::min<size_t>(1u << 20, z.size() - off));
    png_chunk(out, "IEND", 0, 0);
    return true;
}

static bool tiff_encode(const OutImage& img, std::vector<unsigned char>& out, std::string& err)
{
    const int bits = img.depth;
    const int bytes = bits / 8;
    const size_t rowbytes = (size_t)img.w * img.c * bytes;
    uint32_t rps = (uint32_t)std::max<size_t>(1, (1u << 20) / std::max<size_t>(rowbytes, 1));
    if (rps > (uint32_t)img.h) rps = (uint32_t)img.h;
    const uint32_t nstrips = ((uint32_t)img.h + rps - 1) / rps;

    std::vector<unsigned char> body;
    std::vector<uint32_t> offs(nstrips), cnts(nstrips);
    std::vector<unsigned char> strip, z;
    out.assign(8, 0);
    out[0] = 'I'; out[1] = 'I'; out[2] = 42; out[3] = 0;
    for (uint32_t s = 0; s < nstrips; s++)
    {
        uint32_t y0 = s * rps, y1 = std::min<uint32_t>((uint32_t)img.h, y0 + rps);
        strip.resize((size_t)(y1 - y0) * rowbytes);
        size_t base = (size_t)y0 * img.w * img.c;
        size_t cnt = (size_t)(y1 - y0) * img.w * img.c;
        if (bits == 8) for (size_t i = 0; i < cnt; i++) strip[i] = px_8(img, base + i);
        else if (bits == 16) for (size_t i = 0; i < cnt; i++) { uint16_t v = px_16(img, base + i); strip[i * 2] = (unsigned char)v; strip[i * 2 + 1] = (unsigned char)(v >> 8); }
        else for (size_t i = 0; i < cnt; i++) { float v = px_f(img, base + i); memcpy(&strip[i * 4], &v, 4); }
        if (!zlib_compress_vec(strip.data(), strip.size(), z, 6)) { err = "tiff deflate failed"; return false; }
        offs[s] = (uint32_t)out.size();
        cnts[s] = (uint32_t)z.size();
        out.insert(out.end(), z.begin(), z.end());
        if (out.size() & 1) out.push_back(0);
    }

    // out-of-line values
    auto align = [&]() { if (out.size() & 1) out.push_back(0); };
    uint32_t bps_off = 0, so_off = 0, sc_off = 0, xres_off, yres_off;
    align();
    if (img.c > 1) { bps_off = (uint32_t)out.size(); for (int c = 0; c < img.c; c++) put_le16(out, (uint16_t)bits); }
    align();
    if (nstrips > 1)
    {
        so_off = (uint32_t)out.size(); for (uint32_t s = 0; s < nstrips; s++) put_le32(out, offs[s]);
        sc_off = (uint32_t)out.size(); for (uint32_t s = 0; s < nstrips; s++) put_le32(out, cnts[s]);
    }
    xres_off = (uint32_t)out.size(); put_le32(out, 72); put_le32(out, 1);
    yres_off = (uint32_t)out.size(); put_le32(out, 72); put_le32(out, 1);
    align();

    struct Entry { uint16_t tag, type; uint32_t count, value; };
    std::vector<Entry> e;
    e.push_back({256, 4, 1, (uint32_t)img.w});
    e.push_back({257, 4, 1, (uint32_t)img.h});
    e.push_back({258, 3, (uint32_t)img.c, img.c > 1 ? bps_off : (uint32_t)bits});
    e.push_back({259, 3, 1, 8});
    e.push_back({262, 3, 1, 2});
    e.push_back({273, 4, nstrips, nstrips > 1 ? so_off : offs[0]});
    e.push_back({277, 3, 1, (uint32_t)img.c});
    e.push_back({278, 4, 1, rps});
    e.push_back({279, 4, nstrips, nstrips > 1 ? sc_off : cnts[0]});
    e.push_back({282, 5, 1, xres_off});
    e.push_back({283, 5, 1, yres_off});
    e.push_back({284, 3, 1, 1});
    e.push_back({296, 3, 1, 2});
    if (img.c == 4) e.push_back({338, 3, 1, 2});
    e.push_back({339, 3, 1, bits == 32 ? 3u : 1u});
    if (img.c == 4)
    {
        // value for SampleFormat must have count = samples per pixel
        e.back().count = 4;
        uint32_t sf_off = (uint32_t)out.size();
        for (int c = 0; c < 4; c++) put_le16(out, bits == 32 ? 3 : 1);
        e.back().value = sf_off;
    }
    else if (img.c == 3)
    {
        e.back().count = 3;
        uint32_t sf_off = (uint32_t)out.size();
        for (int c = 0; c < 3; c++) put_le16(out, bits == 32 ? 3 : 1);
        put_le16(out, 0);
        e.back().value = sf_off;
    }
    align();

    uint32_t ifd = (uint32_t)out.size();
    out[4] = (unsigned char)ifd; out[5] = (unsigned char)(ifd >> 8); out[6] = (unsigned char)(ifd >> 16); out[7] = (unsigned char)(ifd >> 24);
    put_le16(out, (uint16_t)e.size());
    for (size_t i = 0; i < e.size(); i++)
    {
        put_le16(out, e[i].tag);
        put_le16(out, e[i].type);
        put_le32(out, e[i].count);
        if (e[i].type == 3 && e[i].count == 1) { put_le16(out, (uint16_t)e[i].value); put_le16(out, 0); }
        else put_le32(out, e[i].value);
    }
    put_le32(out, 0);
    return true;
}

static inline uint16_t float_to_half(float f)
{
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (exp <= 0)
    {
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x800000;
        uint32_t t = 14 - exp;
        uint32_t h = mant >> t;
        if ((mant >> (t - 1)) & 1) h++;
        return (uint16_t)(sign | h);
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    uint32_t h = sign | ((uint32_t)exp << 10) | (mant >> 13);
    if (mant & 0x1000) h++;
    return (uint16_t)h;
}

static bool exr_encode(const OutImage& img, std::vector<unsigned char>& out, std::string& err)
{
    const bool half = img.depth != 32;
    const int vbytes = half ? 2 : 4;
    const int nc = img.c;
    // channels must be listed alphabetically
    const char* names4[4] = {"A", "B", "G", "R"};
    const int src4[4] = {3, 2, 1, 0};
    const char* names3[3] = {"B", "G", "R"};
    const int src3[3] = {2, 1, 0};
    const char** names = nc == 4 ? names4 : names3;
    const int* src = nc == 4 ? src4 : src3;

    out.clear();
    put_le32(out, 20000630);
    put_le32(out, 2);
    auto attr = [&](const char* name, const char* type, const std::vector<unsigned char>& v) {
        out.insert(out.end(), name, name + strlen(name) + 1);
        out.insert(out.end(), type, type + strlen(type) + 1);
        put_le32(out, (uint32_t)v.size());
        out.insert(out.end(), v.begin(), v.end());
    };
    std::vector<unsigned char> v;
    for (int c = 0; c < nc; c++)
    {
        v.insert(v.end(), names[c], names[c] + strlen(names[c]) + 1);
        put_le32(v, half ? 1 : 2);
        v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(0);
        put_le32(v, 1);
        put_le32(v, 1);
    }
    v.push_back(0);
    attr("channels", "chlist", v);
    v.assign(1, 3); // ZIP_COMPRESSION
    attr("compression", "compression", v);
    v.clear(); put_le32(v, 0); put_le32(v, 0); put_le32(v, (uint32_t)(img.w - 1)); put_le32(v, (uint32_t)(img.h - 1));
    attr("dataWindow", "box2i", v);
    attr("displayWindow", "box2i", v);
    v.assign(1, 0);
    attr("lineOrder", "lineOrder", v);
    float one = 1.f, zero = 0.f;
    v.assign((unsigned char*)&one, (unsigned char*)&one + 4);
    attr("pixelAspectRatio", "float", v);
    v.clear(); v.insert(v.end(), (unsigned char*)&zero, (unsigned char*)&zero + 4); v.insert(v.end(), (unsigned char*)&zero, (unsigned char*)&zero + 4);
    attr("screenWindowCenter", "v2f", v);
    v.assign((unsigned char*)&one, (unsigned char*)&one + 4);
    attr("screenWindowWidth", "float", v);
    out.push_back(0);

    const int lines_per_block = 16;
    const int nblocks = (img.h + lines_per_block - 1) / lines_per_block;
    const size_t table_pos = out.size();
    out.resize(out.size() + (size_t)nblocks * 8);

    std::vector<unsigned char> raw, tmp, z;
    for (int b = 0; b < nblocks; b++)
    {
        const int y0 = b * lines_per_block, y1 = std::min(img.h, y0 + lines_per_block);
        raw.resize((size_t)(y1 - y0) * img.w * nc * vbytes);
        size_t p = 0;
        for (int y = y0; y < y1; y++)
            for (int c = 0; c < nc; c++)
                for (int x = 0; x < img.w; x++)
                {
                    float f = px_f(img, ((size_t)y * img.w + x) * nc + src[c]);
                    if (half) { uint16_t hv = float_to_half(f); raw[p++] = (unsigned char)hv; raw[p++] = (unsigned char)(hv >> 8); }
                    else { memcpy(&raw[p], &f, 4); p += 4; }
                }

        // zip predictor: interleave then delta
        const size_t n = raw.size();
        tmp.resize(n);
        size_t t1 = 0, t2 = (n + 1) / 2;
        for (size_t i = 0; i < n; i++)
        {
            if ((i & 1) == 0) tmp[t1++] = raw[i];
            else tmp[t2++] = raw[i];
        }
        int prev = tmp[0];
        for (size_t i = 1; i < n; i++)
        {
            int d = (int)tmp[i] - prev + (128 + 256);
            prev = tmp[i];
            tmp[i] = (unsigned char)d;
        }
        if (!zlib_compress_vec(tmp.data(), n, z, 6)) { err = "exr deflate failed"; return false; }

        const uint64_t off = out.size();
        for (int k = 0; k < 8; k++) out[table_pos + (size_t)b * 8 + k] = (unsigned char)(off >> (8 * k));
        put_le32(out, (uint32_t)y0);
        if (z.size() < n)
        {
            put_le32(out, (uint32_t)z.size());
            out.insert(out.end(), z.begin(), z.end());
        }
        else
        {
            put_le32(out, (uint32_t)n);
            out.insert(out.end(), raw.begin(), raw.end());
        }
    }
    return true;
}

static bool sunraster_encode(const OutImage& img, std::vector<unsigned char>& out)
{
    const size_t rowbytes = ((size_t)img.w * 3 + 1) & ~(size_t)1;
    out.clear();
    put_be32(out, 0x59a66a95);
    put_be32(out, (uint32_t)img.w);
    put_be32(out, (uint32_t)img.h);
    put_be32(out, 24);
    put_be32(out, (uint32_t)(rowbytes * img.h));
    put_be32(out, 1);
    put_be32(out, 0);
    put_be32(out, 0);
    for (int y = 0; y < img.h; y++)
    {
        size_t start = out.size();
        for (int x = 0; x < img.w; x++)
        {
            size_t i = ((size_t)y * img.w + x) * img.c;
            out.push_back(px_8(img, i + 2));
            out.push_back(px_8(img, i + 1));
            out.push_back(px_8(img, i + 0));
        }
        while (out.size() - start < rowbytes) out.push_back(0);
    }
    return true;
}

static bool ppm_encode(const OutImage& img, std::vector<unsigned char>& out)
{
    const bool b16 = img.depth == 16;
    char hdr[64];
    int n = snprintf(hdr, sizeof(hdr), "P6\n%d %d\n%d\n", img.w, img.h, b16 ? 65535 : 255);
    out.assign(hdr, hdr + n);
    const size_t npx = (size_t)img.w * img.h;
    out.reserve(out.size() + npx * 3 * (b16 ? 2 : 1));
    for (size_t i = 0; i < npx; i++)
        for (int c = 0; c < 3; c++)
        {
            if (b16) { uint16_t v = px_16(img, i * img.c + c); out.push_back((unsigned char)(v >> 8)); out.push_back((unsigned char)v); }
            else out.push_back(px_8(img, i * img.c + c));
        }
    return true;
}

bool image_encode(const OutImage& img, const std::string& ext_in, int quality, std::vector<unsigned char>& out, std::string& err)
{
    const OutputFormatInfo* f = find_output_format(ext_in.c_str());
    if (!f) { err = "unsupported output format " + ext_in; return false; }
    const std::string ext = f->ext;
    if (f->has_quality && quality < 0) quality = f->quality_default;
    out.clear();

    if (ext == "png") return png_encode(img, out, err);
    if (ext == "tif") return tiff_encode(img, out, err);
    if (ext == "exr") return exr_encode(img, out, err);
    if (ext == "sr") return sunraster_encode(img, out);
    if (ext == "gif") return gif_encode_still(img, out, err);
    if (ext == "ppm") return ppm_encode(img, out);
    if (ext == "jp2")
    {
        if (img.depth == 32)
        {
            OutImage tmp = img;
            tmp.depth = 16;
            tmp.data.resize((size_t)img.w * img.h * img.c * 2);
            for (size_t i = 0; i < (size_t)img.w * img.h * img.c; i++) ((uint16_t*)tmp.data.data())[i] = px_16(img, i);
            return jp2_encode(tmp, out, err);
        }
        if (img.depth == 8 || img.depth == 16) return jp2_encode(img, out, err);
    }
    if (ext == "hdr")
    {
        const size_t npx = (size_t)img.w * img.h;
        std::vector<float> fl(npx * 3);
        for (size_t i = 0; i < npx; i++)
            for (int c = 0; c < 3; c++) fl[i * 3 + c] = px_f(img, i * img.c + c);
        if (!stbi_write_hdr_to_func(stbi_vec_write, &out, img.w, img.h, 3, fl.data())) { err = "hdr encode failed"; return false; }
        return true;
    }
    if (ext == "bmp")
    {
        std::vector<unsigned char> p8 = to_8bit(img, img.c);
        if (!stbi_write_bmp_to_func(stbi_vec_write, &out, img.w, img.h, img.c, p8.data())) { err = "bmp encode failed"; return false; }
        return true;
    }
    if (ext == "tga")
    {
        std::vector<unsigned char> p8 = to_8bit(img, img.c);
        stbi_write_tga_with_rle = quality > 0 ? 1 : 0;
        if (!stbi_write_tga_to_func(stbi_vec_write, &out, img.w, img.h, img.c, p8.data())) { err = "tga encode failed"; return false; }
        return true;
    }
    if (ext == "jpg")
    {
        std::vector<unsigned char> p8 = to_8bit(img, 3);
        int q = std::min(100, std::max(1, quality));
        if (!stbi_write_jpg_to_func(stbi_vec_write, &out, img.w, img.h, 3, p8.data(), q)) { err = "jpeg encode failed"; return false; }
        return true;
    }
    if (ext == "webp")
    {
        std::vector<unsigned char> p8 = to_8bit(img, img.c);
        uint8_t* wp = 0;
        size_t n;
        const int stride = img.w * img.c;
        if (quality > 100)
            n = img.c == 4 ? WebPEncodeLosslessRGBA(p8.data(), img.w, img.h, stride, &wp) : WebPEncodeLosslessRGB(p8.data(), img.w, img.h, stride, &wp);
        else
            n = img.c == 4 ? WebPEncodeRGBA(p8.data(), img.w, img.h, stride, (float)quality, &wp) : WebPEncodeRGB(p8.data(), img.w, img.h, stride, (float)quality, &wp);
        if (n == 0 || !wp) { err = "webp encode failed (webp is limited to 16383x16383)"; return false; }
        out.assign(wp, wp + n);
        WebPFree(wp);
        return true;
    }
    err = "unsupported output format " + ext;
    return false;
}
