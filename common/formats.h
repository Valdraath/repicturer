// output format table shared by the cli and the gui
// mirrors waifu2x-caffe stImage::OutputExtentionList (ext, depth list, quality range/default)

#ifndef W2X_FORMATS_H
#define W2X_FORMATS_H

#include <string.h>

struct OutputFormatInfo
{
    const char* ext;      // without dot, lower case
    int depths[3];        // supported bit depths (0 terminated)
    int ndepth;
    bool has_quality;
    int quality_min;
    int quality_max;
    int quality_default;
    bool alpha;           // container keeps the alpha channel
};

// webp: 1..100 lossy like caffe, 101 = lossless (OpenCV semantics, quality > 100 means lossless)
// tga : 0 = no RLE, 1 = RLE (caffe semantics)
static const OutputFormatInfo g_output_formats[] = {
    {"png",  {8, 16, 0},  2, false, 0, 0, 0,     true},
    {"bmp",  {8, 0, 0},   1, false, 0, 0, 0,     true},
    {"jpg",  {8, 0, 0},   1, true,  0, 100, 100, false},
    {"jp2",  {8, 16, 0},  2, false, 0, 0, 0,     true},
    {"sr",   {8, 0, 0},   1, false, 0, 0, 0,     false},
    {"tif",  {8, 16, 32}, 3, false, 0, 0, 0,     true},
    {"hdr",  {8, 16, 32}, 3, false, 0, 0, 0,     false},
    {"exr",  {8, 16, 32}, 3, false, 0, 0, 0,     true},
    {"ppm",  {8, 16, 0},  2, false, 0, 0, 0,     false},
    {"webp", {8, 0, 0},   1, true,  1, 101, 101, true},
    {"tga",  {8, 0, 0},   1, true,  0, 1, 0,     true},
    {"gif",  {8, 0, 0},   1, false, 0, 0, 0,     true},  // also keeps animations (animated gif / webp input)
};

static const int g_output_format_count = (int)(sizeof(g_output_formats) / sizeof(g_output_formats[0]));

// accepts "png", ".PNG", "jpeg", "tiff" ...
static inline const OutputFormatInfo* find_output_format(const char* ext)
{
    if (!ext) return 0;
    if (ext[0] == '.') ext++;
    char lower[16];
    size_t n = strlen(ext);
    if (n == 0 || n >= sizeof(lower)) return 0;
    for (size_t i = 0; i <= n; i++)
    {
        char c = ext[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    if (strcmp(lower, "jpeg") == 0 || strcmp(lower, "jpe") == 0) strcpy(lower, "jpg");
    if (strcmp(lower, "tiff") == 0) strcpy(lower, "tif");
    if (strcmp(lower, "ras") == 0) strcpy(lower, "sr");
    if (strcmp(lower, "pbm") == 0 || strcmp(lower, "pgm") == 0 || strcmp(lower, "pnm") == 0) strcpy(lower, "ppm");
    if (strcmp(lower, "j2k") == 0 || strcmp(lower, "jpf") == 0) strcpy(lower, "jp2");
    for (int i = 0; i < g_output_format_count; i++)
        if (strcmp(g_output_formats[i].ext, lower) == 0)
            return &g_output_formats[i];
    return 0;
}

static inline bool format_supports_depth(const OutputFormatInfo* f, int depth)
{
    for (int i = 0; i < f->ndepth; i++)
        if (f->depths[i] == depth) return true;
    return false;
}

// input extensions the cli can decode
static const char* const g_input_extensions[] = {
    "png", "jpg", "jpeg", "jpe", "jfif", "bmp", "dib", "tga", "tif", "tiff", "gif", "psd", "hdr", "pic",
    "ppm", "pgm", "pbm", "pnm", "webp", "jp2", "j2k", "jpf",
};

#endif // W2X_FORMATS_H
