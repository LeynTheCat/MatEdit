#include "Skybox.h"
#include "ImageLoader.h"
#include "MaterialSystem.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <vector>
#include <gli/gli.hpp>

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct Group {
    std::string base;
    std::array<bool, 6> found{};
};

bool IsSkyboxFace(const fs::path& path, const std::array<std::string, 6>& suffixes, std::string& base, std::size_t& index) {
    const std::string stem = path.stem().string();
    const std::string lower = Lower(stem);
    if (lower.size() <= 2) return false;
    const std::string suffix = lower.substr(lower.size() - 2);
    const auto it = std::find(suffixes.begin(), suffixes.end(), suffix);
    if (it == suffixes.end()) return false;
    index = static_cast<std::size_t>(it - suffixes.begin());
    base = stem.substr(0, stem.size() - 2);
    return !base.empty();
}

GLuint UploadRasterCubemap(const fs::path& base) {
    const std::array<std::string, 6> suffixes = {"rt", "lf", "up", "dn", "bk", "ft"};
    std::array<unsigned char*, 6> pixels{};
    std::array<int, 6> widths{};
    std::array<int, 6> heights{};
    GLuint texture = 0;

    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        fs::path facePath;
        for (const char* ext : {".png", ".tga"}) {
            fs::path candidate = base.parent_path() / (base.filename().string() + suffixes[i] + ext);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                facePath = candidate;
                break;
            }
        }
        if (facePath.empty()) return 0;
        int channels = 0;
        if (!LoadRasterImage(facePath.string(), pixels[i], widths[i], heights[i], channels)) {
            for (auto* pixel : pixels) FreeRasterImage(pixel);
            return 0;
        }
        if (i > 0 && (widths[i] != widths[0] || heights[i] != heights[0])) {
            for (auto* pixel : pixels) FreeRasterImage(pixel);
            return 0;
        }
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(i), 0, GL_RGBA8,
                     widths[i], heights[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels[i]);
        FreeRasterImage(pixels[i]);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}
}

std::vector<std::string> FindSkyboxNames(const fs::path& envPath) {
    std::vector<std::string> result;
    std::error_code ec;
    if (!fs::is_directory(envPath, ec)) return result;

    const std::array<std::string, 6> suffixes = {"bk", "dn", "ft", "lf", "rt", "up"};
    std::map<std::string, Group> groups;

    for (const auto& item : fs::directory_iterator(envPath, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code typeEc;
        if (!item.is_regular_file(typeEc) || typeEc) continue;
        const std::string ext = Lower(item.path().extension().string());
        if (ext != ".dds" && ext != ".png" && ext != ".tga") continue;

        std::string base;
        std::size_t index = 0;
        if (!IsSkyboxFace(item.path(), suffixes, base, index)) continue;
        const std::string key = Lower(base);
        Group& group = groups[key];
        group.base = base;
        group.found[index] = true;
    }

    for (const auto& [key, group] : groups) {
        if (std::all_of(group.found.begin(), group.found.end(), [](bool value) { return value; })) result.push_back(group.base);
    }
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return Lower(a) < Lower(b);
    });
    return result;
}

GLuint LoadSkyboxCubemap(const std::string& path) {
    fs::path base = fs::path(path);
    if (base.is_relative()) base = gameRootPath / base;
    if (base.extension() == ".dds" || base.extension() == ".DDS" || base.extension() == ".png" || base.extension() == ".PNG" || base.extension() == ".tga" || base.extension() == ".TGA") {
        base.replace_extension();
    }

    const GLuint dds = LoadDDS_Cubemap(base.string());
    if (dds != 0) return dds;
    return UploadRasterCubemap(base);
}
