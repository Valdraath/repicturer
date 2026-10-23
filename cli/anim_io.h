// animated gif / webp: decode every frame, encode frames into an animated gif / webp

#ifndef ANIM_IO_H
#define ANIM_IO_H

#include <string>
#include <vector>
#include <memory>
#include "image_io.h"

struct AnimFrames
{
    int w = 0;
    int h = 0;
    bool alpha = false;               // any frame has transparency (frames are rgba, otherwise rgb)
    std::vector<Image8> frames;       // fully composited canvases
    std::vector<int> durations_ms;    // display time of every frame
    int loop_count = 0;               // 0 = forever, n = n times, -1 = gif without loop extension (play once)
    std::string kind;                 // "gif" or "webp"
};

// true when the file is a gif with more than one frame or an animated webp
bool anim_is_animated(const unsigned char* data, size_t size);

// decode all frames; false (with err) on failure
bool anim_decode(const unsigned char* data, size_t size, AnimFrames& out, std::string& err);

// streaming encoder: frames are added one by one (so big animations do not stay in memory upscaled)
class AnimEncoder
{
public:
    virtual ~AnimEncoder() {}
    virtual bool add(const OutImage& frame, int duration_ms, std::string& err) = 0;
    virtual bool finish(std::vector<unsigned char>& out, std::string& err) = 0;
};

// ext "gif" or "webp"; quality: webp 1-100 lossy, > 100 lossless, < 0 default
std::unique_ptr<AnimEncoder> anim_create_encoder(const std::string& ext, int w, int h, int quality, int loop_count, std::string& err);

// single frame gif (still output format)
bool gif_encode_still(const OutImage& img, std::vector<unsigned char>& out, std::string& err);

#endif // ANIM_IO_H
