// shared ncnn model loading with error reporting (upstream ignored load failures)

#ifndef NET_LOAD_H
#define NET_LOAD_H

#include <stdio.h>
#include <string>
#include "net.h"

#if _WIN32
static inline int load_ncnn_net(ncnn::Net& net, const std::wstring& parampath, const std::wstring& modelpath)
{
    FILE* fp = _wfopen(parampath.c_str(), L"rb");
    if (!fp)
    {
        fwprintf(stderr, L"_wfopen %ls failed\n", parampath.c_str());
        return -1;
    }
    int ret = net.load_param(fp);
    fclose(fp);
    if (ret != 0) return -1;

    fp = _wfopen(modelpath.c_str(), L"rb");
    if (!fp)
    {
        fwprintf(stderr, L"_wfopen %ls failed\n", modelpath.c_str());
        return -1;
    }
    ret = net.load_model(fp);
    fclose(fp);
    return ret == 0 ? 0 : -1;
}
#else
static inline int load_ncnn_net(ncnn::Net& net, const std::string& parampath, const std::string& modelpath)
{
    if (net.load_param(parampath.c_str()) != 0)
    {
        fprintf(stderr, "load_param %s failed\n", parampath.c_str());
        return -1;
    }
    if (net.load_model(modelpath.c_str()) != 0)
    {
        fprintf(stderr, "load_model %s failed\n", modelpath.c_str());
        return -1;
    }
    return 0;
}
#endif

#endif // NET_LOAD_H
