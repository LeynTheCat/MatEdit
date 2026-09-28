#include "ImageLoader.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
}

bool IsRasterImagePath(const std::string& path) {
    const std::string ext = Lower(fs::path(path).extension().string());
    return ext == ".png" || ext == ".tga";
}

bool LoadRasterImage(const std::string& path, unsigned char*& pixels, int& width, int& height, int& channels) {
    pixels = nullptr;
    width = 0;
    height = 0;
    channels = 0;
    if (!IsRasterImagePath(path)) return false;
    pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        pixels = nullptr;
        width = height = channels = 0;
        return false;
    }
    channels = 4;
    return true;
}

void FreeRasterImage(unsigned char* pixels) {
    if (pixels) stbi_image_free(pixels);
}
