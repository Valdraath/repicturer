// image decode / resize / encode for repicturer-cli (enhanced waifu2x-ncnn-vulkan)

#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <string>
#include <vector>

// 8 bit interleaved RGB or RGBA image, the format ncnn waifu2x works on
struct Image8
{
    int w = 0;
    int h = 0;
    int c = 0; // 3 or 4
    std::vector<unsigned char> data;
};

// output pixel buffer, depth 8 (uint8), 16 (uint16 native endian) or 32 (float 0..1)
struct OutImage
{
    int w = 0;
    int h = 0;
    int c = 0;
    int depth = 8;
    std::vector<unsigned char> data;
};

struct DecodeInfo
{
    bool is_jpeg = false;   // used by auto_scale mode (caffe denoises jpeg sources only)
    int src_channels = 0;
    int src_depth = 8;
};

// decode any supported image file content into 8 bit RGB/RGBA
bool image_decode(const unsigned char* data, size_t size, Image8& out, DecodeInfo& info, std::string& err);

// read only the dimensions (used by the gui for the split size list)
bool image_probe_size(const unsigned char* data, size_t size, int& w, int& h);

// resize (or just convert when the size matches) to the requested output depth
// filter follows caffe: cubic, area-like prefilter when shrinking a lot
void image_resize_to(const Image8& in, int ow, int oh, int depth, OutImage& out);

// remove colour from fully transparent pixels (caffe AlphaCleanImage)
void image_alpha_clean(OutImage& img);

// encode; quality < 0 selects the format default
bool image_encode(const OutImage& img, const std::string& ext, int quality, std::vector<unsigned char>& out, std::string& err);

#endif // IMAGE_IO_H
