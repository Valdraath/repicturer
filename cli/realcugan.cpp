// realcugan implemented with ncnn library

#include "realcugan.h"
#include "net_load.h"

// gpu out-of-memory / device errors (upstream ignored them and produced black or partial images);
// thread local because one RealCUGAN is shared by several processing threads
static thread_local int t_cugan_err = 0;
int realcugan_take_error()
{
    const int e = t_cugan_err;
    t_cugan_err = 0;
    return e;
}

#include <algorithm>
#include <vector>
#include <map>

// ncnn
#include "cpu.h"

#include "realcugan_preproc.comp.hex.h"
#include "realcugan_postproc.comp.hex.h"
#include "realcugan_4x_postproc.comp.hex.h"

class FeatureCache
{
public:
    void clear()
    {
        gpu_cache.clear();
        cpu_cache.clear();
    }

    std::string make_key(int yi, int xi, int ti, const std::string& name) const
    {
        return std::to_string(yi) + "-" + std::to_string(xi) + "-" + std::to_string(ti) + "-" + name;
    }

    void load(int yi, int xi, int ti, const std::string& name, ncnn::VkMat& feat)
    {
        feat = gpu_cache[make_key(yi, xi, ti, name)];
    }

    void save(int yi, int xi, int ti, const std::string& name, ncnn::VkMat& feat)
    {
        gpu_cache[make_key(yi, xi, ti, name)] = feat;
    }

    void load(int yi, int xi, int ti, const std::string& name, ncnn::Mat& feat)
    {
        feat = cpu_cache[make_key(yi, xi, ti, name)];
    }

    void save(int yi, int xi, int ti, const std::string& name, ncnn::Mat& feat)
    {
        cpu_cache[make_key(yi, xi, ti, name)] = feat;
    }

public:
    std::map<std::string, ncnn::VkMat> gpu_cache;
    std::map<std::string, ncnn::Mat> cpu_cache;
};

RealCUGAN::RealCUGAN(int gpuid, int num_threads)
{
    vkdev = gpuid == -1 ? 0 : ncnn::get_gpu_device(gpuid);

    net.opt.num_threads = num_threads;

    realcugan_preproc = 0;
    realcugan_postproc = 0;
    realcugan_4x_postproc = 0;
    bicubic_2x = 0;
    bicubic_3x = 0;
    bicubic_4x = 0;
}

RealCUGAN::~RealCUGAN()
{
    // cleanup preprocess and postprocess pipeline
    {
        delete realcugan_preproc;
        delete realcugan_postproc;
        delete realcugan_4x_postproc;
    }

    if (bicubic_2x)
    {
        bicubic_2x->destroy_pipeline(net.opt);
        delete bicubic_2x;
    }

    if (bicubic_3x)
    {
        bicubic_3x->destroy_pipeline(net.opt);
        delete bicubic_3x;
    }

    if (bicubic_4x)
    {
        bicubic_4x->destroy_pipeline(net.opt);
        delete bicubic_4x;
    }
}

#if _WIN32
int RealCUGAN::load(const std::wstring& parampath, const std::wstring& modelpath)
#else
int RealCUGAN::load(const std::string& parampath, const std::string& modelpath)
#endif
{
    net.opt.use_vulkan_compute = vkdev ? true : false;
    net.opt.use_fp16_packed = true;
    net.opt.use_fp16_storage = vkdev ? true : false;
    net.opt.use_fp16_arithmetic = false;
    net.opt.use_int8_storage = true;

    // recent ncnn dereferences the device: only set it for gpu instances (cpu mode crashed upstream)
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
                    compile_spirv_module(realcugan_preproc_comp_data, sizeof(realcugan_preproc_comp_data), net.opt, spirv);
                }
            }

            realcugan_preproc = new ncnn::Pipeline(vkdev);
            realcugan_preproc->set_optimal_local_size_xyz(8, 8, 3);
            realcugan_preproc->create(spirv.data(), spirv.size() * 4, specializations);
        }

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    compile_spirv_module(realcugan_postproc_comp_data, sizeof(realcugan_postproc_comp_data), net.opt, spirv);
                }
            }

            realcugan_postproc = new ncnn::Pipeline(vkdev);
            realcugan_postproc->set_optimal_local_size_xyz(8, 8, 3);
            realcugan_postproc->create(spirv.data(), spirv.size() * 4, specializations);
        }

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    compile_spirv_module(realcugan_4x_postproc_comp_data, sizeof(realcugan_4x_postproc_comp_data), net.opt, spirv);
                }
            }

            realcugan_4x_postproc = new ncnn::Pipeline(vkdev);
            realcugan_4x_postproc->set_optimal_local_size_xyz(8, 8, 3);
            realcugan_4x_postproc->create(spirv.data(), spirv.size() * 4, specializations);
        }
    }

    // bicubic 2x/3x/4x for alpha channel
    {
        bicubic_2x = ncnn::create_layer("Interp");
        bicubic_2x->vkdev = vkdev;

        ncnn::ParamDict pd;
        pd.set(0, 3);// bicubic
        pd.set(1, 2.f);
        pd.set(2, 2.f);
        bicubic_2x->load_param(pd);

        bicubic_2x->create_pipeline(net.opt);
    }
    {
        bicubic_3x = ncnn::create_layer("Interp");
        bicubic_3x->vkdev = vkdev;

        ncnn::ParamDict pd;
        pd.set(0, 3);// bicubic
        pd.set(1, 3.f);
        pd.set(2, 3.f);
        bicubic_3x->load_param(pd);

        bicubic_3x->create_pipeline(net.opt);
    }
    {
        bicubic_4x = ncnn::create_layer("Interp");
        bicubic_4x->vkdev = vkdev;

        ncnn::ParamDict pd;
        pd.set(0, 3);// bicubic
        pd.set(1, 4.f);
        pd.set(2, 4.f);
        bicubic_4x->load_param(pd);

        bicubic_4x->create_pipeline(net.opt);
    }

    return 0;
}

int RealCUGAN::process(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    bool syncgap_needed = tilesize < std::max(inimage.w, inimage.h);

    if (!vkdev)
    {
        // cpu only
        if (syncgap_needed && syncgap)
        {
            if (syncgap == 1)
                return process_cpu_se(inimage, outimage);
            if (syncgap == 2)
                return process_cpu_se_rough(inimage, outimage);
            if (syncgap == 3)
                return process_cpu_se_very_rough(inimage, outimage);
        }
        else
            return process_cpu(inimage, outimage);
    }

    if (noise == -1 && scale == 1)
    {
        outimage = inimage;
        return 0;
    }

    if (syncgap_needed && syncgap)
    {
        if (syncgap == 1)
            return process_se(inimage, outimage);
        if (syncgap == 2)
            return process_se_rough(inimage, outimage);
        if (syncgap == 3)
            return process_se_very_rough(inimage, outimage);
    }

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

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;

    //#pragma omp parallel for num_threads(2)
    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

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
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
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

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            {
                // preproc
                ncnn::VkMat in_tile_gpu;
                ncnn::VkMat in_alpha_tile_gpu;
                {
                    // crop tile
                    int tile_x0 = xi * TILE_SIZE_X - prepadding;
                    int tile_x1 = std::min((xi + 1) * TILE_SIZE_X, w) + prepadding_right;
                    int tile_y0 = yi * TILE_SIZE_Y - prepadding;
                    int tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h) + prepadding_bottom;

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

                    cmd.record_pipeline(realcugan_preproc, bindings, constants, dispatcher);
                }

                // realcugan
                ncnn::VkMat out_tile_gpu;
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.set_blob_vkallocator(blob_vkallocator);
                    ex.set_workspace_vkallocator(blob_vkallocator);
                    ex.set_staging_vkallocator(staging_vkallocator);

                    ex.input("in0", in_tile_gpu);

                    if (ex.extract("out0", out_tile_gpu, cmd) != 0) t_cugan_err = -1;
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
                if (scale == 4)
                {
                    std::vector<ncnn::VkMat> bindings(4);
                    bindings[0] = in_gpu;
                    bindings[1] = out_tile_gpu;
                    bindings[2] = out_alpha_tile_gpu;
                    bindings[3] = out_gpu;

                    std::vector<ncnn::vk_constant_type> constants(16);
                    constants[0].i = in_gpu.w;
                    constants[1].i = in_gpu.h;
                    constants[2].i = in_gpu.cstep;
                    constants[3].i = out_tile_gpu.w;
                    constants[4].i = out_tile_gpu.h;
                    constants[5].i = out_tile_gpu.cstep;
                    constants[6].i = out_gpu.w;
                    constants[7].i = out_gpu.h;
                    constants[8].i = out_gpu.cstep;
                    constants[9].i = xi * TILE_SIZE_X;
                    constants[10].i = std::min(yi * TILE_SIZE_Y, prepadding);
                    constants[11].i = xi * TILE_SIZE_X * scale;
                    constants[12].i = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    constants[13].i = channels;
                    constants[14].i = out_alpha_tile_gpu.w;
                    constants[15].i = out_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    dispatcher.h = out_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realcugan_4x_postproc, bindings, constants, dispatcher);
                }
                else
                {
                    std::vector<ncnn::VkMat> bindings(3);
                    bindings[0] = out_tile_gpu;
                    bindings[1] = out_alpha_tile_gpu;
                    bindings[2] = out_gpu;

                    std::vector<ncnn::vk_constant_type> constants(11);
                    constants[0].i = out_tile_gpu.w;
                    constants[1].i = out_tile_gpu.h;
                    constants[2].i = out_tile_gpu.cstep;
                    constants[3].i = out_gpu.w;
                    constants[4].i = out_gpu.h;
                    constants[5].i = out_gpu.cstep;
                    constants[6].i = xi * TILE_SIZE_X * scale;
                    constants[7].i = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    constants[8].i = channels;
                    constants[9].i = out_alpha_tile_gpu.w;
                    constants[10].i = out_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    dispatcher.h = out_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realcugan_postproc, bindings, constants, dispatcher);
                }
            }

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        // download
        {
            ncnn::Mat out;

            if (opt.use_fp16_storage && opt.use_int8_storage)
            {
                out = ncnn::Mat(out_gpu.w, out_gpu.h, (unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels, (size_t)channels, 1);
            }

            cmd.record_clone(out_gpu, out, opt);

            if (cmd.submit_and_wait() != 0) t_cugan_err = -1;

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

    return 0;
}

int RealCUGAN::process_cpu(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    if (noise == -1 && scale == 1)
    {
        outimage = inimage;
        return 0;
    }

    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            int in_tile_x0 = std::max(xi * TILE_SIZE_X - prepadding, 0);
            int in_tile_x1 = std::min((xi + 1) * TILE_SIZE_X + prepadding_right, w);

            // crop tile
            ncnn::Mat in;
            {
                if (channels == 3)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGB, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
                if (channels == 4)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
            }

            ncnn::Mat out;

            {
                // split alpha and preproc
                ncnn::Mat in_tile;
                ncnn::Mat in_alpha_tile;
                {
                    in_tile.create(in.w, in.h, 3);
                    for (int q = 0; q < 3; q++)
                    {
                        const float* ptr = in.channel(q);
                        float* outptr = in_tile.channel(q);

                        for (int i = 0; i < in.w * in.h; i++)
                        {
                            *outptr++ = *ptr++ * (1 / 255.f);
                        }
                    }

                    if (channels == 4)
                    {
                        in_alpha_tile = in.channel_range(3, 1).clone();
                    }
                }

                // border padding
                {
                    int pad_top = std::max(prepadding - yi * TILE_SIZE_Y, 0);
                    int pad_bottom = std::max(std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom - h, prepadding_bottom), 0);
                    int pad_left = std::max(prepadding - xi * TILE_SIZE_X, 0);
                    int pad_right = std::max(std::min((xi + 1) * TILE_SIZE_X + prepadding_right - w, prepadding_right), 0);

                    ncnn::Mat in_tile_padded;
                    ncnn::copy_make_border(in_tile, in_tile_padded, pad_top, pad_bottom, pad_left, pad_right, 2, 0.f, net.opt);
                    in_tile = in_tile_padded;
                }

                // realcugan
                ncnn::Mat out_tile;
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.input("in0", in_tile);

                    if (ex.extract("out0", out_tile) != 0) t_cugan_err = -1;
                }

                ncnn::Mat out_alpha_tile;
                if (channels == 4)
                {
                    if (scale == 1)
                    {
                        out_alpha_tile = in_alpha_tile;
                    }
                    if (scale == 2)
                    {
                        bicubic_2x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                    if (scale == 3)
                    {
                        bicubic_3x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                    if (scale == 4)
                    {
                        bicubic_4x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                }

                // postproc and merge alpha
                {
                    out.create(tile_w_nopad * scale, tile_h_nopad * scale, channels);
                    if (scale == 4)
                    {
                        for (int q = 0; q < 3; q++)
                        {
                            float* outptr = out.channel(q);

                            for (int i = 0; i < out.h; i++)
                            {
                                const float* inptr = in_tile.channel(q).row(prepadding + i / 4) + prepadding;
                                const float* ptr = out_tile.channel(q).row(i);

                                for (int j = 0; j < out.w; j++)
                                {
                                    *outptr++ = *ptr++ * 255.f + 0.5f + inptr[j / 4] * 255.f;
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int q = 0; q < 3; q++)
                        {
                            float* outptr = out.channel(q);

                            for (int i = 0; i < out.h; i++)
                            {
                                const float* ptr = out_tile.channel(q).row(i);

                                for (int j = 0; j < out.w; j++)
                                {
                                    *outptr++ = *ptr++ * 255.f + 0.5f;
                                }
                            }
                        }
                    }

                    if (channels == 4)
                    {
                        memcpy(out.channel_range(3, 1), out_alpha_tile, out_alpha_tile.total() * sizeof(float));
                    }
                }
            }

            {
                if (channels == 3)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels + xi * scale * TILE_SIZE_X * channels, ncnn::Mat::PIXEL_RGB, w * scale * channels);
                }
                if (channels == 4)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels + xi * scale * TILE_SIZE_X * channels, ncnn::Mat::PIXEL_RGBA, w * scale * channels);
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_se(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

    ncnn::Option opt = net.opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;

    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0"};
    process_se_stage0(inimage, in0, out0, opt, cache);

    std::vector<std::string> gap0 = {"gap0"};
    process_se_sync_gap(inimage, gap0, opt, cache);

    std::vector<std::string> in1 = {"gap0"};
    std::vector<std::string> out1 = {"gap1"};
    process_se_stage0(inimage, in1, out1, opt, cache);

    std::vector<std::string> gap1 = {"gap1"};
    process_se_sync_gap(inimage, gap1, opt, cache);

    std::vector<std::string> in2 = {"gap0", "gap1"};
    std::vector<std::string> out2 = {"gap2"};
    process_se_stage0(inimage, in2, out2, opt, cache);

    std::vector<std::string> gap2 = {"gap2"};
    process_se_sync_gap(inimage, gap2, opt, cache);

    std::vector<std::string> in3 = {"gap0", "gap1", "gap2"};
    std::vector<std::string> out3 = {"gap3"};
    process_se_stage0(inimage, in3, out3, opt, cache);

    std::vector<std::string> gap3 = {"gap3"};
    process_se_sync_gap(inimage, gap3, opt, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_stage2(inimage, in4, outimage, opt, cache);

    cache.clear();

    // free the memory of this picture: kept in the pool, it piled up over a folder (sized for the biggest picture of every thread)
    blob_vkallocator->clear();
    staging_vkallocator->clear();
    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return 0;
}

int RealCUGAN::process_se_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

    ncnn::Option opt = net.opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;

    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_stage0(inimage, in0, out0, opt, cache);

    std::vector<std::string> gap0 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_sync_gap(inimage, gap0, opt, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_stage2(inimage, in4, outimage, opt, cache);

    cache.clear();

    // free the memory of this picture: kept in the pool, it piled up over a folder (sized for the biggest picture of every thread)
    blob_vkallocator->clear();
    staging_vkallocator->clear();
    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return 0;
}

int RealCUGAN::process_se_very_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

    ncnn::Option opt = net.opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;

    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_very_rough_stage0(inimage, in0, out0, opt, cache);

    std::vector<std::string> gap0 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_very_rough_sync_gap(inimage, gap0, opt, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_se_stage2(inimage, in4, outimage, opt, cache);

    cache.clear();

    // free the memory of this picture: kept in the pool, it piled up over a folder (sized for the biggest picture of every thread)
    blob_vkallocator->clear();
    staging_vkallocator->clear();
    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return 0;
}

int RealCUGAN::process_cpu_se(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0"};
    process_cpu_se_stage0(inimage, in0, out0, cache);

    std::vector<std::string> gap0 = {"gap0"};
    process_cpu_se_sync_gap(inimage, gap0, cache);

    std::vector<std::string> in1 = {"gap0"};
    std::vector<std::string> out1 = {"gap1"};
    process_cpu_se_stage0(inimage, in1, out1, cache);

    std::vector<std::string> gap1 = {"gap1"};
    process_cpu_se_sync_gap(inimage, gap1, cache);

    std::vector<std::string> in2 = {"gap0", "gap1"};
    std::vector<std::string> out2 = {"gap2"};
    process_cpu_se_stage0(inimage, in2, out2, cache);

    std::vector<std::string> gap2 = {"gap2"};
    process_cpu_se_sync_gap(inimage, gap2, cache);

    std::vector<std::string> in3 = {"gap0", "gap1", "gap2"};
    std::vector<std::string> out3 = {"gap3"};
    process_cpu_se_stage0(inimage, in3, out3, cache);

    std::vector<std::string> gap3 = {"gap3"};
    process_cpu_se_sync_gap(inimage, gap3, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_stage2(inimage, in4, outimage, cache);

    cache.clear();

    return 0;
}

int RealCUGAN::process_cpu_se_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_stage0(inimage, in0, out0, cache);

    std::vector<std::string> gap0 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_sync_gap(inimage, gap0, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_stage2(inimage, in4, outimage, cache);

    cache.clear();

    return 0;
}

int RealCUGAN::process_cpu_se_very_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const
{
    FeatureCache cache;

    std::vector<std::string> in0 = {};
    std::vector<std::string> out0 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_very_rough_stage0(inimage, in0, out0, cache);

    std::vector<std::string> gap0 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_very_rough_sync_gap(inimage, gap0, cache);

    std::vector<std::string> in4 = {"gap0", "gap1", "gap2", "gap3"};
    process_cpu_se_stage2(inimage, in4, outimage, cache);

    cache.clear();

    return 0;
}

int RealCUGAN::process_se_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, const ncnn::Option& opt, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;

    //#pragma omp parallel for num_threads(2)
    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

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
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        int out_tile_y0 = std::max(yi * TILE_SIZE_Y, 0);
        int out_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h);

        ncnn::VkMat out_gpu;
        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, (size_t)channels, 1, opt.blob_vkallocator);
        }
        else
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, channels, (size_t)4u, 1, opt.blob_vkallocator);
        }

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            {
                // preproc
                ncnn::VkMat in_tile_gpu;
                ncnn::VkMat in_alpha_tile_gpu;
                {
                    // crop tile
                    int tile_x0 = xi * TILE_SIZE_X - prepadding;
                    int tile_x1 = std::min((xi + 1) * TILE_SIZE_X, w) + prepadding_right;
                    int tile_y0 = yi * TILE_SIZE_Y - prepadding;
                    int tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h) + prepadding_bottom;

                    in_tile_gpu.create(tile_x1 - tile_x0, tile_y1 - tile_y0, 3, in_out_tile_elemsize, 1, opt.blob_vkallocator);

                    if (channels == 4)
                    {
                        in_alpha_tile_gpu.create(tile_w_nopad, tile_h_nopad, 1, in_out_tile_elemsize, 1, opt.blob_vkallocator);
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

                    cmd.record_pipeline(realcugan_preproc, bindings, constants, dispatcher);
                }

                // realcugan
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.set_blob_vkallocator(opt.blob_vkallocator);
                    ex.set_workspace_vkallocator(opt.blob_vkallocator);
                    ex.set_staging_vkallocator(opt.staging_vkallocator);

                    ex.input("in0", in_tile_gpu);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::VkMat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    for (size_t i = 0; i < outnames.size(); i++)
                    {
                        ncnn::VkMat feat;
                        if (ex.extract(outnames[i].c_str(), feat, cmd) != 0) t_cugan_err = -1;

                        cache.save(yi, xi, 0, outnames[i], feat);
                    }
                }
            }

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
        cmd.reset();
    }

    return 0;
}

int RealCUGAN::process_se_stage2(const ncnn::Mat& inimage, const std::vector<std::string>& names, ncnn::Mat& outimage, const ncnn::Option& opt, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;

    //#pragma omp parallel for num_threads(2)
    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

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
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        int out_tile_y0 = std::max(yi * TILE_SIZE_Y, 0);
        int out_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h);

        ncnn::VkMat out_gpu;
        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, (size_t)channels, 1, opt.blob_vkallocator);
        }
        else
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, channels, (size_t)4u, 1, opt.blob_vkallocator);
        }

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            {
                // preproc
                ncnn::VkMat in_tile_gpu;
                ncnn::VkMat in_alpha_tile_gpu;
                {
                    // crop tile
                    int tile_x0 = xi * TILE_SIZE_X - prepadding;
                    int tile_x1 = std::min((xi + 1) * TILE_SIZE_X, w) + prepadding_right;
                    int tile_y0 = yi * TILE_SIZE_Y - prepadding;
                    int tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h) + prepadding_bottom;

                    in_tile_gpu.create(tile_x1 - tile_x0, tile_y1 - tile_y0, 3, in_out_tile_elemsize, 1, opt.blob_vkallocator);

                    if (channels == 4)
                    {
                        in_alpha_tile_gpu.create(tile_w_nopad, tile_h_nopad, 1, in_out_tile_elemsize, 1, opt.blob_vkallocator);
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

                    cmd.record_pipeline(realcugan_preproc, bindings, constants, dispatcher);
                }

                // realcugan
                ncnn::VkMat out_tile_gpu;
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.set_blob_vkallocator(opt.blob_vkallocator);
                    ex.set_workspace_vkallocator(opt.blob_vkallocator);
                    ex.set_staging_vkallocator(opt.staging_vkallocator);

                    ex.input("in0", in_tile_gpu);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::VkMat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    if (ex.extract("out0", out_tile_gpu, cmd) != 0) t_cugan_err = -1;
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
                if (scale == 4)
                {
                    std::vector<ncnn::VkMat> bindings(4);
                    bindings[0] = in_gpu;
                    bindings[1] = out_tile_gpu;
                    bindings[2] = out_alpha_tile_gpu;
                    bindings[3] = out_gpu;

                    std::vector<ncnn::vk_constant_type> constants(16);
                    constants[0].i = in_gpu.w;
                    constants[1].i = in_gpu.h;
                    constants[2].i = in_gpu.cstep;
                    constants[3].i = out_tile_gpu.w;
                    constants[4].i = out_tile_gpu.h;
                    constants[5].i = out_tile_gpu.cstep;
                    constants[6].i = out_gpu.w;
                    constants[7].i = out_gpu.h;
                    constants[8].i = out_gpu.cstep;
                    constants[9].i = xi * TILE_SIZE_X;
                    constants[10].i = std::min(yi * TILE_SIZE_Y, prepadding);
                    constants[11].i = xi * TILE_SIZE_X * scale;
                    constants[12].i = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    constants[13].i = channels;
                    constants[14].i = out_alpha_tile_gpu.w;
                    constants[15].i = out_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    dispatcher.h = out_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realcugan_4x_postproc, bindings, constants, dispatcher);
                }
                else
                {
                    std::vector<ncnn::VkMat> bindings(3);
                    bindings[0] = out_tile_gpu;
                    bindings[1] = out_alpha_tile_gpu;
                    bindings[2] = out_gpu;

                    std::vector<ncnn::vk_constant_type> constants(11);
                    constants[0].i = out_tile_gpu.w;
                    constants[1].i = out_tile_gpu.h;
                    constants[2].i = out_tile_gpu.cstep;
                    constants[3].i = out_gpu.w;
                    constants[4].i = out_gpu.h;
                    constants[5].i = out_gpu.cstep;
                    constants[6].i = xi * TILE_SIZE_X * scale;
                    constants[7].i = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    constants[8].i = channels;
                    constants[9].i = out_alpha_tile_gpu.w;
                    constants[10].i = out_alpha_tile_gpu.h;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = std::min(TILE_SIZE_X * scale, out_gpu.w - xi * TILE_SIZE_X * scale);
                    dispatcher.h = out_gpu.h;
                    dispatcher.c = channels;

                    cmd.record_pipeline(realcugan_postproc, bindings, constants, dispatcher);
                }
            }

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        // download
        {
            ncnn::Mat out;

            if (opt.use_fp16_storage && opt.use_int8_storage)
            {
                out = ncnn::Mat(out_gpu.w, out_gpu.h, (unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels, (size_t)channels, 1);
            }

            cmd.record_clone(out_gpu, out, opt);

            if (cmd.submit_and_wait() != 0) t_cugan_err = -1;

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

    return 0;
}

int RealCUGAN::process_se_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, const ncnn::Option& opt, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    std::vector< std::vector<ncnn::VkMat> > feats(names.size());
    for (int yi = 0; yi < ytiles; yi++)
    {
        for (int xi = 0; xi < xtiles; xi++)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        ncnn::VkMat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        feats[i].push_back(feat);
                    }
                }
            }
        }
    }

    const int tiles = ytiles * xtiles * 1;

    ncnn::VkCompute cmd(vkdev);

    // download
    std::vector< std::vector<ncnn::Mat> > feats_cpu(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        feats_cpu[i].resize(tiles);

        for (int j = 0; j < tiles; j++)
        {
            cmd.record_download(feats[i][j], feats_cpu[i][j], opt);
        }
    }

    if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
    cmd.reset();

    // global average
    // upload
    std::vector<ncnn::VkMat> avgfeats(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        for (int j = 0; j < tiles; j++)
        {
            if (opt.use_fp16_storage && ncnn::cpu_support_arm_asimdhp() && feats_cpu[i][j].elembits() == 16)
            {
                ncnn::Mat feat_fp32;
                ncnn::cast_float16_to_float32(feats_cpu[i][j], feat_fp32, opt);
                feats_cpu[i][j] = feat_fp32;
            }

            if (opt.use_packing_layout && feats_cpu[i][j].elempack != 1)
            {
                ncnn::Mat feat_cpu_unpacked;
                ncnn::convert_packing(feats_cpu[i][j], feat_cpu_unpacked, 1, opt);
                feats_cpu[i][j] = feat_cpu_unpacked;
            }
        }

        // handle feats_cpu[i] vector
        {
            ncnn::Mat avgfeat;
            avgfeat.create_like(feats_cpu[i][0]);
            avgfeat.fill(0.f);

            int len = avgfeat.total();

            for (int j = 0; j < tiles; j++)
            {
                const ncnn::Mat f = feats_cpu[i][j];

                for (int k = 0; k < len; k++)
                {
                    avgfeat[k] += f[k];
                }
            }

            for (int k = 0; k < len; k++)
            {
                avgfeat[k] /= tiles;
            }

            cmd.record_upload(avgfeat, avgfeats[i], opt);
        }
    }

    if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
    cmd.reset();


    for (int yi = 0; yi < ytiles; yi++)
    {
        for (int xi = 0; xi < xtiles; xi++)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        cache.save(yi, xi, 0, names[i], avgfeats[i]);
                    }
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_se_very_rough_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, const ncnn::Option& opt, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = 32;
    const int TILE_SIZE_Y = 32;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;

    //#pragma omp parallel for num_threads(2)
    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

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
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        int out_tile_y0 = std::max(yi * TILE_SIZE_Y, 0);
        int out_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h);

        ncnn::VkMat out_gpu;
        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, (size_t)channels, 1, opt.blob_vkallocator);
        }
        else
        {
            out_gpu.create(w * scale, (out_tile_y1 - out_tile_y0) * scale, channels, (size_t)4u, 1, opt.blob_vkallocator);
        }

        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            {
                // preproc
                ncnn::VkMat in_tile_gpu;
                ncnn::VkMat in_alpha_tile_gpu;
                {
                    // crop tile
                    int tile_x0 = xi * TILE_SIZE_X - prepadding;
                    int tile_x1 = std::min((xi + 1) * TILE_SIZE_X, w) + prepadding_right;
                    int tile_y0 = yi * TILE_SIZE_Y - prepadding;
                    int tile_y1 = std::min((yi + 1) * TILE_SIZE_Y, h) + prepadding_bottom;

                    in_tile_gpu.create(tile_x1 - tile_x0, tile_y1 - tile_y0, 3, in_out_tile_elemsize, 1, opt.blob_vkallocator);

                    if (channels == 4)
                    {
                        in_alpha_tile_gpu.create(tile_w_nopad, tile_h_nopad, 1, in_out_tile_elemsize, 1, opt.blob_vkallocator);
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

                    cmd.record_pipeline(realcugan_preproc, bindings, constants, dispatcher);
                }

                // realcugan
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.set_blob_vkallocator(opt.blob_vkallocator);
                    ex.set_workspace_vkallocator(opt.blob_vkallocator);
                    ex.set_staging_vkallocator(opt.staging_vkallocator);

                    ex.input("in0", in_tile_gpu);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::VkMat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    for (size_t i = 0; i < outnames.size(); i++)
                    {
                        ncnn::VkMat feat;
                        if (ex.extract(outnames[i].c_str(), feat, cmd) != 0) t_cugan_err = -1;

                        cache.save(yi, xi, 0, outnames[i], feat);
                    }
                }
            }

            if (xtiles > 1)
            {
                if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
                cmd.reset();
            }
        }

        if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
        cmd.reset();
    }

    return 0;
}

int RealCUGAN::process_se_very_rough_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, const ncnn::Option& opt, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = 32;
    const int TILE_SIZE_Y = 32;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    std::vector< std::vector<ncnn::VkMat> > feats(names.size());
    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        ncnn::VkMat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        feats[i].push_back(feat);
                    }
                }
            }
        }
    }

    const int tiles = (ytiles / 3) * (xtiles / 3) * 1;

    ncnn::VkCompute cmd(vkdev);

    // download
    std::vector< std::vector<ncnn::Mat> > feats_cpu(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        feats_cpu[i].resize(tiles);

        for (int j = 0; j < tiles; j++)
        {
            cmd.record_download(feats[i][j], feats_cpu[i][j], opt);
        }
    }

    if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
    cmd.reset();

    // global average
    // upload
    std::vector<ncnn::VkMat> avgfeats(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        for (int j = 0; j < tiles; j++)
        {
            if (opt.use_fp16_storage && ncnn::cpu_support_arm_asimdhp() && feats_cpu[i][j].elembits() == 16)
            {
                ncnn::Mat feat_fp32;
                ncnn::cast_float16_to_float32(feats_cpu[i][j], feat_fp32, opt);
                feats_cpu[i][j] = feat_fp32;
            }

            if (opt.use_packing_layout && feats_cpu[i][j].elempack != 1)
            {
                ncnn::Mat feat_cpu_unpacked;
                ncnn::convert_packing(feats_cpu[i][j], feat_cpu_unpacked, 1, opt);
                feats_cpu[i][j] = feat_cpu_unpacked;
            }
        }

        // handle feats_cpu[i] vector
        {
            ncnn::Mat avgfeat;
            avgfeat.create_like(feats_cpu[i][0]);
            avgfeat.fill(0.f);

            int len = avgfeat.total();

            for (int j = 0; j < tiles; j++)
            {
                const ncnn::Mat f = feats_cpu[i][j];

                for (int k = 0; k < len; k++)
                {
                    avgfeat[k] += f[k];
                }
            }

            for (int k = 0; k < len; k++)
            {
                avgfeat[k] /= tiles;
            }

            cmd.record_upload(avgfeat, avgfeats[i], opt);
        }
    }

    if (cmd.submit_and_wait() != 0) t_cugan_err = -1;
    cmd.reset();


    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        cache.save(yi, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi, xi + 2, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi + 2, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi + 2, 0, names[i], avgfeats[i]);
                    }
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_cpu_se_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            int in_tile_x0 = std::max(xi * TILE_SIZE_X - prepadding, 0);
            int in_tile_x1 = std::min((xi + 1) * TILE_SIZE_X + prepadding_right, w);

            // crop tile
            ncnn::Mat in;
            {
                if (channels == 3)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGB, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
                if (channels == 4)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
            }

            ncnn::Mat out;

            {
                // split alpha and preproc
                ncnn::Mat in_tile;
                ncnn::Mat in_alpha_tile;
                {
                    in_tile.create(in.w, in.h, 3);
                    for (int q = 0; q < 3; q++)
                    {
                        const float* ptr = in.channel(q);
                        float* outptr = in_tile.channel(q);

                        for (int i = 0; i < in.w * in.h; i++)
                        {
                            *outptr++ = *ptr++ * (1 / 255.f);
                        }
                    }

                    if (channels == 4)
                    {
                        in_alpha_tile = in.channel_range(3, 1).clone();
                    }
                }

                // border padding
                {
                    int pad_top = std::max(prepadding - yi * TILE_SIZE_Y, 0);
                    int pad_bottom = std::max(std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom - h, prepadding_bottom), 0);
                    int pad_left = std::max(prepadding - xi * TILE_SIZE_X, 0);
                    int pad_right = std::max(std::min((xi + 1) * TILE_SIZE_X + prepadding_right - w, prepadding_right), 0);

                    ncnn::Mat in_tile_padded;
                    ncnn::copy_make_border(in_tile, in_tile_padded, pad_top, pad_bottom, pad_left, pad_right, 2, 0.f, net.opt);
                    in_tile = in_tile_padded;
                }

                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.input("in0", in_tile);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::Mat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    for (size_t i = 0; i < outnames.size(); i++)
                    {
                        ncnn::Mat feat;
                        if (ex.extract(outnames[i].c_str(), feat) != 0) t_cugan_err = -1;

                        cache.save(yi, xi, 0, outnames[i], feat);
                    }
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_cpu_se_stage2(const ncnn::Mat& inimage, const std::vector<std::string>& names, ncnn::Mat& outimage, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    for (int yi = 0; yi < ytiles; yi++)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

        for (int xi = 0; xi < xtiles; xi++)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            int in_tile_x0 = std::max(xi * TILE_SIZE_X - prepadding, 0);
            int in_tile_x1 = std::min((xi + 1) * TILE_SIZE_X + prepadding_right, w);

            // crop tile
            ncnn::Mat in;
            {
                if (channels == 3)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGB, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
                if (channels == 4)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
            }

            ncnn::Mat out;

            {
                // split alpha and preproc
                ncnn::Mat in_tile;
                ncnn::Mat in_alpha_tile;
                {
                    in_tile.create(in.w, in.h, 3);
                    for (int q = 0; q < 3; q++)
                    {
                        const float* ptr = in.channel(q);
                        float* outptr = in_tile.channel(q);

                        for (int i = 0; i < in.w * in.h; i++)
                        {
                            *outptr++ = *ptr++ * (1 / 255.f);
                        }
                    }

                    if (channels == 4)
                    {
                        in_alpha_tile = in.channel_range(3, 1).clone();
                    }
                }

                // border padding
                {
                    int pad_top = std::max(prepadding - yi * TILE_SIZE_Y, 0);
                    int pad_bottom = std::max(std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom - h, prepadding_bottom), 0);
                    int pad_left = std::max(prepadding - xi * TILE_SIZE_X, 0);
                    int pad_right = std::max(std::min((xi + 1) * TILE_SIZE_X + prepadding_right - w, prepadding_right), 0);

                    ncnn::Mat in_tile_padded;
                    ncnn::copy_make_border(in_tile, in_tile_padded, pad_top, pad_bottom, pad_left, pad_right, 2, 0.f, net.opt);
                    in_tile = in_tile_padded;
                }

                // realcugan
                ncnn::Mat out_tile;
                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.input("in0", in_tile);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::Mat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    if (ex.extract("out0", out_tile) != 0) t_cugan_err = -1;
                }

                ncnn::Mat out_alpha_tile;
                if (channels == 4)
                {
                    if (scale == 1)
                    {
                        out_alpha_tile = in_alpha_tile;
                    }
                    if (scale == 2)
                    {
                        bicubic_2x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                    if (scale == 3)
                    {
                        bicubic_3x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                    if (scale == 4)
                    {
                        bicubic_4x->forward(in_alpha_tile, out_alpha_tile, opt);
                    }
                }

                // postproc and merge alpha
                {
                    out.create(tile_w_nopad * scale, tile_h_nopad * scale, channels);
                    if (scale == 4)
                    {
                        for (int q = 0; q < 3; q++)
                        {
                            float* outptr = out.channel(q);

                            for (int i = 0; i < out.h; i++)
                            {
                                const float* inptr = in_tile.channel(q).row(prepadding + i / 4) + prepadding;
                                const float* ptr = out_tile.channel(q).row(i);

                                for (int j = 0; j < out.w; j++)
                                {
                                    *outptr++ = *ptr++ * 255.f + 0.5f + inptr[j / 4] * 255.f;
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int q = 0; q < 3; q++)
                        {
                            float* outptr = out.channel(q);

                            for (int i = 0; i < out.h; i++)
                            {
                                const float* ptr = out_tile.channel(q).row(i);

                                for (int j = 0; j < out.w; j++)
                                {
                                    *outptr++ = *ptr++ * 255.f + 0.5f;
                                }
                            }
                        }
                    }

                    if (channels == 4)
                    {
                        memcpy(out.channel_range(3, 1), out_alpha_tile, out_alpha_tile.total() * sizeof(float));
                    }
                }
            }

            {
                if (channels == 3)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels + xi * scale * TILE_SIZE_X * channels, ncnn::Mat::PIXEL_RGB, w * scale * channels);
                }
                if (channels == 4)
                {
                    out.to_pixels((unsigned char*)outimage.data + yi * scale * TILE_SIZE_Y * w * scale * channels + xi * scale * TILE_SIZE_X * channels, ncnn::Mat::PIXEL_RGBA, w * scale * channels);
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_cpu_se_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = tilesize;
    const int TILE_SIZE_Y = tilesize;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    std::vector< std::vector<ncnn::Mat> > feats(names.size());
    for (int yi = 0; yi < ytiles; yi++)
    {
        for (int xi = 0; xi < xtiles; xi++)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        ncnn::Mat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        feats[i].push_back(feat);
                    }
                }
            }
        }
    }

    const int tiles = ytiles * xtiles * 1;

    // global average
    std::vector<ncnn::Mat> avgfeats(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        // handle feats[i] vector
        {
            ncnn::Mat avgfeat;
            avgfeat.create_like(feats[i][0]);
            avgfeat.fill(0.f);

            int len = avgfeat.total();

            for (int j = 0; j < tiles; j++)
            {
                const ncnn::Mat f = feats[i][j];

                for (int k = 0; k < len; k++)
                {
                    avgfeat[k] += f[k];
                }
            }

            for (int k = 0; k < len; k++)
            {
                avgfeat[k] /= tiles;
            }

            avgfeats[i] = avgfeat;
        }
    }

    for (int yi = 0; yi < ytiles; yi++)
    {
        for (int xi = 0; xi < xtiles; xi++)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        cache.save(yi, xi, 0, names[i], avgfeats[i]);
                    }
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_cpu_se_very_rough_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = 32;
    const int TILE_SIZE_Y = 32;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        const int tile_h_nopad = std::min((yi + 1) * TILE_SIZE_Y, h) - yi * TILE_SIZE_Y;

        int prepadding_bottom = prepadding;
        if (scale == 1 || scale == 3)
        {
            prepadding_bottom += (tile_h_nopad + 3) / 4 * 4 - tile_h_nopad;
        }
        if (scale == 2 || scale == 4)
        {
            prepadding_bottom += (tile_h_nopad + 1) / 2 * 2 - tile_h_nopad;
        }

        int in_tile_y0 = std::max(yi * TILE_SIZE_Y - prepadding, 0);
        int in_tile_y1 = std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom, h);

        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            const int tile_w_nopad = std::min((xi + 1) * TILE_SIZE_X, w) - xi * TILE_SIZE_X;

            int prepadding_right = prepadding;
            if (scale == 1 || scale == 3)
            {
                prepadding_right += (tile_w_nopad + 3) / 4 * 4 - tile_w_nopad;
            }
            if (scale == 2 || scale == 4)
            {
                prepadding_right += (tile_w_nopad + 1) / 2 * 2 - tile_w_nopad;
            }

            int in_tile_x0 = std::max(xi * TILE_SIZE_X - prepadding, 0);
            int in_tile_x1 = std::min((xi + 1) * TILE_SIZE_X + prepadding_right, w);

            // crop tile
            ncnn::Mat in;
            {
                if (channels == 3)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGB, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
                if (channels == 4)
                {
                    in = ncnn::Mat::from_pixels_roi(pixeldata, ncnn::Mat::PIXEL_RGBA, w, h, in_tile_x0, in_tile_y0, in_tile_x1 - in_tile_x0, in_tile_y1 - in_tile_y0);
                }
            }

            ncnn::Mat out;

            {
                // split alpha and preproc
                ncnn::Mat in_tile;
                ncnn::Mat in_alpha_tile;
                {
                    in_tile.create(in.w, in.h, 3);
                    for (int q = 0; q < 3; q++)
                    {
                        const float* ptr = in.channel(q);
                        float* outptr = in_tile.channel(q);

                        for (int i = 0; i < in.w * in.h; i++)
                        {
                            *outptr++ = *ptr++ * (1 / 255.f);
                        }
                    }

                    if (channels == 4)
                    {
                        in_alpha_tile = in.channel_range(3, 1).clone();
                    }
                }

                // border padding
                {
                    int pad_top = std::max(prepadding - yi * TILE_SIZE_Y, 0);
                    int pad_bottom = std::max(std::min((yi + 1) * TILE_SIZE_Y + prepadding_bottom - h, prepadding_bottom), 0);
                    int pad_left = std::max(prepadding - xi * TILE_SIZE_X, 0);
                    int pad_right = std::max(std::min((xi + 1) * TILE_SIZE_X + prepadding_right - w, prepadding_right), 0);

                    ncnn::Mat in_tile_padded;
                    ncnn::copy_make_border(in_tile, in_tile_padded, pad_top, pad_bottom, pad_left, pad_right, 2, 0.f, net.opt);
                    in_tile = in_tile_padded;
                }

                {
                    ncnn::Extractor ex = net.create_extractor();

                    ex.input("in0", in_tile);

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        ncnn::Mat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        ex.input(names[i].c_str(), feat);
                    }

                    for (size_t i = 0; i < outnames.size(); i++)
                    {
                        ncnn::Mat feat;
                        if (ex.extract(outnames[i].c_str(), feat) != 0) t_cugan_err = -1;

                        cache.save(yi, xi, 0, outnames[i], feat);
                    }
                }
            }
        }
    }

    return 0;
}

int RealCUGAN::process_cpu_se_very_rough_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, FeatureCache& cache) const
{
    const unsigned char* pixeldata = (const unsigned char*)inimage.data;
    const int w = inimage.w;
    const int h = inimage.h;
    const int channels = inimage.elempack;

    const int TILE_SIZE_X = 32;
    const int TILE_SIZE_Y = 32;

    ncnn::Option opt = net.opt;

    // each tile 400x400
    const int xtiles = (w + TILE_SIZE_X - 1) / TILE_SIZE_X;
    const int ytiles = (h + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

    std::vector< std::vector<ncnn::Mat> > feats(names.size());
    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        ncnn::Mat feat;
                        cache.load(yi, xi, 0, names[i], feat);

                        feats[i].push_back(feat);
                    }
                }
            }
        }
    }

    const int tiles = (ytiles / 3) * (xtiles / 3) * 1;

    // global average
    std::vector<ncnn::Mat> avgfeats(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        // handle feats[i] vector
        {
            ncnn::Mat avgfeat;
            avgfeat.create_like(feats[i][0]);
            avgfeat.fill(0.f);

            int len = avgfeat.total();

            for (int j = 0; j < tiles; j++)
            {
                const ncnn::Mat f = feats[i][j];

                for (int k = 0; k < len; k++)
                {
                    avgfeat[k] += f[k];
                }
            }

            for (int k = 0; k < len; k++)
            {
                avgfeat[k] /= tiles;
            }

            avgfeats[i] = avgfeat;
        }
    }

    for (int yi = 0; yi + 2 < ytiles; yi += 3)
    {
        for (int xi = 0; xi + 2 < xtiles; xi += 3)
        {
            {
                for (size_t i = 0; i < names.size(); i++)
                {
                    {
                        cache.save(yi, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi, xi + 2, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi + 1, xi + 2, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi + 1, 0, names[i], avgfeats[i]);
                        cache.save(yi + 2, xi + 2, 0, names[i], avgfeats[i]);
                    }
                }
            }
        }
    }

    return 0;
}
