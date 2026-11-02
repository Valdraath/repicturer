// classic (non-AI) resampling, see resample.h

#include "resample.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <thread>
#include <vector>

#include "gpu.h"
#include "pipeline.h"
#include "command.h"
#include "mat.h"
#include "option.h"

#include "resample_pass.comp.hex.h"
#include "resample_ewa.comp.hex.h"

static const double PI = 3.14159265358979323846;

// ============================================================================ names

static const char* const g_filter_names[] = {"nearest", "bilinear", "mitchell", "catrom", "lanczos", "spline36", "jinc"};

int resample_filter_from_name(const std::string& s)
{
    for (int i = 0; i < (int)(sizeof(g_filter_names) / sizeof(g_filter_names[0])); i++)
        if (s == g_filter_names[i]) return i;
    return -1;
}

const char* resample_filter_name(int filter)
{
    return filter >= 0 && filter <= RF_JINC ? g_filter_names[filter] : "?";
}

bool resample_taps_valid(int filter, int taps)
{
    if (filter == RF_LANCZOS) return taps == 2 || taps == 3 || taps == 4 || taps == 8;
    if (filter == RF_JINC) return taps == 3 || taps == 4 || taps == 8;
    return true;
}

bool resample_filter_rings(int filter)
{
    return filter == RF_CATROM || filter == RF_LANCZOS || filter == RF_SPLINE36 || filter == RF_JINC;
}

bool resample_uses_linear(const ResampleOptions& o, int iw, int ih, int ow, int oh)
{
    return o.linear && ow <= iw && oh <= ih && (ow < iw || oh < ih);
}

// ============================================================================ kernels

// Bessel function of the first kind, order 1 (Numerical Recipes, about 1e-8)
static double bessel_j1(double x)
{
    const double ax = fabs(x);
    if (ax < 8.0)
    {
        const double y = x * x;
        const double a = x * (72362614232.0 + y * (-7895059235.0 + y * (242396853.1 + y * (-2972611.439 + y * (15704.48260 + y * (-30.16036606))))));
        const double b = 144725228442.0 + y * (2300535178.0 + y * (18583304.74 + y * (99447.43394 + y * (376.9991397 + y * 1.0))));
        return a / b;
    }
    const double z = 8.0 / ax, y = z * z, xx = ax - 2.356194491;
    const double a = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4 + y * (0.2457520174e-5 + y * (-0.240337019e-6))));
    const double b = 0.04687499995 + y * (-0.2002690873e-3 + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
    const double r = sqrt(0.636619772 / ax) * (cos(xx) * a - z * sin(xx) * b);
    return x < 0.0 ? -r : r;
}

static double jinc(double x)
{
    if (x < 1e-9) return 1.0;
    const double t = PI * x;
    return 2.0 * bessel_j1(t) / t;
}

// zeros of jinc: the radius of Jinc with n taps (lobes)
static const double g_jinc_zeros[8] = {1.2196698912665045, 2.2331305943815286, 3.2383154841662362, 4.2410628637960699,
                                       5.2427643768701817, 6.2439216898644877, 7.2447598687199570, 8.2453949139520427};

static double sinc(double x)
{
    if (fabs(x) < 1e-9) return 1.0;
    const double t = PI * x;
    return sin(t) / t;
}

// Mitchell-Netravali family
static double bc_cubic(double x, double B, double C)
{
    x = fabs(x);
    if (x < 1.0)
        return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6.0;
    if (x < 2.0)
        return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6.0;
    return 0.0;
}

static double spline36(double x)
{
    x = fabs(x);
    if (x < 1.0) return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
    if (x < 2.0) { x -= 1.0; return ((-6.0 / 11.0 * x + 270.0 / 209.0) * x - 156.0 / 209.0) * x; }
    if (x < 3.0) { x -= 2.0; return ((1.0 / 11.0 * x - 45.0 / 209.0) * x + 26.0 / 209.0) * x; }
    return 0.0;
}

static int clamp_taps(int filter, int taps)
{
    if (resample_taps_valid(filter, taps)) return taps;
    return 3;
}

static double kernel_radius(int filter, int taps)
{
    switch (filter)
    {
    case RF_NEAREST: return 0.5;
    case RF_BILINEAR: return 1.0;
    case RF_MITCHELL:
    case RF_CATROM: return 2.0;
    case RF_LANCZOS: return clamp_taps(filter, taps);
    case RF_SPLINE36: return 3.0;
    case RF_JINC: return g_jinc_zeros[clamp_taps(filter, taps) - 1];
    }
    return 1.0;
}

static double kernel(int filter, int taps, double x)
{
    switch (filter)
    {
    case RF_BILINEAR: x = fabs(x); return x < 1.0 ? 1.0 - x : 0.0;
    case RF_MITCHELL: return bc_cubic(x, 1.0 / 3.0, 1.0 / 3.0);
    case RF_CATROM: return bc_cubic(x, 0.0, 0.5);
    case RF_LANCZOS:
    {
        const double a = clamp_taps(filter, taps);
        return fabs(x) < a ? sinc(x) * sinc(x / a) : 0.0;
    }
    case RF_SPLINE36: return spline36(x);
    case RF_JINC:
    {
        // jinc windowed by jinc (the window's first zero at the radius), like madVR
        const double r = kernel_radius(filter, taps);
        x = fabs(x);
        return x < r ? jinc(x) * jinc(x * g_jinc_zeros[0] / r) : 0.0;
    }
    }
    return 0.0;
}

static float strength_of(int ar)
{
    return ar == AR_STRICT ? 1.0f : ar == AR_RELAXED ? 0.5f : 0.0f;
}

// ============================================================================ the taps of one axis (separable filters)

struct Axis
{
    int nin = 0;
    int nout = 0;
    int ntap = 0;
    std::vector<int> idx;   // nout * ntap source indices (clamped to the edge)
    std::vector<float> w;   // nout * ntap weights (normalized)
    std::vector<int> lobe;  // nout * 2: the main-lobe taps [first, last) for anti-ringing
};

static void make_axis(int nin, int nout, int filter, int taps, Axis& a)
{
    a.nin = nin;
    a.nout = nout;
    const double s = (double)nout / nin;
    if (filter == RF_NEAREST)
    {
        a.ntap = 1;
        a.idx.resize(nout);
        a.w.assign(nout, 1.0f);
        a.lobe.resize((size_t)nout * 2);
        for (int o = 0; o < nout; o++)
        {
            a.idx[o] = std::min(nin - 1, std::max(0, (int)floor((o + 0.5) / s)));
            a.lobe[o * 2] = 0;
            a.lobe[o * 2 + 1] = 1;
        }
        return;
    }
    // shrinking: the kernel is widened so every source pixel counts (no aliasing)
    const double fs = std::max(1.0, 1.0 / s);
    const double R = kernel_radius(filter, taps) * fs;
    a.ntap = (int)floor(2.0 * R) + 2;
    a.idx.assign((size_t)nout * a.ntap, 0);
    a.w.assign((size_t)nout * a.ntap, 0.0f);
    a.lobe.assign((size_t)nout * 2, 0);
    std::vector<double> wd(a.ntap);
    for (int o = 0; o < nout; o++)
    {
        const double c = (o + 0.5) / s - 0.5;
        const int i0 = (int)ceil(c - R);
        double sum = 0.0;
        int n = 0, l0 = -1, l1 = -1;
        for (int t = 0; t < a.ntap; t++)
        {
            const int i = i0 + t;
            const double d = (i - c) / fs;
            const double k = i - c < R ? kernel(filter, taps, d) : 0.0;
            wd[t] = k;
            sum += k;
            a.idx[(size_t)o * a.ntap + t] = std::min(nin - 1, std::max(0, i));
            if (k != 0.0) n = t + 1;
            if (fabs(d) < 1.0)
            {
                if (l0 < 0) l0 = t;
                l1 = t + 1;
            }
        }
        (void)n;
        if (l0 < 0)
        {
            // no tap within one pixel (cannot happen with these kernels): the nearest one
            int best = 0;
            for (int t = 1; t < a.ntap; t++)
                if (fabs(i0 + t - c) < fabs(i0 + best - c)) best = t;
            l0 = best;
            l1 = best + 1;
        }
        if (fabs(sum) < 1e-12) sum = 1.0;
        for (int t = 0; t < a.ntap; t++) a.w[(size_t)o * a.ntap + t] = (float)(wd[t] / sum);
        a.lobe[o * 2] = l0;
        a.lobe[o * 2 + 1] = l1;
    }
}

// ============================================================================ Jinc (EWA) parameters

struct Ewa
{
    float isx = 1, isy = 1;   // source / output
    float fsx = 1, fsy = 1;   // support scale
    float radius = 1;
    int lutn = 1024;
    float lutscale = 1;
    std::vector<float> lut;   // lutn + 1 values, kernel by squared distance
};

static void make_ewa(int iw, int ih, int ow, int oh, int taps, Ewa& e)
{
    e.isx = (float)((double)iw / ow);
    e.isy = (float)((double)ih / oh);
    e.fsx = (float)std::max(1.0, (double)iw / ow);
    e.fsy = (float)std::max(1.0, (double)ih / oh);
    e.radius = (float)kernel_radius(RF_JINC, taps);
    e.lutn = 1024;
    e.lutscale = (float)(e.lutn / ((double)e.radius * e.radius));
    e.lut.resize(e.lutn + 1);
    for (int i = 0; i <= e.lutn; i++)
    {
        const double d = sqrt((double)i / e.lutn) * e.radius;
        e.lut[i] = (float)kernel(RF_JINC, taps, d);
    }
}

// the source rows an output stripe needs
static void ewa_rows(const Ewa& e, int ih, int oy0, int oy1, int& iy0, int& iy1)
{
    // one row of margin on each side: the gpu works the reach out again per pixel in its own float maths, and a value
    // right on a row boundary may round the other way there (a read outside the stripe). The extra rows get no weight
    const float c0 = (oy0 + 0.5f) * e.isy - 0.5f, c1 = (oy1 - 1 + 0.5f) * e.isy - 0.5f;
    iy0 = std::max(0, std::min(ih - 1, (int)ceil(c0 - e.radius * e.fsy) - 1));
    iy1 = std::max(0, std::min(ih - 1, (int)floor(c1 + e.radius * e.fsy) + 1)) + 1;
}

// ============================================================================ 8 bit <-> float (rgba, premultiplied)

static float srgb_to_linear(double v)
{
    return (float)(v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
}

struct ColorTables
{
    float to_lin[256];
    float thresholds[255]; // linear value halfway between two 8 bit codes
    ColorTables()
    {
        for (int i = 0; i < 256; i++) to_lin[i] = srgb_to_linear(i / 255.0);
        for (int i = 0; i < 255; i++) thresholds[i] = srgb_to_linear((i + 0.5) / 255.0);
    }
};
static const ColorTables& tables()
{
    static ColorTables t;
    return t;
}

static void rows_to_float(const Image8& in, int y0, int y1, bool linear, std::vector<float>& dst)
{
    const ColorTables& t = tables();
    const int c = in.c;
    dst.resize((size_t)in.w * (y1 - y0) * 4);
    float* d = dst.data();
    for (int y = y0; y < y1; y++)
    {
        const unsigned char* s = in.data.data() + (size_t)y * in.w * c;
        for (int x = 0; x < in.w; x++, s += c, d += 4)
        {
            const float a = c == 4 ? s[3] / 255.0f : 1.0f;
            for (int k = 0; k < 3; k++)
                d[k] = (linear ? t.to_lin[s[k]] : s[k] / 255.0f) * a;
            d[3] = a;
        }
    }
}

static inline unsigned char to_byte(float v)
{
    const int i = (int)(v * 255.0f + 0.5f);
    return (unsigned char)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static inline unsigned char lin_to_byte(float v)
{
    const float* th = tables().thresholds;
    return (unsigned char)(std::upper_bound(th, th + 255, v) - th);
}

// linear light -> the stored (gamma) value 0..1, for dithering (a table: pow() for every value was slow)
struct LinearToSrgb
{
    static const int N = 65536;
    std::vector<float> t;
    LinearToSrgb() : t(N + 1)
    {
        for (int i = 0; i <= N; i++)
        {
            const double v = (double)i / N;
            t[i] = (float)(v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055);
        }
    }
    float operator()(float v) const
    {
        const float f = v * N;
        const int i = std::min(N - 1, std::max(0, (int)f));
        return t[i] + (t[i + 1] - t[i]) * (f - (float)i);
    }
};
static const LinearToSrgb& to_srgb()
{
    static LinearToSrgb t;
    return t;
}

static const unsigned char g_bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26, 12, 44,  4, 36, 14, 46,  6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41, 51, 19, 59, 27, 49, 17, 57, 25, 15, 47,  7, 39, 13, 45,  5, 37, 63, 31, 55, 23, 61, 29, 53, 21};

// error diffusion (Jarvis-Judice-Ninke, serpentine): the rounding error of a value is spread over the 12 values
// after it. One channel; rows must come in order (the error is carried over to the next rows)
class ErrorDiffusion
{
public:
    explicit ErrorDiffusion(int w) : w(w), e0(w + 4, 0.0f), e1(w + 4, 0.0f), e2(w + 4, 0.0f) {}
    // v: values 0..255 of one row (changed in place to the rounded value); skip[x]: no error to or from x
    void row(float* v, const unsigned char* skip, int y)
    {
        const bool rtl = y & 1;
        for (int i = 0; i < w; i++)
        {
            const int x = rtl ? w - 1 - i : i;
            if (skip && skip[x]) { v[x] = 0; continue; }
            const float val = v[x] + e0[x + 2];
            const float q = std::min(255.0f, std::max(0.0f, floorf(val + 0.5f)));
            const float e = (val - q) / 48.0f;
            v[x] = q;
            const int d = rtl ? -1 : 1;
            e0[x + 2 + d] += e * 7; e0[x + 2 + 2 * d] += e * 5;
            for (int k = -2; k <= 2; k++)
            {
                static const float r1[5] = {3, 5, 7, 5, 3}, r2[5] = {1, 3, 5, 3, 1};
                e1[x + 2 + k] += e * r1[k + 2];
                e2[x + 2 + k] += e * r2[k + 2];
            }
        }
        e0.swap(e1);
        e1.swap(e2);
        std::fill(e2.begin(), e2.end(), 0.0f);
    }

private:
    int w;
    std::vector<float> e0, e1, e2;
};

// the 8 bit output of the float rows, rounded or dithered; the rows of a picture come stripe by stripe, in order
class Quantizer
{
public:
    Quantizer(int w, int c, bool linear, int dither) : w(w), c(c), linear(linear), dither(dither)
    {
        if (dither == DITHER_ERROR_DIFFUSION)
            for (int k = 0; k < 3; k++) ed.emplace_back(w);
    }
    void rows(const float* src, int y0, int y1, OutImage& out)
    {
        std::vector<float> ch[3];
        std::vector<unsigned char> skip;
        if (dither == DITHER_ERROR_DIFFUSION) { for (auto& v : ch) v.resize(w); skip.resize(w); }
        for (int y = y0; y < y1; y++)
        {
            unsigned char* d = out.data.data() + (size_t)y * w * c;
            const float* s = src + (size_t)(y - y0) * w * 4;
            for (int x = 0; x < w; x++)
            {
                const float* px = s + (size_t)x * 4;
                unsigned char* o = d + (size_t)x * c;
                const float a = std::min(1.0f, std::max(0.0f, px[3]));
                const float inv = c == 4 ? (a > 0.0f ? 1.0f / a : 0.0f) : 1.0f;
                for (int k = 0; k < 3; k++)
                {
                    const float v = std::min(1.0f, std::max(0.0f, px[k] * inv));
                    if (dither == DITHER_OFF)
                        o[k] = linear ? lin_to_byte(v) : to_byte(v);
                    else
                    {
                        const float g = (linear ? to_srgb()(v) : v) * 255.0f;
                        if (dither == DITHER_ORDERED)
                        {
                            // a value exactly on a level stays on it; between two levels the pattern picks one
                            const float t = (g_bayer8[(y & 7) * 8 + (x & 7)] + 0.5f) / 64.0f;
                            o[k] = (unsigned char)std::min(255.0f, std::max(0.0f, floorf(g + t)));
                        }
                        else
                            ch[k][x] = g;
                    }
                }
                if (c == 4) o[3] = to_byte(a);
                if (dither == DITHER_ERROR_DIFFUSION) skip[x] = c == 4 && a <= 0.0f;
            }
            if (dither == DITHER_ERROR_DIFFUSION)
                for (int k = 0; k < 3; k++)
                {
                    ed[k].row(ch[k].data(), c == 4 ? skip.data() : nullptr, y);
                    for (int x = 0; x < w; x++) d[(size_t)x * c + k] = (unsigned char)ch[k][x];
                }
        }
    }

private:
    int w, c;
    bool linear;
    int dither;
    std::vector<ErrorDiffusion> ed;
};

// ============================================================================ the plan shared by the cpu and the gpu

struct ResamplePlan
{
    int iw = 0, ih = 0, ow = 0, oh = 0;
    bool ewa = false;
    bool linear = false;
    float strength = 0;
    Axis ax, ay;          // separable
    bool doX = false, doY = false;
    Ewa e;                // jinc
};

static void make_plan(const Image8& in, int ow, int oh, const ResampleOptions& o, ResamplePlan& p)
{
    p.iw = in.w;
    p.ih = in.h;
    p.ow = ow;
    p.oh = oh;
    p.linear = resample_uses_linear(o, in.w, in.h, ow, oh);
    p.strength = resample_filter_rings(o.filter) ? strength_of(o.antiringing) : 0.0f;
    p.ewa = o.filter == RF_JINC;
    if (p.ewa)
        make_ewa(in.w, in.h, ow, oh, o.taps, p.e);
    else
    {
        p.doX = ow != in.w;
        p.doY = oh != in.h;
        if (p.doX) make_axis(in.w, ow, o.filter, o.taps, p.ax);
        if (p.doY) make_axis(in.h, oh, o.filter, o.taps, p.ay);
    }
}

// source rows [iy0, iy1) an output stripe [oy0, oy1) needs
static void stripe_rows(const ResamplePlan& p, int oy0, int oy1, int& iy0, int& iy1)
{
    if (p.ewa) { ewa_rows(p.e, p.ih, oy0, oy1, iy0, iy1); return; }
    if (!p.doY) { iy0 = oy0; iy1 = oy1; return; }
    iy0 = p.ih;
    iy1 = 0;
    for (int y = oy0; y < oy1; y++)
        for (int t = 0; t < p.ay.ntap; t++)
        {
            const int i = p.ay.idx[(size_t)y * p.ay.ntap + t];
            iy0 = std::min(iy0, i);
            iy1 = std::max(iy1, i + 1);
        }
}

// output rows per stripe so that no float buffer gets bigger than `budget` bytes
static int stripe_height(const ResamplePlan& p, size_t budget)
{
    const size_t px = budget / 16;
    const size_t wmax = (size_t)std::max(p.iw, p.ow);
    int rows = p.oh;
    while (rows > 1)
    {
        int iy0, iy1;
        stripe_rows(p, 0, rows, iy0, iy1);
        // the middle of the picture needs the most source rows for the same output rows
        int my0, my1;
        const int mid = std::max(0, std::min(p.oh - rows, p.oh / 2 - rows / 2));
        stripe_rows(p, mid, mid + rows, my0, my1);
        const size_t in_rows = (size_t)std::max(iy1 - iy0, my1 - my0);
        if (in_rows * wmax <= px && (size_t)rows * p.ow <= px) break;
        rows = (rows + 1) / 2;
    }
    return rows;
}

// ============================================================================ cpu

template<typename F>
static void parallel_rows(int n, int threads, F fn)
{
    threads = std::max(1, std::min(threads, n / 4 + 1));
    if (threads == 1) { fn(0, n); return; }
    std::vector<std::thread> th;
    for (int k = 0; k < threads; k++)
    {
        const int a = (int)((long long)n * k / threads), b = (int)((long long)n * (k + 1) / threads);
        th.emplace_back([=]() { fn(a, b); });
    }
    for (auto& t : th) t.join();
}

static inline void ar_clamp(float* v, const float* lo, const float* hi, float strength)
{
    for (int k = 0; k < 4; k++)
    {
        const float c = std::min(hi[k], std::max(lo[k], v[k]));
        v[k] = v[k] + (c - v[k]) * strength;
    }
}

// one separable pass: along x (dst row = src row) or along y (src rows start at `base`)
static void cpu_pass(const Axis& a, int axis, const float* src, int srcw, float* dst, int outw, int outh, int coord0, int base,
                     float strength, int threads)
{
    parallel_rows(outh, threads, [&](int r0, int r1) {
        for (int gy = r0; gy < r1; gy++)
            for (int gx = 0; gx < outw; gx++)
            {
                const int k = axis == 0 ? gx : gy + coord0;
                const size_t t0 = (size_t)k * a.ntap;
                float sum[4] = {0, 0, 0, 0};
                for (int t = 0; t < a.ntap; t++)
                {
                    const int i = a.idx[t0 + t];
                    const float* s = axis == 0 ? src + ((size_t)gy * srcw + i) * 4 : src + ((size_t)(i - base) * srcw + gx) * 4;
                    const float w = a.w[t0 + t];
                    for (int c = 0; c < 4; c++) sum[c] += w * s[c];
                }
                if (strength > 0.0f)
                {
                    float lo[4] = {3e38f, 3e38f, 3e38f, 3e38f}, hi[4] = {-3e38f, -3e38f, -3e38f, -3e38f};
                    for (int t = a.lobe[k * 2]; t < a.lobe[k * 2 + 1]; t++)
                    {
                        const int i = a.idx[t0 + t];
                        const float* s = axis == 0 ? src + ((size_t)gy * srcw + i) * 4 : src + ((size_t)(i - base) * srcw + gx) * 4;
                        for (int c = 0; c < 4; c++) { lo[c] = std::min(lo[c], s[c]); hi[c] = std::max(hi[c], s[c]); }
                    }
                    ar_clamp(sum, lo, hi, strength);
                }
                memcpy(dst + ((size_t)gy * outw + gx) * 4, sum, sizeof(sum));
            }
    });
}

static void cpu_ewa(const Ewa& e, const float* src, int srcw, int srch, float* dst, int outw, int outh, int oy0, int base,
                    int rows, float strength, int threads)
{
    parallel_rows(outh, threads, [&](int r0, int r1) {
        const float r2 = e.radius * e.radius;
        for (int gy = r0; gy < r1; gy++)
            for (int gx = 0; gx < outw; gx++)
            {
                const float cx = ((float)gx + 0.5f) * e.isx - 0.5f;
                const float cy = ((float)(gy + oy0) + 0.5f) * e.isy - 0.5f;
                const int x0 = (int)ceil(cx - e.radius * e.fsx), x1 = (int)floor(cx + e.radius * e.fsx);
                const int y0 = (int)ceil(cy - e.radius * e.fsy), y1 = (int)floor(cy + e.radius * e.fsy);
                float sum[4] = {0, 0, 0, 0}, wsum = 0;
                float lo[4] = {3e38f, 3e38f, 3e38f, 3e38f}, hi[4] = {-3e38f, -3e38f, -3e38f, -3e38f};
                for (int iy = y0; iy <= y1; iy++)
                {
                    const float dy = ((float)iy - cy) / e.fsy;
                    const int ry = std::min(rows - 1, std::max(0, std::min(srch - 1, std::max(0, iy)) - base));
                    const float* row = src + (size_t)ry * srcw * 4;
                    for (int ix = x0; ix <= x1; ix++)
                    {
                        const float dx = ((float)ix - cx) / e.fsx;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 >= r2) continue;
                        const float f = d2 * e.lutscale;
                        const int li = std::min((int)f, e.lutn - 1);
                        const float w = e.lut[li] + (e.lut[li + 1] - e.lut[li]) * (f - (float)li);
                        const float* s = row + (size_t)std::min(srcw - 1, std::max(0, ix)) * 4;
                        for (int c = 0; c < 4; c++) sum[c] += w * s[c];
                        wsum += w;
                        if (fabsf(dx) < 1.0f && fabsf(dy) < 1.0f)
                            for (int c = 0; c < 4; c++) { lo[c] = std::min(lo[c], s[c]); hi[c] = std::max(hi[c], s[c]); }
                    }
                }
                for (int c = 0; c < 4; c++) sum[c] /= wsum;
                if (strength > 0.0f && lo[0] <= hi[0]) ar_clamp(sum, lo, hi, strength);
                memcpy(dst + ((size_t)gy * outw + gx) * 4, sum, sizeof(sum));
            }
    });
}

static bool same_size_copy(const Image8& in, int ow, int oh, OutImage& out)
{
    out.w = ow;
    out.h = oh;
    out.c = in.c;
    if (ow != in.w || oh != in.h) return false;
    out.data = in.data;
    return true;
}

void resample_cpu(const Image8& in, int ow, int oh, const ResampleOptions& o, OutImage& out, int threads)
{
    if (same_size_copy(in, ow, oh, out)) return;
    out.data.assign((size_t)ow * oh * in.c, 0);
    ResamplePlan p;
    make_plan(in, ow, oh, o, p);
    const int rows = stripe_height(p, (size_t)64 << 20); // at most 3 float buffers of 64 MB (counted by the RAM limit)
    Quantizer q(ow, out.c, p.linear, o.dither);
    std::vector<float> src, tmp, dst;
    for (int oy0 = 0; oy0 < oh; oy0 += rows)
    {
        const int oy1 = std::min(oh, oy0 + rows);
        int iy0, iy1;
        stripe_rows(p, oy0, oy1, iy0, iy1);
        rows_to_float(in, iy0, iy1, p.linear, src);
        const float* res;
        if (p.ewa)
        {
            dst.resize((size_t)ow * (oy1 - oy0) * 4);
            cpu_ewa(p.e, src.data(), in.w, in.h, dst.data(), ow, oy1 - oy0, oy0, iy0, iy1 - iy0, p.strength, threads);
            res = dst.data();
        }
        else
        {
            const float* cur = src.data();
            if (p.doX)
            {
                tmp.resize((size_t)ow * (iy1 - iy0) * 4);
                cpu_pass(p.ax, 0, cur, in.w, tmp.data(), ow, iy1 - iy0, 0, 0, p.strength, threads);
                cur = tmp.data();
            }
            if (p.doY)
            {
                dst.resize((size_t)ow * (oy1 - oy0) * 4);
                cpu_pass(p.ay, 1, cur, ow, dst.data(), ow, oy1 - oy0, oy0, iy0, p.strength, threads);
                cur = dst.data();
            }
            res = cur;
        }
        q.rows(res, oy0, oy1, out);
    }
}

// ============================================================================ deband (reduce banding artifacts)

// min / max of every window of 2r+1 values along a line (a monotonic deque: O(n))
static void line_minmax(const unsigned char* src, size_t stride, int n, int r, unsigned char* mn, unsigned char* mx, size_t ostride)
{
    std::vector<int> qa(n), qb(n);
    int ha = 0, ta = 0, hb = 0, tb = 0;
    int next = 0;
    for (int i = 0; i < n; i++)
    {
        const int hi = std::min(n - 1, i + r);
        while (next <= hi)
        {
            const unsigned char v = src[next * stride];
            while (ta > ha && src[qa[ta - 1] * stride] >= v) ta--;
            qa[ta++] = next;
            while (tb > hb && src[qb[tb - 1] * stride] <= v) tb--;
            qb[tb++] = next;
            next++;
        }
        while (qa[ha] < i - r) ha++;
        while (qb[hb] < i - r) hb++;
        mn[i * ostride] = src[qa[ha] * stride];
        mx[i * ostride] = src[qb[hb] * stride];
    }
}

// in flat areas only (the colours within the window differ by at most a few levels: a banded sky, shadow, soft
// background) every pixel becomes the average of its window, which turns the steps into a smooth ramp. Edges, lines
// and texture have a larger range and are left as they are. The smooth result is put back into 8 bit with error
// diffusion: plain rounding would make the same bands again. Transparency is not changed
// one scale of deband: blends the window average (radius r) into acc where the window spans at most T levels
static void deband_pass(const Image8& img, int r, float T, int threads, std::vector<float> acc[3])
{
    const int w = img.w, h = img.h, c = img.c;
    const size_t n = (size_t)w * h;

    // the largest colour range of the three channels around every pixel
    std::vector<unsigned char> range(n, 0), chan(n), rmin(n), rmax(n), cmin(n), cmax(n);
    for (int k = 0; k < 3; k++)
    {
        for (size_t i = 0; i < n; i++) chan[i] = img.data[i * c + k];
        parallel_rows(h, threads, [&](int a, int b) {
            for (int y = a; y < b; y++)
                line_minmax(&chan[(size_t)y * w], 1, w, r, &rmin[(size_t)y * w], &rmax[(size_t)y * w], 1);
        });
        parallel_rows(w, threads, [&](int a, int b) {
            for (int x = a; x < b; x++)
            {
                line_minmax(&rmin[x], w, h, r, &cmin[x], &cmax[x], w);
                std::vector<unsigned char> t1(h), t2(h);
                line_minmax(&rmax[x], w, h, r, t1.data(), t2.data(), 1);
                for (int y = 0; y < h; y++) cmax[(size_t)y * w + x] = t2[y];
            }
        });
        for (size_t i = 0; i < n; i++) range[i] = std::max<unsigned char>(range[i], (unsigned char)(cmax[i] - cmin[i]));
    }

    // per channel: the window average (box sums along rows, then columns), blended in by how flat the area is
    std::vector<float> rowsum(n);
    for (int k = 0; k < 3; k++)
    {
        parallel_rows(h, threads, [&](int a, int b) {
            for (int y = a; y < b; y++)
            {
                const unsigned char* s = &img.data[(size_t)y * w * c + k];
                float sum = 0;
                int lo = 0, hi = -1;
                for (int x = 0; x < w; x++)
                {
                    const int nlo = std::max(0, x - r), nhi = std::min(w - 1, x + r);
                    while (hi < nhi) sum += s[(size_t)(++hi) * c];
                    while (lo < nlo) sum -= s[(size_t)(lo++) * c];
                    rowsum[(size_t)y * w + x] = sum / (float)(nhi - nlo + 1);
                }
            }
        });
        parallel_rows(w, threads, [&](int a, int b) {
            for (int x = a; x < b; x++)
            {
                float sum = 0;
                int lo = 0, hi = -1;
                for (int y = 0; y < h; y++)
                {
                    const int nlo = std::max(0, y - r), nhi = std::min(h - 1, y + r);
                    while (hi < nhi) sum += rowsum[(size_t)(++hi) * w + x];
                    while (lo < nlo) sum -= rowsum[(size_t)(lo++) * w + x];
                    const size_t i = (size_t)y * w + x;
                    const float mean = sum / (float)(nhi - nlo + 1);
                    // fully smoothed up to T levels of range, not at all from 2T
                    const float f = std::min(1.0f, std::max(0.0f, (2.0f * T - (float)range[i]) / T));
                    acc[k][i] += (mean - acc[k][i]) * f;
                }
            }
        });
    }
}

// several scales: narrow bands are smoothed by the small windows, wide bands (a sky with steps of 50-100 pixels) by
// the large ones, which only act where the area is flat over their whole width
void deband_image(Image8& img, int level, int threads)
{
    if (level <= 0 || img.w < 2 || img.h < 2) return;
    struct Scale { int r; float T; };
    static const Scale scales[3] = {{8, 2.0f}, {24, 3.0f}, {64, 4.0f}};
    const int passes = std::min(3, level); // low: 1 scale, medium: 2, high: 3
    const int w = img.w, h = img.h, c = img.c;
    const size_t n = (size_t)w * h;
    std::vector<float> acc[3];
    for (int k = 0; k < 3; k++)
    {
        acc[k].resize(n);
        for (size_t i = 0; i < n; i++) acc[k][i] = img.data[i * c + k];
    }
    for (int s = 0; s < passes; s++) deband_pass(img, scales[s].r, scales[s].T, threads, acc);
    // back to 8 bit with error diffusion (rounding would make the bands again)
    for (int k = 0; k < 3; k++)
    {
        ErrorDiffusion e(w);
        std::vector<float> row(w);
        for (int y = 0; y < h; y++)
        {
            memcpy(row.data(), &acc[k][(size_t)y * w], sizeof(float) * w);
            e.row(row.data(), nullptr, y);
            for (int x = 0; x < w; x++) img.data[((size_t)y * w + x) * c + k] = (unsigned char)row[x];
        }
    }
}

// ============================================================================ gpu

static ncnn::Option gpu_option()
{
    ncnn::Option opt;
    opt.use_vulkan_compute = true;
    opt.use_fp16_packed = false;
    opt.use_fp16_storage = false;
    opt.use_fp16_arithmetic = false;
    opt.use_int8_packed = false;
    opt.use_int8_storage = false;
    opt.use_int8_arithmetic = false;
    opt.use_bf16_storage = false;
    opt.use_packing_layout = false;
    return opt;
}

static ncnn::Pipeline* make_pipeline(ncnn::VulkanDevice* vkdev, const char* data, int size)
{
    std::vector<uint32_t> spirv;
    if (ncnn::compile_spirv_module(data, size, gpu_option(), spirv) != 0 || spirv.empty()) return 0;
    ncnn::Pipeline* pl = new ncnn::Pipeline(vkdev);
    pl->set_optimal_local_size_xyz(8, 8, 1);
    std::vector<ncnn::vk_specialization_type> spec;
    if (pl->create(spirv.data(), spirv.size() * 4, spec) != 0)
    {
        delete pl;
        return 0;
    }
    return pl;
}

GpuResampler::GpuResampler(int gpuid)
{
    vkdev = ncnn::get_gpu_device(gpuid);
    pass = vkdev ? make_pipeline(vkdev, resample_pass_comp_data, sizeof(resample_pass_comp_data)) : 0;
    ewa = vkdev ? make_pipeline(vkdev, resample_ewa_comp_data, sizeof(resample_ewa_comp_data)) : 0;
}

GpuResampler::~GpuResampler()
{
    delete pass;
    delete ewa;
}

// upload a cpu array as a gpu buffer (4 byte elements); false when out of memory
template<typename T>
static bool upload(ncnn::VkCompute& cmd, const std::vector<T>& v, ncnn::VkMat& dst, const ncnn::Option& opt)
{
    static_assert(sizeof(T) == 4, "4 byte elements");
    ncnn::Mat m((int)v.size(), (void*)v.data(), (size_t)4u);
    cmd.record_clone(m, dst, opt);
    return !dst.empty();
}

int GpuResampler::run(const Image8& in, int ow, int oh, const ResampleOptions& o, OutImage& out) const
{
    if (!ok()) return -1;
    if (same_size_copy(in, ow, oh, out)) return 0;
    out.data.assign((size_t)ow * oh * in.c, 0);
    ResamplePlan p;
    make_plan(in, ow, oh, o, p);

    // every buffer below the size a shader may address (larger ones are silently cut short on some gpus) and small
    // enough for the memory limit
    const size_t range = vkdev->info.physicalDeviceProperties().limits.maxStorageBufferRange;
    const int rows = stripe_height(p, std::min(range / 2, (size_t)128 << 20));
    Quantizer q(ow, out.c, p.linear, o.dither);

    ncnn::Option opt = gpu_option();
    ncnn::VkAllocator* blob = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging = vkdev->acquire_staging_allocator();
    opt.blob_vkallocator = blob;
    opt.workspace_vkallocator = blob;
    opt.staging_vkallocator = staging;

    int ret = 0;
    {
        // the tables stay on the gpu for every stripe
        ncnn::VkMat xi, xw, xl, yi, yw, yl, lut;
        {
            ncnn::VkCompute cmd(vkdev);
            bool okc = true;
            if (p.ewa) okc = upload(cmd, p.e.lut, lut, opt);
            if (p.doX) okc = okc && upload(cmd, p.ax.idx, xi, opt) && upload(cmd, p.ax.w, xw, opt) && upload(cmd, p.ax.lobe, xl, opt);
            if (p.doY) okc = okc && upload(cmd, p.ay.idx, yi, opt) && upload(cmd, p.ay.w, yw, opt) && upload(cmd, p.ay.lobe, yl, opt);
            if (!okc || cmd.submit_and_wait() != 0) ret = -1;
        }

        std::vector<float> src;
        for (int oy0 = 0; oy0 < oh && ret == 0; oy0 += rows)
        {
            const int oy1 = std::min(oh, oy0 + rows), orows = oy1 - oy0;
            int iy0, iy1;
            stripe_rows(p, oy0, oy1, iy0, iy1);
            const int irows = iy1 - iy0;
            rows_to_float(in, iy0, iy1, p.linear, src);

            ncnn::VkCompute cmd(vkdev);
            ncnn::VkMat src_gpu;
            if (!upload(cmd, src, src_gpu, opt)) { ret = -1; break; }

            ncnn::VkMat cur = src_gpu;
            int curw = in.w;
            auto record_pass = [&](const Axis& a, int axis, const ncnn::VkMat& idx, const ncnn::VkMat& w, const ncnn::VkMat& lobe,
                                   int outh, int coord0, int base) -> bool {
                ncnn::VkMat dst;
                dst.create(ow * outh * 4, (size_t)4u, blob);
                if (dst.empty()) return false;
                std::vector<ncnn::VkMat> bindings(5);
                bindings[0] = cur;
                bindings[1] = dst;
                bindings[2] = idx;
                bindings[3] = w;
                bindings[4] = lobe;
                std::vector<ncnn::vk_constant_type> c(8);
                c[0].i = curw;
                c[1].i = ow;
                c[2].i = outh;
                c[3].i = a.ntap;
                c[4].i = axis;
                c[5].i = coord0;
                c[6].i = base;
                c[7].f = p.strength;
                ncnn::VkMat dispatcher;
                dispatcher.w = ow;
                dispatcher.h = outh;
                dispatcher.c = 1;
                cmd.record_pipeline(pass, bindings, c, dispatcher);
                cur = dst;
                curw = ow;
                return true;
            };

            if (p.ewa)
            {
                ncnn::VkMat dst;
                dst.create(ow * orows * 4, (size_t)4u, blob);
                if (dst.empty()) { ret = -1; break; }
                std::vector<ncnn::VkMat> bindings(3);
                bindings[0] = src_gpu;
                bindings[1] = dst;
                bindings[2] = lut;
                std::vector<ncnn::vk_constant_type> c(15);
                c[0].i = in.w;
                c[1].i = in.h;
                c[2].i = ow;
                c[3].i = orows;
                c[4].i = oy0;
                c[5].i = iy0;
                c[6].i = irows;
                c[7].i = p.e.lutn;
                c[8].f = p.e.isx;
                c[9].f = p.e.isy;
                c[10].f = p.e.fsx;
                c[11].f = p.e.fsy;
                c[12].f = p.e.radius;
                c[13].f = p.e.lutscale;
                c[14].f = p.strength;
                ncnn::VkMat dispatcher;
                dispatcher.w = ow;
                dispatcher.h = orows;
                dispatcher.c = 1;
                cmd.record_pipeline(ewa, bindings, c, dispatcher);
                cur = dst;
            }
            else
            {
                if (p.doX && !record_pass(p.ax, 0, xi, xw, xl, irows, 0, 0)) { ret = -1; break; }
                if (p.doY && !record_pass(p.ay, 1, yi, yw, yl, orows, oy0, iy0)) { ret = -1; break; }
            }

            ncnn::Mat res;
            cmd.record_clone(cur, res, opt);
            if (cmd.submit_and_wait() != 0 || res.empty() || (size_t)res.w < (size_t)ow * orows * 4) { ret = -1; break; }
            q.rows((const float*)res.data, oy0, oy1, out);
        }
    }

    blob->clear();
    staging->clear();
    vkdev->reclaim_blob_allocator(blob);
    vkdev->reclaim_staging_allocator(staging);
    return ret;
}
