// realesrgan implemented with ncnn library
//
// changes for RePicturer:
//  - shaders compiled at run time by ncnn (no vulkan sdk / glslangValidator needed at build time)
//  - rgb pixel order on every platform
//  - cpu mode (upstream real-esrgan-ncnn-vulkan is gpu only), used as fallback when no vulkan gpu exists

#include "realesrgan.h"
#include "net_load.h"

#include <string.h>
#include <algorithm>
#include <vector>

#include "realesrgan_preproc.comp.hex.h"
#include "realesrgan_postproc.comp.hex.h"

RealESRGAN::RealESRGAN(int gpuid, int num_threads)
{
    vkdev = gpuid == -1 ? 0 : ncnn::get_gpu_device(gpuid);

    net.opt.num_threads = num_threads;

    realesrgan_preproc = 0;
    realesrgan_postproc = 0;
    bicubic_2x = 0;
    bicubic_3x = 0;
    bicubic_4x = 0;
}

RealESRGAN::~RealESRGAN()
{
    // cleanup preprocess and postprocess pipeline
    {
        delete realesrgan_preproc;
        delete realesrgan_postproc;
    }

    ncnn::Layer* layers[3] = {bicubic_2x, bicubic_3x, bicubic_4x};
    for (int i = 0; i < 3; i++)
    {
        if (layers[i])
        {
            layers[i]->destroy_pipeline(net.opt);
            delete layers[i];
        }
    }
}

#if _WIN32
int RealESRGAN::load(const std::wstring& parampath, const std::wstring& modelpath)
#else
int RealESRGAN::load(const std::string& parampath, const std::string& modelpath)
#endif
{
    net.opt.use_vulkan_compute = vkdev ? true : false;
    net.opt.use_fp16_packed = true;
    net.opt.use_fp16_storage = vkdev ? true : false;
    net.opt.use_fp16_arithmetic = false;
    net.opt.use_int8_storage = true;

    if (vkdev)
        net.set_vulkan_device(vkdev);

    if (load_ncnn_net(net, parampath, modelpath) != 0)
        return -1;

    // initialize preprocess and postprocess pipeline
    if (vkdev)
    {
        std::vector<ncnn::vk_specialization_type> specializations(1);
        specializations[0].i = 0;

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    compile_spirv_module(realesrgan_preproc_comp_data, sizeof(realesrgan_preproc_comp_data), net.opt, spirv);
                }
            }

            realesrgan_preproc = new ncnn::Pipeline(vkdev);
            realesrgan_preproc->set_optimal_local_size_xyz(32, 32, 3);
            realesrgan_preproc->create(spirv.data(), spirv.size() * 4, specializations);
        }

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    compile_spirv_module(realesrgan_postproc_comp_data, sizeof(realesrgan_postproc_comp_data), net.opt, spirv);
                }
            }

            realesrgan_postproc = new ncnn::Pipeline(vkdev);
            realesrgan_postproc->set_optimal_local_size_xyz(32, 32, 3);
            realesrgan_postproc->create(spirv.data(), spirv.size() * 4, specializations);
        }
    }

    // bicubic 2x/3x/4x for alpha channel
    ncnn::Layer** layers[3] = {&bicubic_2x, &bicubic_3x, &bicubic_4x};
    for (int i = 0; i < 3; i++)
    {
        ncnn::Layer* l = ncnn::create_layer("Interp");
        l->vkdev = vkdev;

        ncnn::ParamDict pd;
        pd.set(0, 3); // bicubic
        pd.set(1, (float)(i + 2));
        pd.set(2, (float)(i + 2));
        l->load_param(pd);

        l->create_pipeline(net.opt);
        *layers[i] = l;
    }

    return 0;
}

// cpu path: same tiling as the gpu path; the network keeps the size, so the padded border is cropped afterwards
int RealESRGAN::process_cpu(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::Option opt = net.opt;

    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const ncnn::Layer* bicubic = scale == 2 ? bicubic_2x : scale == 3 ? bicubic_3x : bicubic_4x;

    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        const int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        const int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding, h);

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            const int in_tile_x0 = std::max(xi * TILE_SIZE_X - prepadding, 0);
            const int in_tile_x1 = std::min((xi + 1) * TILE_SIZE_X + prepadding, w);

            // crop tile + alpha without padding
            ncnn::Mat in, in_nopad;
            if (channels == 3)
                in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGB, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
            else
            {
                in_nopad = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, xi * TILE_SIZE_X, yi * TILE_SIZE_Y, tile_w_nopad, tile_h_nopad);
                in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
            }

            ncnn::Mat in_tile(in.w, in.h, 3);
            for (int q = 0; q < 3; q++)
            {
                const float* ptr = in.channel(q);
                float* outptr = in_tile.channel(q);
                for (int i = 0; i < in.w * in.h; i++)
                    *outptr++ = *ptr++ * (1 / 255.f);
            }

            // replicate the image border where the tile touches it so every tile has the full padding
            const int pad_top = prepadding - (yi * TILE_SIZE_Y - in_tile_y0);
            const int pad_left = prepadding - (xi * TILE_SIZE_X - in_tile_x0);
            const int pad_bottom = prepadding - (in_tile_y1 - std::min((yi + 1) * TILE_SIZE_Y, h));
            const int pad_right = prepadding - (in_tile_x1 - std::min((xi + 1) * TILE_SIZE_X, w));
            {
                ncnn::Mat padded;
                ncnn::copy_make_border(in_tile, padded, pad_top, pad_bottom, pad_left, pad_right, ncnn::BORDER_REPLICATE, 0.f, net.opt);
                in_tile = padded;
            }

            ncnn::Mat acc;
            {
                ncnn::Extractor ex = net.create_extractor();
                ex.input("data", in_tile);
                ex.extract("output", acc);
            }

            ncnn::Mat out_alpha_tile;
            if (channels == 4)
            {
                ncnn::Mat in_alpha_tile = in_nopad.channel_range(3, 1).clone();
                bicubic->forward(in_alpha_tile, out_alpha_tile, opt);
            }

            // crop the padded border and write
            ncnn::Mat out(tile_w_nopad * scale, tile_h_nopad * scale, channels);
            const float norm = 255.f;
            for (int q = 0; q < 3; q++)
            {
                float* outptr = out.channel(q);
                const ncnn::Mat a = acc.channel(q);
                for (int i = 0; i < out.h; i++)
                {
                    const float* ptr = a.row(i + prepadding * scale) + prepadding * scale;
                    for (int j = 0; j < out.w; j++)
                    {
                        float v = *ptr++ * norm + 0.5f;
                        *outptr++ = std::min(std::max(v, 0.f), 255.f);
                    }
                }
            }
            if (channels == 4)
                memcpy(out.channel_range(3, 1), out_alpha_tile, out_alpha_tile.total() * sizeof(float));

            unsigned char* dst = (unsigned char*)outimage.data + (size_t)yi * scale * TILE_SIZE_Y * w * scale * channels + (size_t)xi * scale * TILE_SIZE_X * channels;
            out.to_pixels(dst, channels == 3 ? ncnn::Mat::PIXEL_RGB : ncnn::Mat::PIXEL_RGBA, w * scale * channels);
        }
    }

    return 0;
}

int RealESRGAN::process(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    // gpu out-of-memory / device errors used to be ignored and gave black or partial images
    int gpu_err = 0;
    if (!vkdev)
        return process_cpu(inimage, outimage);

    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

    ncnn::Option opt = net.opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;

    // each tile 100x100
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;

    //#pragma omp parallel for num_threads(2)
    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding, h);

        ncnn::Mat in;
        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            in = ncnn::Mat(w, (in_tile_y1 - in_tile_y0), (unsigned char*)pixeldata + in_tile_y0 * w * channels, (size_t)channels, 1);
        }
        else
        {
            if (channels == 3)
            {
                in = ncnn::Mat::from_pixels(pixeldata + in_tile_y0 * w * channels, ncnn::Mat::PIXEL_RGB, w, (in_tile_y1 - in_tile_y0));
            }
            if (channels == 4)
            {
                in = ncnn::Mat::from_pixels(pixeldata + in_tile_y0 * w * channels, ncnn::Mat::PIXEL_RGBA, w, (in_tile_y1 - in_tile_y0));
            }
        }

        ncnn::VkCompute cmd(vkdev);

        // upload
        ncnn::VkMat in_gpu;
        {
            cmd.record_clone(in, in_gpu, opt);

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) gpu_err = -1;
                cmd.reset();
            }
        }

        int out_tile_y0 = std::max(yi * TILE_SIZE_Y, 0);
        int out_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h);

        ncnn::VkMat out_gpu;
        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, (size_t)channels, 1, blob_vkallocator);
        }
        else
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, channels, (size_t)4u, 1, blob_vkallocator);
        }

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            {
                // preproc
                ncnn::VkMat in_tile_gpu;
                ncnn::VkMat in_alpha_tile_gpu;
                {
                    // crop tile
                    int tile_x0 = xi * TILE_SIZE_X - prepadding;
                    int tile_x1 = std::min((xi + 1) * TILE_SIZE_X, w) + prepadding;
                    int tile_y0 = yi * TILE_SIZE_Y - prepadding;
                    int tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h) + prepadding;

                    in_tile_gpu.create(tile_x1 - tile_x0, tile_y1 - tile_y0, 3, in_out_tile_elemsize, 1, blob_vkallocator);

                    if (channels == 4)
                    {
                        in_alpha_tile_gpu.create(tile_w_nopad, tile_h_nopad, 1, in_out_tile_elemsize, 1, blob_vkallocator);
                    }

                    std::vector<ncnn::VkMat> bindings(3);
                    bindings[0] = in_gpu;
                    bindings[1] = in_tile_gpu;
                    bindings[2] = in_alpha_tile_gpu;

                    std::vector<ncnn::vk_constant_type> constants(13);
                    constants[0].i = in_gpu.w;
                    constants[1].i = in_gpu.h;
                    constants[2].i = in_gpu.cstep;
                    constants[3].i = in_tile_gpu.w;
                    constants[4].i = in_tile_gpu.h;
                    constants[5].i = in_tile_gpu.cstep;
                    constants[6].i = prepadding;
                    constants[7].i = prepadding;
                    constants[8].i = xi * TILE_SIZE_X;
                    constants[9].i = std::min(yi * TILE_SIZE_Y, prepadding);
                    constants[10].i = channels;
                    constants[11].i = in_alpha_tile_gpu.w;
                    constants[12].i = in_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = in_tile_gpu.w;
                    dispatcher.h = in_tile_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realesrgan_preproc, bindings, constants, dispatcher);
                }

                // realesrgan
                ncnn::VkMat out_tile_gpu;
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.set_blob_vkallocator(blob_vkallocator);
                    ex.set_workspace_vkallocator(blob_vkallocator);
                    ex.set_staging_vkallocator(staging_vkallocator);

                    ex.input("data", in_tile_gpu);

                    if (ex.extract("output", out_tile_gpu, cmd) != 0) gpu_err = -1;
                }

                ncnn::VkMat out_alpha_tile_gpu;
                if (channels == 4)
                {
                    if (scale == 1)
                    {
                        out_alpha_tile_gpu = in_alpha_tile_gpu;
                    }
                    if (scale == 2)
                    {
                        bicubic_2x->forward(in_alpha_tile_gpu, out_alpha_tile_gpu, cmd, opt);
                    }
                    if (scale == 3)
                    {
                        bicubic_3x->forward(in_alpha_tile_gpu, out_alpha_tile_gpu, cmd, opt);
                    }
                    if (scale == 4)
                    {
                        bicubic_4x->forward(in_alpha_tile_gpu, out_alpha_tile_gpu, cmd, opt);
                    }
                }

                // postproc
                {
                    std::vector<ncnn::VkMat> bindings(3);
                    bindings[0] = out_tile_gpu;
                    bindings[1] = out_alpha_tile_gpu;
                    bindings[2] = out_gpu;

                    std::vector<ncnn::vk_constant_type> constants(13);
                    constants[0].i = out_tile_gpu.w;
                    constants[1].i = out_tile_gpu.h;
                    constants[2].i = out_tile_gpu.cstep;
                    constants[3].i = out_gpu.w;
                    constants[4].i = out_gpu.h;
                    constants[5].i = out_gpu.cstep;
                    constants[6].i = xi * TILE_SIZE_X * scale;
                    constants[7].i = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    constants[8].i = prepadding * scale;
                    constants[9].i = prepadding * scale;
                    constants[10].i = channels;
                    constants[11].i = out_alpha_tile_gpu.w;
                    constants[12].i = out_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    dispatcher.h = out_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realesrgan_postproc, bindings, constants, dispatcher);
                }
            }

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) gpu_err = -1;
                cmd.reset();
            }

            fprintf(stderr, "%.2f%%\n", (float)(yi * xtiles + xi) / (ytiles * xtiles) * 100);
        }

        // download
        {
            ncnn::Mat out;

            if (opt.use_fp16_storage && opt.use_int8_storage)
            {
                out = ncnn::Mat(out_gpu.w, out_gpu.h, (unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels, (size_t)channels, 1);
            }

            cmd.record_clone(out_gpu, out, opt);

            if (cmd.submit_and_wait() != 0) gpu_err = -1;

            if (!(opt.use_fp16_storage && opt.use_int8_storage))
            {
                if (channels == 3)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels, ncnn::Mat::PIXEL_RGB);
                }
                if (channels == 4)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels, ncnn::Mat::PIXEL_RGBA);
                }
            }
        }
    }

    // free the memory of this picture: kept in the pool, it piled up over a folder (sized for the biggest picture of every thread)
    blob_vkallocator->clear();
    staging_vkallocator->clear();
    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return gpu_err;
}
