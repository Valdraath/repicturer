#pragma once

#include <string>

// width / height of an image file without decoding it
bool ProbeImageSize(const std::string& path, int& w, int& h);
