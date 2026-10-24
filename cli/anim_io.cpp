// animated gif / webp support for repicturer-cli (enhanced waifu2x-ncnn-vulkan)
//
// decode : gif via stb_image (composited frames, delays), animated webp via libwebp WebPAnimDecoder
// encode : animated webp via libwebp WebPAnimEncoder (lossy 1-100 / lossless 101)
//          gif with an own encoder: median cut palette per frame (up to 256 colours, one kept for
//          transparency), lzw, per frame delay, NETSCAPE loop extension

#include "anim_io.h"

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <algorithm>

#include "stb_image.h"

#include "webp/decode.h"
#include "webp/encode.h"
#include "webp/demux.h"
#include "webp/mux.h"

#include "../common/formats.h"

// ============================================================================ detection / decoding

static bool is_gif(const unsigned char* d, size_t n) { return n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0); }
static bool is_webp(const unsigned char* d, size_t n) { return n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0; }

// counts image descriptors of a gif without decoding the pixels (stops at 2)
static int gif_count_frames(const unsigned char* d, size_t n)
{
    if (!is_gif(d, n) || n < 13) return 0;
    size_t p = 13;
    if (d[10] & 0x80) p += 3u * (1u << ((d[10] & 7) + 1)); // global colour table
    int frames = 0;
    while (p < n)
    {
        const unsigned char b = d[p++];
        if (b == 0x3B) break; // trailer
        if (b == 0x21)
        {
            // extension: label, then sub-blocks
            if (p >= n) break;
            p++;
            while (p < n && d[p] != 0) p += d[p] + 1u;
            p++;
        }
        else if (b == 0x2C)
        {
            if (p + 9 > n) break;
            const unsigned char flags = d[p + 8];
            p += 9;
            if (flags & 0x80) p += 3u * (1u << ((flags & 7) + 1)); // local colour table
            p++; // lzw minimum code size
            while (p < n && d[p] != 0) p += d[p] + 1u;
            p++;
            if (++frames >= 2) return frames;
        }
        else
            break;
    }
    return frames;
}

// NETSCAPE2.0 loop count, -1 when the gif has no loop extension
static int gif_loop_count(const unsigned char* d, size_t n)
{
    static const char sig[] = "NETSCAPE2.0";
    for (size_t i = 0; i + 16 < n; i++)
        if (memcmp(d + i, sig, 11) == 0 && d[i + 11] == 3 && d[i + 12] == 1)
            return d[i + 13] | (d[i + 14] << 8);
    return -1;
}

bool anim_is_animated(const unsigned char* data, size_t size)
{
    if (is_gif(data, size)) return gif_count_frames(data, size) >= 2;
    if (is_webp(data, size))
    {
        WebPBitstreamFeatures f;
        return WebPGetFeatures(data, size, &f) == VP8_STATUS_OK && f.has_animation;
    }
    return false;
}

// rgba frames -> rgb when nothing is transparent (the 3 channel path is faster)
static void finish_frames(AnimFrames& a, const std::vector<std::vector<unsigned char> >& rgba)
{
    a.alpha = false;
    for (const auto& f : rgba)
    {
        for (size_t i = 3; i < f.size() && !a.alpha; i += 4)
            if (f[i] != 255) a.alpha = true;
        if (a.alpha) break;
    }
    const size_t npx = (size_t)a.w * a.h;
    a.frames.resize(rgba.size());
    for (size_t k = 0; k < rgba.size(); k++)
    {
        Image8& im = a.frames[k];
        im.w = a.w;
        im.h = a.h;
        im.c = a.alpha ? 4 : 3;
        if (a.alpha)
            im.data = rgba[k];
        else
        {
            im.data.resize(npx * 3);
            for (size_t i = 0; i < npx; i++)
            {
                im.data[i * 3 + 0] = rgba[k][i * 4 + 0];
                im.data[i * 3 + 1] = rgba[k][i * 4 + 1];
                im.data[i * 3 + 2] = rgba[k][i * 4 + 2];
            }
        }
    }
}

bool anim_decode(const unsigned char* data, size_t size, AnimFrames& out, std::string& err)
{
    out = AnimFrames();
    std::vector<std::vector<unsigned char> > rgba;

    if (is_gif(data, size))
    {
        int* delays = nullptr;
        int w = 0, h = 0, z = 0, comp = 0;
        unsigned char* px = stbi_load_gif_from_memory(data, (int)size, &delays, &w, &h, &z, &comp, 4);
        if (!px || z < 1)
        {
            err = "gif decode failed";
            if (delays) stbi_image_free(delays);
            return false;
        }
        out.w = w;
        out.h = h;
        out.kind = "gif";
        out.loop_count = gif_loop_count(data, size);
        const size_t fsz = (size_t)w * h * 4;
        rgba.resize(z);
        for (int k = 0; k < z; k++)
        {
            rgba[k].assign(px + fsz * k, px + fsz * (k + 1));
            // browsers show 0 / 10 ms gif frames for 100 ms; keep what people see
            int d = delays ? delays[k] : 100;
            out.durations_ms.push_back(d <= 10 ? 100 : d);
        }
        stbi_image_free(px);
        if (delays) stbi_image_free(delays);
        finish_frames(out, rgba);
        return true;
    }

    if (is_webp(data, size))
    {
        WebPData wd = {data, size};
        WebPAnimDecoderOptions opt;
        if (!WebPAnimDecoderOptionsInit(&opt)) { err = "webp animation decoder init failed"; return false; }
        opt.color_mode = MODE_RGBA;
        opt.use_threads = 0;
        WebPAnimDecoder* dec = WebPAnimDecoderNew(&wd, &opt);
        if (!dec) { err = "animated webp decode failed"; return false; }
        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(dec, &info))
        {
            WebPAnimDecoderDelete(dec);
            err = "animated webp header error";
            return false;
        }
        out.w = (int)info.canvas_width;
        out.h = (int)info.canvas_height;
        out.kind = "webp";
        out.loop_count = (int)info.loop_count;
        const size_t fsz = (size_t)out.w * out.h * 4;
        int prev = 0;
        while (WebPAnimDecoderHasMoreFrames(dec))
        {
            uint8_t* buf = nullptr;
            int ts = 0;
            if (!WebPAnimDecoderGetNext(dec, &buf, &ts))
            {
                WebPAnimDecoderDelete(dec);
                err = "animated webp frame decode failed";
                return false;
            }
            rgba.emplace_back(buf, buf + fsz);
            out.durations_ms.push_back(std::max(1, ts - prev));
            prev = ts;
        }
        WebPAnimDecoderDelete(dec);
        if (rgba.empty()) { err = "animated webp has no frames"; return false; }
        finish_frames(out, rgba);
        return true;
    }

    err = "not an animated gif / webp";
    return false;
}

// ============================================================================ gif encoder

namespace {

struct Palette
{
    int count = 0;              // colours used (without the transparent one)
    int transparent = -1;       // index of the transparent entry or -1
    unsigned char rgb[256][3];
};

// median cut on a 6 bit per channel histogram; representative = mean of the real colours in a box
static void build_palette(const OutImage& img, Palette& pal, std::vector<int>& lut)
{
    const size_t npx = (size_t)img.w * img.h;
    const int c = img.c;
    bool transparent = false;
    std::vector<uint32_t> cnt(1 << 18, 0);
    std::vector<uint64_t> sr(1 << 18, 0), sg(1 << 18, 0), sb(1 << 18, 0);
    const unsigned char* p = img.data.data();
    for (size_t i = 0; i < npx; i++, p += c)
    {
        if (c == 4 && p[3] < 128) { transparent = true; continue; }
        const int bin = ((p[0] >> 2) << 12) | ((p[1] >> 2) << 6) | (p[2] >> 2);
        cnt[bin]++;
        sr[bin] += p[0];
        sg[bin] += p[1];
        sb[bin] += p[2];
    }

    std::vector<int> bins;
    for (int i = 0; i < (1 << 18); i++)
        if (cnt[i]) bins.push_back(i);

    const int maxColours = transparent ? 255 : 256;

    struct Box { int begin, end; };
    std::vector<Box> boxes;
    if (!bins.empty()) boxes.push_back({0, (int)bins.size()});

    auto chan = [](int bin, int axis) { return axis == 0 ? (bin >> 12) & 63 : axis == 1 ? (bin >> 6) & 63 : bin & 63; };

    while ((int)boxes.size() < maxColours)
    {
        // split the box with the largest range * pixel count
        int best = -1, bestAxis = 0;
        double bestScore = 0;
        for (int b = 0; b < (int)boxes.size(); b++)
        {
            if (boxes[b].end - boxes[b].begin < 2) continue;
            int lo[3] = {63, 63, 63}, hi[3] = {0, 0, 0};
            uint64_t n = 0;
            for (int k = boxes[b].begin; k < boxes[b].end; k++)
            {
                for (int a = 0; a < 3; a++)
                {
                    int v = chan(bins[k], a);
                    lo[a] = std::min(lo[a], v);
                    hi[a] = std::max(hi[a], v);
                }
                n += cnt[bins[k]];
            }
            int axis = 0;
            for (int a = 1; a < 3; a++)
                if (hi[a] - lo[a] > hi[axis] - lo[axis]) axis = a;
            const double score = (double)(hi[axis] - lo[axis]) * (double)n;
            if (score > bestScore) { bestScore = score; best = b; bestAxis = axis; }
        }
        if (best < 0) break;

        Box bx = boxes[best];
        std::sort(bins.begin() + bx.begin, bins.begin() + bx.end, [&](int a, int b) { return chan(a, bestAxis) < chan(b, bestAxis); });
        uint64_t total = 0;
        for (int k = bx.begin; k < bx.end; k++) total += cnt[bins[k]];
        uint64_t acc = 0;
        int mid = bx.begin + 1;
        for (int k = bx.begin; k < bx.end - 1; k++)
        {
            acc += cnt[bins[k]];
            mid = k + 1;
            if (acc * 2 >= total) break;
        }
        boxes[best] = {bx.begin, mid};
        boxes.push_back({mid, bx.end});
    }

    pal.count = (int)boxes.size();
    lut.assign(1 << 18, -1);
    for (int b = 0; b < pal.count; b++)
    {
        uint64_t n = 0, r = 0, g = 0, bl = 0;
        for (int k = boxes[b].begin; k < boxes[b].end; k++)
        {
            const int bin = bins[k];
            n += cnt[bin];
            r += sr[bin];
            g += sg[bin];
            bl += sb[bin];
            lut[bin] = b;
        }
        pal.rgb[b][0] = (unsigned char)(n ? (r + n / 2) / n : 0);
        pal.rgb[b][1] = (unsigned char)(n ? (g + n / 2) / n : 0);
        pal.rgb[b][2] = (unsigned char)(n ? (bl + n / 2) / n : 0);
    }
    if (pal.count == 0)
    {
        pal.count = 1;
        pal.rgb[0][0] = pal.rgb[0][1] = pal.rgb[0][2] = 0;
    }
    pal.transparent = transparent ? pal.count : -1;
}

// nearest palette entry for a pixel (bins that were not in the histogram do not occur)
static inline int map_pixel(const unsigned char* p, const std::vector<int>& lut)
{
    const int bin = ((p[0] >> 2) << 12) | ((p[1] >> 2) << 6) | (p[2] >> 2);
    return lut[bin] < 0 ? 0 : lut[bin];
}

class LzwWriter
{
public:
    LzwWriter(std::vector<unsigned char>& o) : out(o) {}

    void encode(const std::vector<unsigned char>& idx, int minCodeSize)
    {
        out.push_back((unsigned char)minCodeSize);
        const int clear = 1 << minCodeSize;
        const int eoi = clear + 1;
        int codeSize = minCodeSize + 1;
        int next = eoi + 1;
        reset_table();
        put(clear, codeSize);
        if (idx.empty())
        {
            put(eoi, codeSize);
            flush_bits();
            out.push_back(0);
            return;
        }
        int prefix = idx[0];
        for (size_t i = 1; i < idx.size(); i++)
        {
            const int k = idx[i];
            const int key = (prefix << 8) | k;
            int code = lookup(key);
            if (code >= 0)
            {
                prefix = code;
                continue;
            }
            put(prefix, codeSize);
            if (next < 4095) // like giflib: clear before code 4095 is used
            {
                insert(key, next);
                if (next == (1 << codeSize) && codeSize < 12) codeSize++;
                next++;
            }
            else
            {
                put(clear, codeSize);
                reset_table();
                codeSize = minCodeSize + 1;
                next = eoi + 1;
            }
            prefix = k;
        }
        put(prefix, codeSize);
        // the decoder grows the code size one code later than the encoder table: keep in step
        if (next == (1 << codeSize) && codeSize < 12) codeSize++;
        put(eoi, codeSize);
        flush_bits();
        out.push_back(0); // block terminator
    }

private:
    static const int HSIZE = 5003;
    std::vector<unsigned char>& out;
    int keys[HSIZE];
    short codes[HSIZE];
    unsigned char block[256];
    int blockLen = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    void reset_table() { for (int i = 0; i < HSIZE; i++) keys[i] = -1; }
    int lookup(int key) const
    {
        int h = (int)(((unsigned)key * 2654435761u) % HSIZE);
        while (keys[h] != -1)
        {
            if (keys[h] == key) return codes[h];
            if (++h == HSIZE) h = 0;
        }
        return -1;
    }
    void insert(int key, int code)
    {
        int h = (int)(((unsigned)key * 2654435761u) % HSIZE);
        while (keys[h] != -1) if (++h == HSIZE) h = 0;
        keys[h] = key;
        codes[h] = (short)code;
    }
    void put(int code, int size)
    {
        bitBuf |= (uint32_t)code << bitCnt;
        bitCnt += size;
        while (bitCnt >= 8)
        {
            byte((unsigned char)(bitBuf & 0xFF));
            bitBuf >>= 8;
            bitCnt -= 8;
        }
    }
    void byte(unsigned char b)
    {
        block[blockLen++] = b;
        if (blockLen == 255) emit_block();
    }
    void emit_block()
    {
        if (!blockLen) return;
        out.push_back((unsigned char)blockLen);
        out.insert(out.end(), block, block + blockLen);
        blockLen = 0;
    }
    void flush_bits()
    {
        if (bitCnt > 0) byte((unsigned char)(bitBuf & 0xFF));
        bitBuf = 0;
        bitCnt = 0;
        emit_block();
    }
};

static void put16(std::vector<unsigned char>& v, int x) { v.push_back((unsigned char)(x & 0xFF)); v.push_back((unsigned char)((x >> 8) & 0xFF)); }

class GifEncoder : public AnimEncoder
{
public:
    GifEncoder(int w, int h, int loop) : W(w), H(h)
    {
        out.insert(out.end(), {'G', 'I', 'F', '8', '9', 'a'});
        put16(out, W);
        put16(out, H);
        out.push_back(0x00); // no global colour table (every frame has its own palette)
        out.push_back(0);
        out.push_back(0);
        if (loop >= 0)
        {
            const unsigned char ext[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01};
            out.insert(out.end(), ext, ext + sizeof(ext));
            put16(out, std::min(loop, 65535));
            out.push_back(0);
        }
    }

    bool add(const OutImage& frame, int duration_ms, std::string& err) override
    {
        if ((frame.c != 3 && frame.c != 4) || frame.w != W || frame.h != H)
        {
            err = "gif frame format mismatch";
            return false;
        }
        Palette pal;
        std::vector<int> lut;
        build_palette(frame, pal, lut);
        const int used = pal.count + (pal.transparent >= 0 ? 1 : 0);
        int bits = 1;
        while ((1 << bits) < used) bits++;

        // graphic control extension: delay, disposal, transparency
        const int disposal = pal.transparent >= 0 ? 2 : 1; // transparent frames must not show the previous frame
        out.push_back(0x21);
        out.push_back(0xF9);
        out.push_back(4);
        out.push_back((unsigned char)((disposal << 2) | (pal.transparent >= 0 ? 1 : 0)));
        put16(out, std::max(0, (duration_ms + 5) / 10));
        out.push_back((unsigned char)(pal.transparent >= 0 ? pal.transparent : 0));
        out.push_back(0);

        // image descriptor + local colour table
        out.push_back(0x2C);
        put16(out, 0);
        put16(out, 0);
        put16(out, W);
        put16(out, H);
        out.push_back((unsigned char)(0x80 | (bits - 1)));
        for (int i = 0; i < (1 << bits); i++)
        {
            if (i < pal.count) out.insert(out.end(), pal.rgb[i], pal.rgb[i] + 3);
            else { out.push_back(0); out.push_back(0); out.push_back(0); }
        }

        const size_t npx = (size_t)W * H;
        std::vector<unsigned char> idx(npx);
        const unsigned char* p = frame.data.data();
        for (size_t i = 0; i < npx; i++, p += frame.c)
        {
            if (frame.c == 4 && p[3] < 128) idx[i] = (unsigned char)pal.transparent;
            else idx[i] = (unsigned char)map_pixel(p, lut);
        }
        LzwWriter lzw(out);
        lzw.encode(idx, std::max(2, bits));
        return true;
    }

    bool finish(std::vector<unsigned char>& o, std::string&) override
    {
        out.push_back(0x3B);
        o.swap(out);
        return true;
    }

private:
    int W, H;
    std::vector<unsigned char> out;
};

// ============================================================================ animated webp encoder

class WebPAnimEnc : public AnimEncoder
{
public:
    WebPAnimEnc() {}
    ~WebPAnimEnc() override { if (enc) WebPAnimEncoderDelete(enc); }

    bool init(int w, int h, int quality, int loop, std::string& err)
    {
        if (w > 16383 || h > 16383)
        {
            err = "webp is limited to 16383x16383 pixels";
            return false;
        }
        W = w;
        H = h;
        q = quality < 0 ? 100 : quality;
        WebPAnimEncoderOptions opt;
        if (!WebPAnimEncoderOptionsInit(&opt)) { err = "webp animation encoder init failed"; return false; }
        opt.anim_params.loop_count = loop < 0 ? 1 : loop; // gif without loop extension plays once
        opt.allow_mixed = 0;
        enc = WebPAnimEncoderNew(w, h, &opt);
        if (!enc) { err = "webp animation encoder init failed"; return false; }
        return true;
    }

    bool add(const OutImage& frame, int duration_ms, std::string& err) override
    {
        WebPConfig cfg;
        if (!WebPConfigInit(&cfg)) { err = "webp config failed"; return false; }
        if (q > 100)
        {
            cfg.lossless = 1;
            cfg.quality = 75;
            cfg.exact = 0;
        }
        else
            cfg.quality = (float)q;
        WebPPicture pic;
        if (!WebPPictureInit(&pic)) { err = "webp picture init failed"; return false; }
        pic.use_argb = 1;
        pic.width = W;
        pic.height = H;
        const int ok = frame.c == 4 ? WebPPictureImportRGBA(&pic, frame.data.data(), W * 4) : WebPPictureImportRGB(&pic, frame.data.data(), W * 3);
        if (!ok || !WebPAnimEncoderAdd(enc, &pic, ts, &cfg))
        {
            WebPPictureFree(&pic);
            err = std::string("webp frame encode failed: ") + (enc ? WebPAnimEncoderGetError(enc) : "");
            return false;
        }
        WebPPictureFree(&pic);
        ts += std::max(1, duration_ms);
        return true;
    }

    bool finish(std::vector<unsigned char>& out, std::string& err) override
    {
        WebPData data;
        WebPDataInit(&data);
        if (!WebPAnimEncoderAdd(enc, nullptr, ts, nullptr) || !WebPAnimEncoderAssemble(enc, &data))
        {
            err = std::string("webp animation assemble failed: ") + WebPAnimEncoderGetError(enc);
            WebPDataClear(&data);
            return false;
        }
        out.assign(data.bytes, data.bytes + data.size);
        WebPDataClear(&data);
        return true;
    }

private:
    WebPAnimEncoder* enc = nullptr;
    int W = 0, H = 0, q = 100, ts = 0;
};

} // namespace

std::unique_ptr<AnimEncoder> anim_create_encoder(const std::string& ext, int w, int h, int quality, int loop_count, std::string& err)
{
    if (ext == "gif")
    {
        if (w > 65535 || h > 65535)
        {
            err = "gif is limited to 65535x65535 pixels";
            return nullptr;
        }
        return std::unique_ptr<AnimEncoder>(new GifEncoder(w, h, loop_count));
    }
    if (ext == "webp")
    {
        std::unique_ptr<WebPAnimEnc> e(new WebPAnimEnc());
        if (!e->init(w, h, quality, loop_count, err)) return nullptr;
        return std::unique_ptr<AnimEncoder>(e.release());
    }
    err = "animation is not supported by ." + ext;
    return nullptr;
}

bool gif_encode_still(const OutImage& img, std::vector<unsigned char>& out, std::string& err)
{
    std::unique_ptr<AnimEncoder> e = anim_create_encoder("gif", img.w, img.h, -1, -1, err);
    if (!e) return false;
    return e->add(img, 0, err) && e->finish(out, err);
}
