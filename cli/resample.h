// classic (non-AI) resampling for repicturer-cli: the Non-AI Scale mode and the final resize of the AI modes
//
// filters like madVR: Nearest Neighbor, Bilinear, Bicubic Mitchell / Catmull-Rom, Lanczos and Spline36 (separable,
// two passes) and Jinc (EWA, round). Optional anti-ringing (clamps the halos of the sharp filters to the nearest source
// pixels) and scaling in linear light (downscaling only). The cpu and the gpu (vulkan, through ncnn) give the same
// result: the cpu prepares every weight, the gpu only adds them up

#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <string>

#include "image_io.h"

namespace ncnn { class VulkanDevice; class Pipeline; }

enum ResampleFilter
{
    RF_NEAREST = 0,
    RF_BILINEAR,
    RF_MITCHELL,
    RF_CATROM,
    RF_LANCZOS,
    RF_SPLINE36,
    RF_JINC,
};

enum AntiRinging
{
    AR_OFF = 0,
    AR_RELAXED = 1,
    AR_STRICT = 2,
};

// how the result is put into 8 bit: rounded, or dithered so smooth gradients do not get bands
enum Dither
{
    DITHER_OFF = 0,
    DITHER_ORDERED = 1,         // an 8x8 Bayer pattern
    DITHER_ERROR_DIFFUSION = 2, // Jarvis-Judice-Ninke, serpentine
};

struct ResampleOptions
{
    int filter = RF_MITCHELL;
    int taps = 3;            // Lanczos 2 / 3 / 4 / 8, Jinc 3 / 4 / 8; ignored by the other filters
    int antiringing = AR_OFF;
    bool linear = false;     // scale in linear light; only used when the picture gets smaller (see resample_uses_linear)
    int dither = DITHER_OFF;
};

// reduce banding artifacts of the source (level 0 off, 1 low, 2 medium, 3 high), before any scaling
void deband_image(Image8& img, int level, int threads);

// "nearest", "bilinear", "mitchell", "catrom", "lanczos", "spline36", "jinc"; -1 if unknown
int resample_filter_from_name(const std::string& s);
const char* resample_filter_name(int filter);
// taps the filter accepts (Lanczos 2/3/4/8, Jinc 3/4/8); filters without taps accept any value
bool resample_taps_valid(int filter, int taps);
// filters with negative lobes (halos) that anti-ringing can reduce
bool resample_filter_rings(int filter);
// linear light only when downscaling: no side gets bigger and at least one side gets smaller
bool resample_uses_linear(const ResampleOptions& o, int iw, int ih, int ow, int oh);

// on the cpu with `threads` threads
void resample_cpu(const Image8& in, int ow, int oh, const ResampleOptions& o, OutImage& out, int threads);

// on a vulkan gpu; one per device, shared by the threads
class GpuResampler
{
public:
    explicit GpuResampler(int gpuid);
    ~GpuResampler();
    bool ok() const { return pass && ewa; }
    // 0 = done; non-zero = the gpu failed (nothing usable in out)
    int run(const Image8& in, int ow, int oh, const ResampleOptions& o, OutImage& out) const;

private:
    ncnn::VulkanDevice* vkdev;
    ncnn::Pipeline* pass;
    ncnn::Pipeline* ewa;
};

#endif // RESAMPLE_H
