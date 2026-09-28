#include "MaterialSystem.h"
#include "ImageLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <gli/gli.hpp>
#define _CRT_SECURE_NO_WARNINGS

fs::path gameRootPath = "";
std::vector<std::string> physicalMaterialTypes = {"default"};

std::string ResolveWadTextureReference(const std::string& reference);

std::vector<std::string> LoadPhysicalMaterialTypes() {
    std::vector<std::string> types;
    fs::path defPath = gameRootPath / "scripts" / "materials.def";
    if (!fs::exists(defPath)) {
        defPath = gameRootPath / "materials.def";
    }
    
    std::ifstream file(defPath);
    if (!file.is_open()) {
        return {"default"};
    }

    std::string line;
    std::string lastLine;
    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                types.push_back(lastLine.substr(n1 + 1, n2 - n1 - 1));
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }

    if (types.empty()) types.push_back("default");
    return types;
}

namespace {
std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

struct CachedDDS {
    GLuint texture = 0;
    std::filesystem::file_time_type writeTime{};
    std::size_t refs = 0;
    TextureFormatInfo formatInfo{};
};

std::unordered_map<std::string, CachedDDS> g_ddsCache;
std::unordered_map<GLuint, TextureFormatInfo> g_textureFormatInfo;
std::vector<WadArchive> g_wadArchives;

std::string NormalizeMaterialTextureReference(std::string value) {
    const std::string lower = ToLower(value);
    if (lower.rfind("wad://", 0) == 0 || lower.rfind("wad:/", 0) == 0) {
        const std::size_t separator = value.rfind('#');
        if (separator != std::string::npos) value = value.substr(separator + 1);
        const std::string extension = ToLower(fs::path(value).extension().string());
        if (extension == ".png" || extension == ".tga" || extension == ".dds") value = fs::path(value).stem().string();
        return value;
    }

    const std::string extension = ToLower(fs::path(value).extension().string());
    if (extension == ".png" || extension == ".tga") {
        const std::string wadReference = ResolveWadTextureReference(value);
        if (!wadReference.empty()) {
            const std::size_t separator = wadReference.rfind('#');
            if (separator != std::string::npos) value = wadReference.substr(separator + 1);
            const std::string resolvedExtension = ToLower(fs::path(value).extension().string());
            if (resolvedExtension == ".png" || resolvedExtension == ".tga" || resolvedExtension == ".dds") value = fs::path(value).stem().string();
        }
    }
    return value;
}

constexpr GLenum kGLCompressedRed_RGTC1 = 0x8DBB;
constexpr GLenum kGLCompressedRG_RGTC2 = 0x8DBD;
constexpr GLenum kGLCompressedSignedRG_RGTC2 = 0x8DBE;
constexpr GLenum kGLCompressedRGBA_BPTC_UNORM = 0x8E8C;
constexpr GLenum kGLCompressedSRGBAlpha_BPTC_UNORM = 0x8E8D;
constexpr GLenum kGLCompressedRGB_BPTC_SIGNED_FLOAT = 0x8E8E;
constexpr GLenum kGLCompressedRGB_BPTC_UNSIGNED_FLOAT = 0x8E8F;
constexpr GLenum kGLCompressedSRGB_S3TC_DXT1_EXT = 0x8C4C;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT1_EXT = 0x8C4D;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT3_EXT = 0x8C4E;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT5_EXT = 0x8C4F;

TextureFormatInfo DetectTextureFormatInfo(GLenum internalFormat, GLenum externalFormat) {
    TextureFormatInfo info;
    info.valid = internalFormat != GL_NONE;
    info.compressed = externalFormat == GL_NONE;
    info.channels = 4;

    switch (internalFormat) {
        case kGLCompressedRed_RGTC1:
            info.bc4 = true;
            info.channels = 1;
            break;
        case kGLCompressedRG_RGTC2:
        case kGLCompressedSignedRG_RGTC2:
            info.bc5 = true;
            info.channels = 2;
            break;
        case kGLCompressedRGBA_BPTC_UNORM:
            info.channels = 4;
            break;
        case kGLCompressedSRGBAlpha_BPTC_UNORM:
            info.channels = 4;
            info.srgb = true;
            break;
        case kGLCompressedRGB_BPTC_SIGNED_FLOAT:
        case kGLCompressedRGB_BPTC_UNSIGNED_FLOAT:
            info.channels = 3;
            break;
        case kGLCompressedSRGB_S3TC_DXT1_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT1_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT3_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT5_EXT:
            info.srgb = true;
            break;
        case GL_R8:
        case GL_R16F:
        case GL_R32F:
            info.channels = 1;
            break;
        case GL_RG8:
        case GL_RG16F:
        case GL_RG32F:
            info.channels = 2;
            break;
        case GL_RGB8:
        case GL_RGB16F:
        case GL_RGB32F:
            info.channels = 3;
            break;
        case GL_SRGB8:
            info.channels = 3;
            info.srgb = true;
            break;
        case GL_SRGB8_ALPHA8:
            info.channels = 4;
            info.srgb = true;
            break;
        default:
            break;
    }

    return info;
}

void CopyString(char* destination, std::size_t capacity, const std::string& value) {
    if (capacity == 0) return;
    std::snprintf(destination, capacity, "%s", value.c_str());
}

struct UploadedDDS {
    GLuint texture = 0;
    TextureFormatInfo formatInfo{};
};

UploadedDDS UploadDDS2D(const gli::texture& texture, const std::string& sourcePath) {
    if (texture.empty() || texture.levels() == 0) return {};

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(texture.format(), texture.swizzles());
    if (Format.Internal == GL_NONE) {
        std::cerr << "Unsupported DDS format: " << sourcePath << std::endl;
        return {};
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.levels() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    const TextureFormatInfo formatInfo = DetectTextureFormatInfo(Format.Internal, Format.External);
    if (formatInfo.bc4) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }
    for (std::size_t level = 0; level < texture.levels(); ++level) {
        const GLsizei width = static_cast<GLsizei>(texture.extent(level).x);
        const GLsizei height = static_cast<GLsizei>(texture.extent(level).y);
        const GLsizei size = static_cast<GLsizei>(texture.size(level));

        if (compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D,
                                   static_cast<GLint>(level),
                                   Format.Internal,
                                   width,
                                   height,
                                   0,
                                   size,
                                   texture.data(0, 0, level));
        } else {
            glTexImage2D(GL_TEXTURE_2D,
                         static_cast<GLint>(level),
                         Format.Internal,
                         width,
                         height,
                         0,
                         Format.External,
                         Format.Type,
                         texture.data(0, 0, level));
        }
    }

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &textureID);
        std::cerr << "Failed to upload DDS texture: " << sourcePath << std::endl;
        return {};
    }

    UploadedDDS uploaded;
    uploaded.texture = textureID;
    uploaded.formatInfo = formatInfo;
    return uploaded;
}
}

GLuint LoadDDSTexture(const std::string& path) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    if (fullPath.extension().empty()) fullPath += ".dds";
    if (ToLower(fullPath.extension().string()) != ".dds") return 0;
    std::error_code ec;
    const fs::path canonicalPath = fs::weakly_canonical(fullPath, ec);
    if (!ec) fullPath = canonicalPath;
    ec.clear();
    const std::string key = fullPath.generic_string();
    const auto writeTime = fs::exists(fullPath, ec) ? fs::last_write_time(fullPath, ec) : fs::file_time_type{};
    auto cacheIt = g_ddsCache.find(key);
    if (cacheIt != g_ddsCache.end()) {
        if (!ec && cacheIt->second.texture != 0 && cacheIt->second.writeTime == writeTime) {
            ++cacheIt->second.refs;
            return cacheIt->second.texture;
        }
        if (cacheIt->second.texture != 0) {
            g_textureFormatInfo.erase(cacheIt->second.texture);
            glDeleteTextures(1, &cacheIt->second.texture);
        }
        g_ddsCache.erase(cacheIt);
    }

    const std::string ddsFilePath = fullPath.string();
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    const UploadedDDS uploaded = UploadDDS2D(texture, ddsFilePath);
    if (uploaded.texture == 0) return 0;

    g_ddsCache.emplace(key, CachedDDS{uploaded.texture, writeTime, 1, uploaded.formatInfo});
    g_textureFormatInfo[uploaded.texture] = uploaded.formatInfo;
    return uploaded.texture;
}

void ReleaseDDSTexture(GLuint texture) {
    if (texture == 0) return;
    for (auto it = g_ddsCache.begin(); it != g_ddsCache.end(); ++it) {
        if (it->second.texture != texture) continue;
        if (it->second.refs > 1) {
            --it->second.refs;
        } else {
            g_textureFormatInfo.erase(it->second.texture);
            glDeleteTextures(1, &it->second.texture);
            g_ddsCache.erase(it);
        }
        return;
    }
    g_textureFormatInfo.erase(texture);
    glDeleteTextures(1, &texture);
}

TextureFormatInfo GetTextureFormatInfo(GLuint texture) {
    if (texture == 0) return {};
    auto it = g_textureFormatInfo.find(texture);
    if (it != g_textureFormatInfo.end()) return it->second;

    GLint internalFormat = GL_NONE;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (internalFormat == GL_NONE) return {};
    return DetectTextureFormatInfo(static_cast<GLenum>(internalFormat), GL_NONE);
}

GLuint LoadDDS_Cubemap(const std::string& path) {
    fs::path base = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    if (base.extension() == ".dds" || base.extension() == ".DDS") base.replace_extension();

    const fs::path singlePath = base.string() + ".dds";
    gli::texture single = gli::load(singlePath.string());
    if (!single.empty() && single.faces() == 6) {
        gli::gl GL(gli::gl::PROFILE_GL33);
        const gli::gl::format Format = GL.translate(single.format(), single.swizzles());
        if (Format.Internal == GL_NONE) return 0;

        GLuint textureID = 0;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
        for (std::size_t face = 0; face < 6; ++face) {
            for (std::size_t level = 0; level < single.levels(); ++level) {
                const GLsizei width = static_cast<GLsizei>(single.extent(level).x);
                const GLsizei height = static_cast<GLsizei>(single.extent(level).y);
                const GLsizei size = static_cast<GLsizei>(single.size(level));
                const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
                if (compressed) {
                    glCompressedTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, size, single.data(face, 0, level));
                } else {
                    glTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, Format.External, Format.Type, single.data(face, 0, level));
                }
            }
        }
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &textureID);
            return 0;
        }
        return textureID;
    }

    const std::array<std::string, 6> suffixes = {"rt", "lf", "up", "dn", "bk", "ft"};
    std::array<gli::texture, 6> faces;
    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        fs::path facePath = base.parent_path() / (base.filename().string() + suffixes[i] + ".dds");
        faces[i] = gli::load(facePath.string());
        if (faces[i].empty() || faces[i].faces() != 1 || faces[i].levels() == 0) return 0;
    }

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(faces[0].format(), faces[0].swizzles());
    if (Format.Internal == GL_NONE) return 0;

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    for (std::size_t face = 0; face < 6; ++face) {
        if (faces[face].format() != faces[0].format() || faces[face].levels() != faces[0].levels()) {
            glDeleteTextures(1, &textureID);
            return 0;
        }
        for (std::size_t level = 0; level < faces[face].levels(); ++level) {
            const GLsizei width = static_cast<GLsizei>(faces[face].extent(level).x);
            const GLsizei height = static_cast<GLsizei>(faces[face].extent(level).y);
            const GLsizei size = static_cast<GLsizei>(faces[face].size(level));
            const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
            if (compressed) {
                glCompressedTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, size, faces[face].data(0, 0, level));
            } else {
                glTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, Format.External, Format.Type, faces[face].data(0, 0, level));
            }
        }
    }

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &textureID);
        return 0;
    }
    return textureID;
}

void Material::updateBuffers() {
    diffusePath[0] = '\0';
    normalPath[0] = '\0';
    glossPath[0] = '\0';
    lumaPath[0] = '\0';
    bumpPath[0] = '\0';
    detailPath[0] = '\0';
    CopyString(detailScale, sizeof(detailScale), "1 1");
    smoothness = 0.0f;
    reflectScale = 0.0f;
    refractScale = 0.0f;
    aberrationScale = 0.0f;
    reliefScale = 0.0f;
    textureScaleX = 1.0f;
    textureScaleY = 1.0f;
    swayHeight = 0;
    matTypeIndex = 0;

    for (const auto& p : params) {
        if (p.first == "diffuseMap") CopyString(diffusePath, sizeof(diffusePath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "normalMap") CopyString(normalPath, sizeof(normalPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "glossMap") CopyString(glossPath, sizeof(glossPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "LumaMap") CopyString(lumaPath, sizeof(lumaPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "bumpMap" || p.first == "bump") CopyString(bumpPath, sizeof(bumpPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "detailmap") CopyString(detailPath, sizeof(detailPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "detailScale") CopyString(detailScale, sizeof(detailScale), p.second);
        if (p.first == "smoothness") try { smoothness = std::stof(p.second); } catch(...) {}
        if (p.first == "reflectScale") try { reflectScale = std::stof(p.second); } catch(...) {}
        if (p.first == "refractScale") try { refractScale = std::stof(p.second); } catch(...) {}
        if (p.first == "aberrationScale") try { aberrationScale = std::stof(p.second); } catch(...) {}
        if (p.first == "reliefScale") try { reliefScale = std::stof(p.second); } catch(...) {}
        if (p.first == "textureScale") {
            std::istringstream scaleStream(p.second);
            if (!(scaleStream >> textureScaleX)) textureScaleX = 1.0f;
            if (!(scaleStream >> textureScaleY)) textureScaleY = textureScaleX;
        }
        if (p.first == "swayHeight") try { swayHeight = std::stoi(p.second); } catch(...) {}
        if (p.first == "material") {
            for (std::size_t i = 0; i < physicalMaterialTypes.size(); ++i) {
                if (p.second == physicalMaterialTypes[i]) {
                    matTypeIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }
}

void Material::syncParams() {
    auto setParam = [&](const std::string& key, const std::string& val) {
        for (auto& p : params) {
            if (p.first == key) {
                p.second = val;
                return;
            }
        }
        params.push_back({key, val});
    };

    auto setOptionalParam = [&](const std::string& key, const char* value) {
        if (value[0] != '\0') {
            setParam(key, value);
        } else {
            params.erase(std::remove_if(params.begin(), params.end(), [&](const auto& p) {
                return p.first == key;
            }), params.end());
        }
    };

    const std::string normalizedDiffuse = NormalizeMaterialTextureReference(diffusePath);
    const std::string normalizedNormal = NormalizeMaterialTextureReference(normalPath);
    const std::string normalizedGloss = NormalizeMaterialTextureReference(glossPath);
    const std::string normalizedLuma = NormalizeMaterialTextureReference(lumaPath);
    const std::string normalizedBump = NormalizeMaterialTextureReference(bumpPath);
    const std::string normalizedDetail = NormalizeMaterialTextureReference(detailPath);
    CopyString(diffusePath, sizeof(diffusePath), normalizedDiffuse);
    CopyString(normalPath, sizeof(normalPath), normalizedNormal);
    CopyString(glossPath, sizeof(glossPath), normalizedGloss);
    CopyString(lumaPath, sizeof(lumaPath), normalizedLuma);
    CopyString(bumpPath, sizeof(bumpPath), normalizedBump);
    CopyString(detailPath, sizeof(detailPath), normalizedDetail);
    setOptionalParam("diffuseMap", diffusePath);
    setOptionalParam("normalMap", normalPath);
    setOptionalParam("glossMap", glossPath);
    setOptionalParam("LumaMap", lumaPath);
    setOptionalParam("bumpMap", bumpPath);
    setOptionalParam("detailmap", detailPath);
    setParam("detailScale", detailScale);
    setParam("smoothness", std::to_string(smoothness));
    setParam("reflectScale", std::to_string(reflectScale));
    setParam("refractScale", std::to_string(refractScale));
    setParam("aberrationScale", std::to_string(aberrationScale));
    setParam("reliefScale", std::to_string(reliefScale));
    setParam("textureScale", std::to_string(textureScaleX) + " " + std::to_string(textureScaleY));
    setParam("swayHeight", std::to_string(swayHeight));
    
    if (matTypeIndex >= 0 && matTypeIndex < (int)physicalMaterialTypes.size()) {
        setParam("material", physicalMaterialTypes[matTypeIndex]);
    }
}

void Material::releaseTextures() {
    for (const auto& [key, id] : textures) {
        if (id != 0) ReleaseDDSTexture(id);
    }
    textures.clear();
}

void Material::loadTextures() {
    releaseTextures();
    for (const auto& p : params) {
        if (p.first == "diffuseMap") textures["diffuse"] = LoadTextureReference(p.second);
        if (p.first == "normalMap") textures["normal"] = LoadTextureReference(p.second);
        if (p.first == "glossMap") textures["gloss"] = LoadTextureReference(p.second);
        if (p.first == "LumaMap") textures["luma"] = LoadTextureReference(p.second);
        if (p.first == "bumpMap" || p.first == "bump") textures["bump"] = LoadTextureReference(p.second);
        if (p.first == "detailmap") textures["detail"] = LoadTextureReference(p.second);
    }
}

void PhysicalMaterialEntry::updateBuffers() {
    impactDecal[0] = '\0';
    impactPartsBuf[0] = '\0';
    impactSoundBuf[0] = '\0';
    stepSoundBuf[0] = '\0';

    auto joinVec = [](const std::vector<std::string>& vec) {
        std::string res;
        for (size_t i = 0; i < vec.size(); ++i) {
            res += vec[i];
            if (i + 1 < vec.size()) res += " ";
        }
        return res;
    };

    for (auto& [key, vals] : multiParams) {
        std::string joined = joinVec(vals);
        if (key == "impact_decal") CopyString(impactDecal, sizeof(impactDecal), vals.empty() ? "" : vals[0]);
        if (key == "impact_parts") CopyString(impactPartsBuf, sizeof(impactPartsBuf), joined);
        if (key == "impact_sound") CopyString(impactSoundBuf, sizeof(impactSoundBuf), joined);
        if (key == "step_sound") CopyString(stepSoundBuf, sizeof(stepSoundBuf), joined);
    }
}

void PhysicalMaterialEntry::syncParams() {
    auto splitToVec = [](const std::string& str) {
        std::vector<std::string> res;
        std::stringstream ss(str);
        std::string item;
        while (ss >> item) {
            res.push_back(item);
        }
        return res;
    };

    if (strlen(impactDecal) > 0) multiParams["impact_decal"] = {impactDecal};
    else multiParams.erase("impact_decal");

    std::vector<std::string> parts = splitToVec(impactPartsBuf);
    if (!parts.empty()) multiParams["impact_parts"] = parts;
    else multiParams.erase("impact_parts");

    std::vector<std::string> impSounds = splitToVec(impactSoundBuf);
    if (!impSounds.empty()) multiParams["impact_sound"] = impSounds;
    else multiParams.erase("impact_sound");

    std::vector<std::string> stepSounds = splitToVec(stepSoundBuf);
    if (!stepSounds.empty()) multiParams["step_sound"] = stepSounds;
    else multiParams.erase("step_sound");
}

void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats) {
    physMats.clear();
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return;

    std::string line, lastLine;
    PhysicalMaterialEntry* currentMat = nullptr;

    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (line.find('{') != std::string::npos) {
            physMats.emplace_back();
            currentMat = &physMats.back();
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                currentMat->name = lastLine.substr(n1 + 1, n2 - n1 - 1);
            } else {
                currentMat->name = "Unnamed";
            }
        } else if (line.find('}') != std::string::npos) {
            if (currentMat) currentMat->updateBuffers();
            currentMat = nullptr;
        } else if (currentMat) {
            size_t q1 = line.find('\"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('\"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string key = line.substr(q1 + 1, q2 - q1 - 1);

            std::vector<std::string> values;
            size_t searchPos = q2 + 1;
            while (true) {
                size_t v1 = line.find('\"', searchPos);
                if (v1 == std::string::npos) break;
                size_t v2 = line.find('\"', v1 + 1);
                if (v2 == std::string::npos) break;
                values.push_back(line.substr(v1 + 1, v2 - v1 - 1));
                searchPos = v2 + 1;
            }
            if (!values.empty()) {
                currentMat->multiParams[key] = values;
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }
}

void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) return;

    for (const auto& mat : physMats) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& [key, vals] : mat.multiParams) {
            file << "\t\"" << key << "\"";
            for (const auto& v : vals) {
                file << "\t\"" << v << "\"";
            }
            file << "\n";
        }
        file << "}\n";
    }
}

bool LoadAllMaterials(const std::string& path, std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return false;

    std::vector<Material> loadedMaterials;
    std::string line, lastLine;
    Material* currentMat = nullptr;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            loadedMaterials.emplace_back();
            currentMat = &loadedMaterials.back();
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                currentMat->name = lastLine.substr(n1 + 1, n2 - n1 - 1);
            } else {
                currentMat->name = "Unnamed";
            }
        } else if (line.find('}') != std::string::npos) {
            if (currentMat) currentMat->updateBuffers();
            currentMat = nullptr;
        } else if (currentMat) {
            size_t q1 = line.find('\"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('\"', q1 + 1);
            size_t q3 = line.find('\"', q2 + 1);
            if (q3 == std::string::npos) continue;
            size_t q4 = line.find('\"', q3 + 1);

            if (q1 != std::string::npos && q2 != std::string::npos && q3 != std::string::npos && q4 != std::string::npos) {
                currentMat->params.push_back({line.substr(q1 + 1, q2 - q1 - 1), line.substr(q3 + 1, q4 - q3 - 1)});
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }

    if (file.bad()) return false;
    for (auto& material : materials) material.releaseTextures();
    materials = std::move(loadedMaterials);
    return true;
}

void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) return;
    for (const auto& mat : materials) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& p : mat.params) {
            std::string value = p.second;
            if (p.first == "diffuseMap" || p.first == "normalMap" || p.first == "glossMap" ||
                p.first == "LumaMap" || p.first == "bumpMap" || p.first == "bump" || p.first == "detailmap") {
                value = NormalizeMaterialTextureReference(value);
                if (ToLower(fs::path(value).extension().string()) == ".dds") {
                    value = fs::path(value).replace_extension().string();
                }
            }
            file << "\t\"" << p.first << "\"\t\"" << value << "\"\n";
        }
        file << "}\n";
    }
}

namespace {
#pragma pack(push, 1)
struct WadHeaderRaw { char identification[4]; std::int32_t numLumps; std::int32_t infoTableOffset; };
struct WadLumpRaw { std::int32_t filePos; std::int32_t diskSize; std::int32_t size; std::uint8_t type; std::uint8_t compression; std::uint16_t padding; char name[16]; };
#pragma pack(pop)

bool ReadWadArchive(const std::string& relativePath, WadArchive& out) {
    fs::path fullPath = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : gameRootPath / relativePath;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file) return false;

    WadHeaderRaw header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || (std::strncmp(header.identification, "WAD2", 4) != 0 && std::strncmp(header.identification, "WAD3", 4) != 0)) return false;
    if (header.numLumps < 0 || header.numLumps > 1000000 || header.infoTableOffset < 0) return false;

    file.seekg(header.infoTableOffset, std::ios::beg);
    if (!file) return false;

    out = {};
    out.relativePath = relativePath;
    out.displayName = fullPath.filename().string();

    for (std::int32_t i = 0; i < header.numLumps; ++i) {
        WadLumpRaw lump{};
        file.read(reinterpret_cast<char*>(&lump), sizeof(lump));
        if (!file) return false;
        if (lump.type != 0x43 || lump.compression != 0) continue;
        if (lump.filePos < 0 || lump.diskSize < 40 || lump.diskSize > 256 * 1024 * 1024) continue;

        const auto directoryReturn = file.tellg();
        file.seekg(lump.filePos, std::ios::beg);
        if (!file) { file.clear(); file.seekg(directoryReturn); continue; }

        char textureName[16]{};
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int32_t offsets[4]{};
        file.read(textureName, sizeof(textureName));
        file.read(reinterpret_cast<char*>(&width), sizeof(width));
        file.read(reinterpret_cast<char*>(&height), sizeof(height));
        file.read(reinterpret_cast<char*>(offsets), sizeof(offsets));

        if (!file || width <= 0 || height <= 0 || width > 8192 || height > 8192 || offsets[0] < 40) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (pixelCount > 64u * 1024u * 1024u) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        WadTexture texture;
        texture.name.assign(textureName, strnlen(textureName, sizeof(textureName)));
        if (texture.name.empty()) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }
        texture.width = width;
        texture.height = height;

        const std::streamoff lumpStart = static_cast<std::streamoff>(lump.filePos);
        const std::streamoff pixelOffset = lumpStart + static_cast<std::streamoff>(offsets[0]);
        const std::streamoff paletteOffset = lumpStart + static_cast<std::streamoff>(offsets[3]) + static_cast<std::streamoff>((width / 8) * (height / 8));
        if (pixelOffset < 0 || paletteOffset < 0 || pixelOffset + static_cast<std::streamoff>(pixelCount) > lumpStart + lump.diskSize) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        texture.pixelOffset = static_cast<std::uint32_t>(pixelOffset);
        texture.paletteOffset = static_cast<std::uint32_t>(paletteOffset);
        out.textures.push_back(std::move(texture));
        file.clear();
        file.seekg(directoryReturn);
    }
    return true;
}

}

std::string ResolveWadTextureReference(const std::string& reference);

TexturePreviewInfo LoadTexturePreview(const std::string& reference) {
    TexturePreviewInfo info{};
    const WadArchive* wad = nullptr;
    const WadTexture* wadTexture = nullptr;
    std::string resolvedReference = reference;

    if (reference.rfind("wad://", 0) != 0) {
        const std::string wadReference = ResolveWadTextureReference(reference);
        if (!wadReference.empty()) resolvedReference = wadReference;
    }

    if (resolvedReference.rfind("wad://", 0) == 0) {
        const std::string encoded = resolvedReference.substr(6);
        const std::size_t separator = encoded.rfind('#');
        if (separator == std::string::npos) return info;
        const std::string wadPath = encoded.substr(0, separator);
        const std::string textureName = encoded.substr(separator + 1);
        for (const auto& candidate : g_wadArchives) {
            if (candidate.relativePath == wadPath) {
                wad = &candidate;
                break;
            }
        }
        if (!wad) return info;
        for (const auto& candidate : wad->textures) {
            if (candidate.name == textureName) {
                wadTexture = &candidate;
                break;
            }
        }
        if (!wadTexture || wadTexture->pixelOffset == 0 || wadTexture->paletteOffset == 0) return info;

        const fs::path wadFilePath = fs::path(wadPath).is_absolute() ? fs::path(wadPath) : (gameRootPath / wadPath);
        std::ifstream wadFile(wadFilePath, std::ios::binary);
        if (!wadFile) return info;

        const std::size_t pixelCount = static_cast<std::size_t>(wadTexture->width) * static_cast<std::size_t>(wadTexture->height);
        std::vector<unsigned char> indices(pixelCount);
        wadFile.seekg(static_cast<std::streamoff>(wadTexture->pixelOffset), std::ios::beg);
        wadFile.read(reinterpret_cast<char*>(indices.data()), static_cast<std::streamsize>(indices.size()));
        if (!wadFile) return info;

        wadFile.seekg(static_cast<std::streamoff>(wadTexture->paletteOffset), std::ios::beg);
        std::uint16_t paletteCount = 0;
        wadFile.read(reinterpret_cast<char*>(&paletteCount), sizeof(paletteCount));
        if (!wadFile || paletteCount < 256) return info;

        std::array<unsigned char, 256 * 3> palette{};
        wadFile.read(reinterpret_cast<char*>(palette.data()), static_cast<std::streamsize>(palette.size()));
        if (!wadFile) return info;

        std::vector<unsigned char> rgba(pixelCount * 4u);
        for (std::size_t px = 0; px < pixelCount; ++px) {
            const unsigned int index = indices[px];
            rgba[px * 4u + 0] = palette[index * 3u + 0];
            rgba[px * 4u + 1] = palette[index * 3u + 1];
            rgba[px * 4u + 2] = palette[index * 3u + 2];
            rgba[px * 4u + 3] = (!wadTexture->name.empty() && wadTexture->name[0] == '{' && index == 255u) ? 0u : 255u;
        }

        glGenTextures(1, &info.texture);
        glBindTexture(GL_TEXTURE_2D, info.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wadTexture->width, wadTexture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            ReleaseTexturePreview(info);
            return {};
        }
        info.width = wadTexture->width;
        info.height = wadTexture->height;
        info.valid = true;
        g_textureFormatInfo[info.texture] = TextureFormatInfo{true, false, false, false, 4};
        return info;
    }

    const fs::path referencePath(reference);
    const std::string extension = ToLower(referencePath.extension().string());
    if (extension == ".png" || extension == ".tga" || extension.empty() || extension != ".dds") return info;

    const GLuint texture = LoadDDSTexture(reference);
    if (!texture) return info;
    GLint width = 0, height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glBindTexture(GL_TEXTURE_2D, 0);
    info.texture = texture;
    info.width = std::max(0, width);
    info.height = std::max(0, height);
    info.valid = width > 0 && height > 0;
    info.cachedDDS = true;
    if (!info.valid) ReleaseTexturePreview(info);
    return info;
}

std::string ResolveWadTextureReference(const std::string& reference) {
    std::string name = reference;
    if (name.rfind("wad://", 0) == 0) {
        const std::size_t separator = name.rfind('#');
        if (separator == std::string::npos) return {};
        name = name.substr(separator + 1);
    } else if (name.rfind("wad:/", 0) == 0) {
        name = name.substr(5);
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
    }

    const std::string extension = ToLower(fs::path(name).extension().string());
    if (extension == ".png" || extension == ".tga" || extension == ".dds") name = fs::path(name).stem().string();
    if (name.empty()) return {};

    for (const auto& wad : g_wadArchives) {
        for (const auto& texture : wad.textures) {
            std::string textureName = texture.name;
            const std::string textureExtension = ToLower(fs::path(textureName).extension().string());
            if (textureExtension == ".png" || textureExtension == ".tga" || textureExtension == ".dds") textureName = fs::path(textureName).stem().string();
            if (ToLower(textureName) != ToLower(name)) continue;
            return MakeWadTextureReference(wad, texture);
        }
    }
    return {};
}

GLuint LoadTextureReference(const std::string& reference) {
    const fs::path referencePath(reference);
    const std::string extension = ToLower(referencePath.extension().string());
    if (extension == ".png" || extension == ".tga") return 0;

    const bool isWadReference = reference.rfind("wad://", 0) == 0 || reference.rfind("wad:/", 0) == 0;
    if (!isWadReference && extension == ".dds") return LoadDDSTexture(reference);

    const std::string wadReference = ResolveWadTextureReference(reference);
    if (!wadReference.empty()) {
        TexturePreviewInfo preview = LoadTexturePreview(wadReference);
        const GLuint texture = preview.texture;
        preview.texture = 0;
        ReleaseTexturePreview(preview);
        return texture;
    }

    if (isWadReference || !extension.empty()) return 0;
    return LoadDDSTexture(reference);
}

void ReleaseTexturePreview(TexturePreviewInfo& preview) {
    if (preview.texture) {
        if (preview.cachedDDS) ReleaseDDSTexture(preview.texture);
        else glDeleteTextures(1, &preview.texture);
    }
    preview = {};
}

const std::vector<WadArchive>& GetWadArchives() { return g_wadArchives; }

bool AddWadArchive(const std::string& relativePath) {
    const auto duplicate = std::find_if(g_wadArchives.begin(), g_wadArchives.end(), [&](const WadArchive& wad) { return wad.relativePath == relativePath; });
    if (duplicate != g_wadArchives.end()) return true;
    WadArchive wad;
    if (!ReadWadArchive(relativePath, wad)) return false;
    g_wadArchives.push_back(std::move(wad));
    return true;
}

void LoadWadArchives(const std::vector<std::string>& paths) {
    ClearWadArchives();
    for (const auto& path : paths) AddWadArchive(path);
}

void ScanAndLoadAllWads() {
    ClearWadArchives();
    if (gameRootPath.empty() || !fs::exists(gameRootPath)) return;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(gameRootPath, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".wad" && it->path().extension() != ".WAD") continue;
        std::error_code rec;
        const std::string relative = fs::relative(it->path(), gameRootPath, rec).generic_string();
        if (!rec) AddWadArchive(relative);
    }
}

void ClearWadArchives() { g_wadArchives.clear(); }

std::vector<std::string> GetLoadedWadPaths() {
    std::vector<std::string> paths;
    paths.reserve(g_wadArchives.size());
    for (const auto& wad : g_wadArchives) paths.push_back(wad.relativePath);
    return paths;
}

std::string MakeWadTextureReference(const WadArchive& wad, const WadTexture& texture) {
    return std::string("wad://") + wad.relativePath + "#" + texture.name;
}


namespace {
#pragma pack(push, 1)
struct DDS_PIXELFORMAT_RAW {
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t fourCC;
    std::uint32_t rgbBitCount;
    std::uint32_t rMask;
    std::uint32_t gMask;
    std::uint32_t bMask;
    std::uint32_t aMask;
};

struct DDS_HEADER_RAW {
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t height;
    std::uint32_t width;
    std::uint32_t pitchOrLinearSize;
    std::uint32_t depth;
    std::uint32_t mipMapCount;
    std::uint32_t reserved1[11];
    DDS_PIXELFORMAT_RAW ddspf;
    std::uint32_t caps;
    std::uint32_t caps2;
    std::uint32_t caps3;
    std::uint32_t caps4;
    std::uint32_t reserved2;
};

struct DDS_HEADER_DX10_RAW {
    std::uint32_t dxgiFormat;
    std::uint32_t resourceDimension;
    std::uint32_t miscFlag;
    std::uint32_t arraySize;
    std::uint32_t miscFlags2;
};
#pragma pack(pop)

constexpr std::uint32_t DDS_MAGIC = 0x20534444u;
constexpr std::uint32_t DDSD_CAPS = 0x1u;
constexpr std::uint32_t DDSD_HEIGHT = 0x2u;
constexpr std::uint32_t DDSD_WIDTH = 0x4u;
constexpr std::uint32_t DDSD_PIXELFORMAT = 0x1000u;
constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x20000u;
constexpr std::uint32_t DDSD_LINEARSIZE = 0x80000u;
constexpr std::uint32_t DDSCAPS_COMPLEX = 0x8u;
constexpr std::uint32_t DDSCAPS_TEXTURE = 0x1000u;
constexpr std::uint32_t DDSCAPS_MIPMAP = 0x400000u;
constexpr std::uint32_t DDPF_FOURCC = 0x4u;
constexpr std::uint32_t D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3u;
constexpr std::uint32_t DXGI_FORMAT_BC4_UNORM = 80u;
constexpr std::uint32_t DXGI_FORMAT_BC5_UNORM = 83u;
constexpr std::uint32_t DXGI_FORMAT_BC7_UNORM = 98u;
constexpr GLenum GL_COMPRESSED_RGBA_BPTC_UNORM_VALUE = 0x8E8Cu;
constexpr GLenum GL_COMPRESSED_IMAGE_SIZE_VALUE = 0x86A0u;

std::uint32_t FourCC(const char a, const char b, const char c, const char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

void BuildLuma(const TexturePixels& source, std::vector<std::uint8_t>& luma) {
    const std::size_t count = static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height);
    luma.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float r = source.rgba[i * 4u + 0u] / 255.0f;
        const float g = source.rgba[i * 4u + 1u] / 255.0f;
        const float b = source.rgba[i * 4u + 2u] / 255.0f;
        const float value = std::clamp(0.2126f * r + 0.7152f * g + 0.0722f * b, 0.0f, 1.0f);
        luma[i] = static_cast<std::uint8_t>(std::lround(value * 255.0f));
    }
}

void BuildHeight(const TexturePixels& source, int channel, bool invert, std::vector<std::uint8_t>& height) {
    const std::size_t count = static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height);
    height.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        float value = 0.0f;
        if (channel == 0) {
            const float r = source.rgba[i * 4u + 0u] / 255.0f;
            const float g = source.rgba[i * 4u + 1u] / 255.0f;
            const float b = source.rgba[i * 4u + 2u] / 255.0f;
            value = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        } else {
            value = source.rgba[i * 4u + static_cast<std::size_t>(channel - 1)] / 255.0f;
        }
        if (invert) value = 1.0f - value;
        height[i] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }
}

void ApplyBumpHeightSettings(std::vector<std::uint8_t>& height, float contrast, float brightness, bool normalize) {
    if (normalize && !height.empty()) {
        const auto minmax = std::minmax_element(height.begin(), height.end());
        const int minValue = *minmax.first;
        const int maxValue = *minmax.second;
        if (maxValue > minValue) {
            const float scale = 255.0f / static_cast<float>(maxValue - minValue);
            for (std::uint8_t& value : height) {
                value = static_cast<std::uint8_t>(std::clamp((static_cast<int>(value) - minValue) * scale, 0.0f, 255.0f));
            }
        }
    }

    const float safeContrast = std::max(0.0f, contrast);
    if (safeContrast == 1.0f && brightness == 0.0f) return;
    for (std::uint8_t& value : height) {
        float adjusted = value / 255.0f;
        adjusted = (adjusted - 0.5f) * safeContrast + 0.5f + brightness;
        value = static_cast<std::uint8_t>(std::clamp(adjusted, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

void DownsampleGray(const std::vector<std::uint8_t>& src, int width, int height, std::vector<std::uint8_t>& dst, int& outWidth, int& outHeight) {
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    dst.resize(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight));
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = std::min(width - 1, x * 2);
            const int x1 = std::min(width - 1, x0 + 1);
            const int y0 = std::min(height - 1, y * 2);
            const int y1 = std::min(height - 1, y0 + 1);
            const unsigned value = static_cast<unsigned>(src[static_cast<std::size_t>(y0) * width + x0]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y0) * width + x1]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y1) * width + x0]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y1) * width + x1]);
            dst[static_cast<std::size_t>(y) * outWidth + x] = static_cast<std::uint8_t>((value + 2u) / 4u);
        }
    }
}

TexturePreviewInfo UploadCreatorPreviewRGBA(const std::vector<std::uint8_t>& rgba, int width, int height, bool mipmaps) {
    TexturePreviewInfo preview{};
    if (rgba.empty() || width <= 0 || height <= 0) return preview;

    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &preview.texture);
    glBindTexture(GL_TEXTURE_2D, preview.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &preview.texture);
        return {};
    }

    preview.width = width;
    preview.height = height;
    preview.valid = true;
    preview.cachedDDS = false;
    return preview;
}

void DownsampleRGBA(const std::vector<std::uint8_t>& src, int width, int height, std::vector<std::uint8_t>& dst, int& outWidth, int& outHeight) {
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    dst.resize(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4u);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = std::min(width - 1, x * 2);
            const int x1 = std::min(width - 1, x0 + 1);
            const int y0 = std::min(height - 1, y * 2);
            const int y1 = std::min(height - 1, y0 + 1);
            const std::size_t out = (static_cast<std::size_t>(y) * outWidth + x) * 4u;
            for (int c = 0; c < 4; ++c) {
                const unsigned value = src[(static_cast<std::size_t>(y0) * width + x0) * 4u + c] +
                                       src[(static_cast<std::size_t>(y0) * width + x1) * 4u + c] +
                                       src[(static_cast<std::size_t>(y1) * width + x0) * 4u + c] +
                                       src[(static_cast<std::size_t>(y1) * width + x1) * 4u + c];
                dst[out + c] = static_cast<std::uint8_t>((value + 2u) / 4u);
            }
        }
    }
}

bool CompressBC7(const std::vector<std::vector<std::uint8_t>>& rgbaMips, const std::vector<std::pair<int, int>>& sizes, std::vector<std::vector<std::uint8_t>>& blocks) {
    if (rgbaMips.empty() || rgbaMips.size() != sizes.size()) return false;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t level = 0; level < rgbaMips.size(); ++level) {
        const int width = sizes[level].first;
        const int height = sizes[level].second;
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_COMPRESSED_RGBA_BPTC_UNORM_VALUE, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaMips[level].data());
        GLint size = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, static_cast<GLint>(level), GL_COMPRESSED_IMAGE_SIZE_VALUE, &size);
        if (size <= 0) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &texture);
            return false;
        }
        blocks.emplace_back(static_cast<std::size_t>(size));
        glGetCompressedTexImage(GL_TEXTURE_2D, static_cast<GLint>(level), blocks.back().data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
    return glGetError() == GL_NO_ERROR;
}

TexturePreviewInfo GenerateNormalMapPreviewTextureImpl(const std::string& source, float strength, bool flipX, bool flipY, bool fullZRange, int heightChannel, bool invertHeight, bool mipmaps) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};

    std::vector<std::uint8_t> heightData;
    BuildHeight(pixels, heightChannel, invertHeight, heightData);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixels.width) * static_cast<std::size_t>(pixels.height) * 4u);
    const float scale = std::max(0.0f, strength);

    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            auto sample = [&](int sx, int sy) -> float {
                sx = std::clamp(sx, 0, pixels.width - 1);
                sy = std::clamp(sy, 0, pixels.height - 1);
                return heightData[static_cast<std::size_t>(sy) * pixels.width + sx] / 255.0f;
            };
            const float dx = (sample(x + 1, y - 1) + 2.0f * sample(x + 1, y) + sample(x + 1, y + 1)) -
                             (sample(x - 1, y - 1) + 2.0f * sample(x - 1, y) + sample(x - 1, y + 1));
            const float dy = (sample(x - 1, y + 1) + 2.0f * sample(x, y + 1) + sample(x + 1, y + 1)) -
                             (sample(x - 1, y - 1) + 2.0f * sample(x, y - 1) + sample(x + 1, y - 1));
            float nx = -dx * scale;
            float ny = -dy * scale;
            float nz = 1.0f;
            const float invLength = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= invLength;
            ny *= invLength;
            if (flipX) nx = -nx;
            if (flipY) ny = -ny;
            const std::size_t index = (static_cast<std::size_t>(y) * pixels.width + x) * 4u;
            rgba[index + 0] = static_cast<std::uint8_t>(std::clamp(nx * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[index + 1] = static_cast<std::uint8_t>(std::clamp(ny * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
            const float encodedZ = fullZRange ? nz : (nz * 0.5f + 0.5f);
            rgba[index + 2] = static_cast<std::uint8_t>(std::clamp(encodedZ, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[index + 3] = 255;
        }
    }

    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, mipmaps);
}

TexturePreviewInfo GenerateBumpMapPreviewTextureImpl(const std::string& source, int heightChannel, bool invert, float contrast, float brightness, bool normalize, bool mipmaps) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};

    std::vector<std::uint8_t> height;
    BuildHeight(pixels, heightChannel, invert, height);
    ApplyBumpHeightSettings(height, contrast, brightness, normalize);

    std::vector<std::uint8_t> rgba(height.size() * 4u);
    for (std::size_t i = 0; i < height.size(); ++i) {
        const std::uint8_t v = height[i];
        rgba[i * 4u + 0u] = v;
        rgba[i * 4u + 1u] = v;
        rgba[i * 4u + 2u] = v;
        rgba[i * 4u + 3u] = 255;
    }
    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, mipmaps);
}

TexturePreviewInfo GenerateGlossMapPreviewTextureImpl(const std::string& source, float contrast, float brightness, float power, bool invert, int metric, float lowerThreshold, float upperThreshold, bool normalize, bool mipmaps) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};

    std::vector<std::uint8_t> luma;
    BuildLuma(pixels, luma);
    const float safeContrast = std::max(0.0f, contrast);
    const float safePower = std::max(0.01f, power);
    std::vector<std::uint8_t> rgba(luma.size() * 4u);

    const float lower = std::clamp(std::min(lowerThreshold, upperThreshold), 0.0f, 1.0f);
    const float upper = std::clamp(std::max(lowerThreshold, upperThreshold), 0.0f, 1.0f);
    for (std::size_t i = 0; i < luma.size(); ++i) {
        const float r = pixels.rgba[i * 4u + 0u] / 255.0f;
        const float g = pixels.rgba[i * 4u + 1u] / 255.0f;
        const float b = pixels.rgba[i * 4u + 2u] / 255.0f;
        const float y = luma[i] / 255.0f;
        const float dr = std::fabs(r - y);
        const float dg = std::fabs(g - y);
        const float db = std::fabs(b - y);
        float distance = metric == 0 ? (dr + dg + db) / 3.0f : (metric == 1 ? std::max({dr, dg, db}) : std::sqrt(dr * dr + dg * dg + db * db));
        if (normalize) distance = std::clamp((distance - lower) / std::max(0.0001f, upper - lower), 0.0f, 1.0f);
        else distance = std::clamp(distance, lower, upper);
        float value = normalize ? distance : (upper > lower ? (distance - lower) / (upper - lower) : 0.0f);
        if (invert) value = 1.0f - value;
        value = (value - 0.5f) * safeContrast + 0.5f + brightness;
        value = std::clamp(value, 0.0f, 1.0f);
        value = std::pow(value, safePower);
        const std::uint8_t v = static_cast<std::uint8_t>(value * 255.0f + 0.5f);
        rgba[i * 4u + 0u] = v;
        rgba[i * 4u + 1u] = v;
        rgba[i * 4u + 2u] = v;
        rgba[i * 4u + 3u] = 255;
    }

    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, mipmaps);
}

void EncodeBC4Block(const std::uint8_t* values, int stride, std::uint8_t out[8]) {
    std::uint8_t maxValue = 0;
    std::uint8_t minValue = 255;
    std::uint8_t samples[16]{};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::uint8_t value = values[y * stride + x];
            samples[y * 4 + x] = value;
            maxValue = std::max(maxValue, value);
            minValue = std::min(minValue, value);
        }
    }

    out[0] = maxValue;
    out[1] = minValue;
    std::uint8_t palette[8]{};
    palette[0] = maxValue;
    palette[1] = minValue;
    for (int i = 2; i < 8; ++i) {
        palette[i] = static_cast<std::uint8_t>(((8 - i) * static_cast<unsigned>(maxValue) + (i - 1) * static_cast<unsigned>(minValue) + 3u) / 7u);
    }

    std::uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int bestDistance = 1 << 30;
        for (int j = 0; j < 8; ++j) {
            const int distance = std::abs(static_cast<int>(samples[i]) - static_cast<int>(palette[j]));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = j;
            }
        }
        indices |= static_cast<std::uint64_t>(best) << (3 * i);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<std::uint8_t>((indices >> (8 * i)) & 0xFFu);
}

void EncodeBC4(const std::vector<std::uint8_t>& image, int width, int height, std::vector<std::uint8_t>& blocks) {
    const int blockWidth = std::max(1, (width + 3) / 4);
    const int blockHeight = std::max(1, (height + 3) / 4);
    blocks.resize(static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight) * 8u);
    std::uint8_t block[16]{};
    std::uint8_t encoded[8]{};
    for (int by = 0; by < blockHeight; ++by) {
        for (int bx = 0; bx < blockWidth; ++bx) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(width - 1, bx * 4 + x);
                    const int sy = std::min(height - 1, by * 4 + y);
                    block[y * 4 + x] = image[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            EncodeBC4Block(block, 4, encoded);
            std::memcpy(blocks.data() + (static_cast<std::size_t>(by) * blockWidth + bx) * 8u, encoded, 8u);
        }
    }
}

void EncodeBC5(const std::vector<std::uint8_t>& red, const std::vector<std::uint8_t>& green, int width, int height, std::vector<std::uint8_t>& blocks) {
    const int blockWidth = std::max(1, (width + 3) / 4);
    const int blockHeight = std::max(1, (height + 3) / 4);
    blocks.resize(static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight) * 16u);
    std::uint8_t blockR[16]{}, blockG[16]{}, encodedR[8]{}, encodedG[8]{};
    for (int by = 0; by < blockHeight; ++by) {
        for (int bx = 0; bx < blockWidth; ++bx) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(width - 1, bx * 4 + x);
                    const int sy = std::min(height - 1, by * 4 + y);
                    blockR[y * 4 + x] = red[static_cast<std::size_t>(sy) * width + sx];
                    blockG[y * 4 + x] = green[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            EncodeBC4Block(blockR, 4, encodedR);
            EncodeBC4Block(blockG, 4, encodedG);
            const std::size_t offset = (static_cast<std::size_t>(by) * blockWidth + bx) * 16u;
            std::memcpy(blocks.data() + offset, encodedR, 8u);
            std::memcpy(blocks.data() + offset + 8u, encodedG, 8u);
        }
    }
}

bool WriteDDS(const std::string& outputPath, int width, int height, const std::vector<std::vector<std::uint8_t>>& mipData, std::uint32_t dxgiFormat, int blockBytes, std::uint32_t legacyFourCC = 0u) {
    (void)blockBytes;
    if (width <= 0 || height <= 0 || mipData.empty()) return false;
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const std::uint32_t mipCount = static_cast<std::uint32_t>(mipData.size());
    const bool useLegacyHeader = legacyFourCC != 0u;
    DDS_HEADER_RAW header{};
    header.size = 124u;
    header.flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE;
    if (mipCount > 1) header.flags |= DDSD_MIPMAPCOUNT;
    header.height = static_cast<std::uint32_t>(height);
    header.width = static_cast<std::uint32_t>(width);
    header.pitchOrLinearSize = static_cast<std::uint32_t>(mipData.front().size());
    header.mipMapCount = mipCount;
    header.ddspf.size = 32u;
    header.ddspf.flags = DDPF_FOURCC;
    header.ddspf.fourCC = useLegacyHeader ? legacyFourCC : FourCC('D', 'X', '1', '0');
    header.caps = DDSCAPS_TEXTURE;
    if (mipCount > 1) header.caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;

    DDS_HEADER_DX10_RAW dx10{};
    dx10.dxgiFormat = dxgiFormat;
    dx10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
    dx10.arraySize = 1u;

    file.write(reinterpret_cast<const char*>(&DDS_MAGIC), sizeof(DDS_MAGIC));
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!useLegacyHeader) file.write(reinterpret_cast<const char*>(&dx10), sizeof(dx10));
    for (const auto& mip : mipData) file.write(reinterpret_cast<const char*>(mip.data()), static_cast<std::streamsize>(mip.size()));
    return static_cast<bool>(file);
}

bool LoadWadPixels(const std::string& reference, TexturePixels& pixels) {
    std::string encoded = reference;
    if (encoded.rfind("wad://", 0) == 0) encoded = encoded.substr(6);
    else if (encoded.rfind("wad:/", 0) == 0) encoded = encoded.substr(5);
    else return false;
    const std::size_t separator = encoded.rfind('#');
    if (separator == std::string::npos) {
        const std::string name = encoded;
        const std::string resolved = ResolveWadTextureReference(name);
        if (resolved.empty()) return false;
        return LoadWadPixels(resolved, pixels);
    }
    const std::string wadPath = encoded.substr(0, separator);
    const std::string textureName = encoded.substr(separator + 1);
    const WadArchive* wad = nullptr;
    for (const auto& candidate : g_wadArchives) {
        if (ToLower(candidate.relativePath) == ToLower(wadPath)) {
            wad = &candidate;
            break;
        }
    }
    if (!wad) return false;
    const WadTexture* texture = nullptr;
    for (const auto& candidate : wad->textures) {
        if (ToLower(candidate.name) == ToLower(textureName)) {
            texture = &candidate;
            break;
        }
    }
    if (!texture) return false;
    const fs::path path = fs::path(wadPath).is_absolute() ? fs::path(wadPath) : gameRootPath / wadPath;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::size_t count = static_cast<std::size_t>(texture->width) * static_cast<std::size_t>(texture->height);
    std::vector<std::uint8_t> indices(count);
    file.seekg(static_cast<std::streamoff>(texture->pixelOffset));
    file.read(reinterpret_cast<char*>(indices.data()), static_cast<std::streamsize>(indices.size()));
    if (!file) return false;
    file.seekg(static_cast<std::streamoff>(texture->paletteOffset));
    std::uint16_t paletteCount = 0;
    file.read(reinterpret_cast<char*>(&paletteCount), sizeof(paletteCount));
    if (!file || paletteCount < 256) return false;
    std::array<std::uint8_t, 256 * 3> palette{};
    file.read(reinterpret_cast<char*>(palette.data()), static_cast<std::streamsize>(palette.size()));
    if (!file) return false;

    pixels.width = texture->width;
    pixels.height = texture->height;
    pixels.rgba.resize(count * 4u);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t paletteIndex = static_cast<std::size_t>(indices[i]) * 3u;
        pixels.rgba[i * 4u + 0u] = palette[paletteIndex + 0u];
        pixels.rgba[i * 4u + 1u] = palette[paletteIndex + 1u];
        pixels.rgba[i * 4u + 2u] = palette[paletteIndex + 2u];
        pixels.rgba[i * 4u + 3u] = (!texture->name.empty() && texture->name[0] == '{' && indices[i] == 255u) ? 0u : 255u;
    }
    return true;
}
}

TexturePreviewInfo GenerateNormalMapPreviewTexture(const std::string& source, float strength, bool flipX, bool flipY, bool fullZRange, int heightChannel, bool invertHeight, bool mipmaps) {
    return GenerateNormalMapPreviewTextureImpl(source, strength, flipX, flipY, fullZRange, heightChannel, invertHeight, mipmaps);
}

TexturePreviewInfo GenerateGlossMapPreviewTexture(const std::string& source, float contrast, float brightness, float power, bool invert, int metric, float lowerThreshold, float upperThreshold, bool normalize, bool mipmaps) {
    return GenerateGlossMapPreviewTextureImpl(source, contrast, brightness, power, invert, metric, lowerThreshold, upperThreshold, normalize, mipmaps);
}

TexturePreviewInfo GenerateBumpMapPreviewTexture(const std::string& source, int heightChannel, bool invert, float contrast, float brightness, bool normalize, bool mipmaps) {
    return GenerateBumpMapPreviewTextureImpl(source, heightChannel, invert, contrast, brightness, normalize, mipmaps);
}

bool LoadTexturePixels(const std::string& reference, TexturePixels& pixels) {
    pixels = {};
    if (reference.empty()) return false;
    std::string resolved = reference;
    if (resolved.rfind("wad://", 0) == 0 || resolved.rfind("wad:/", 0) == 0) return LoadWadPixels(resolved, pixels);
    const std::string wadReference = ResolveWadTextureReference(resolved);
    if (!wadReference.empty()) return LoadWadPixels(wadReference, pixels);

    fs::path path = fs::path(resolved).is_absolute() ? fs::path(resolved) : gameRootPath / resolved;
    const std::string extension = ToLower(path.extension().string());
    if (extension == ".png" || extension == ".tga") {
        unsigned char* data = nullptr;
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!LoadRasterImage(path.string(), data, width, height, channels)) return false;
        pixels.width = width;
        pixels.height = height;
        pixels.rgba.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        FreeRasterImage(data);
        return true;
    }
    if (extension != ".dds") return false;

    const GLuint texture = LoadDDSTexture(path.string());
    if (!texture) return false;
    GLint width = 0;
    GLint height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width > 0 && height > 0) {
        pixels.width = width;
        pixels.height = height;
        pixels.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    ReleaseDDSTexture(texture);
    return pixels.width > 0 && pixels.height > 0 && !pixels.rgba.empty() && glGetError() == GL_NO_ERROR;
}

bool GenerateNormalMapDDS(const std::string& source, const std::string& outputPath, float strength, bool flipX, bool flipY, bool fullZRange, int heightChannel, bool invertHeight, bool mipmaps, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;
    std::vector<std::uint8_t> height;
    BuildHeight(pixels, heightChannel, invertHeight, height);
    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int heightSize = pixels.height;
    std::vector<std::uint8_t> currentHeight = std::move(height);
    const float scale = std::max(0.0f, strength);

    while (true) {
        std::vector<std::uint8_t> normalX(static_cast<std::size_t>(width) * static_cast<std::size_t>(heightSize));
        std::vector<std::uint8_t> normalY(normalX.size());
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(heightSize) * 4u);
        for (int y = 0; y < heightSize; ++y) {
            for (int x = 0; x < width; ++x) {
                auto sample = [&](int sx, int sy) -> float {
                    sx = std::clamp(sx, 0, width - 1);
                    sy = std::clamp(sy, 0, heightSize - 1);
                    return currentHeight[static_cast<std::size_t>(sy) * width + sx] / 255.0f;
                };
                const float dx = (sample(x + 1, y - 1) + 2.0f * sample(x + 1, y) + sample(x + 1, y + 1)) - (sample(x - 1, y - 1) + 2.0f * sample(x - 1, y) + sample(x - 1, y + 1));
                const float dy = (sample(x - 1, y + 1) + 2.0f * sample(x, y + 1) + sample(x + 1, y + 1)) - (sample(x - 1, y - 1) + 2.0f * sample(x, y - 1) + sample(x + 1, y - 1));
                float nx = -dx * scale;
                float ny = -dy * scale;
                float nz = 1.0f;
                const float invLength = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLength;
                ny *= invLength;
                if (flipX) nx = -nx;
                if (flipY) ny = -ny;
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const std::uint8_t r = static_cast<std::uint8_t>(std::clamp(nx * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                const std::uint8_t g = static_cast<std::uint8_t>(std::clamp(ny * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                const std::uint8_t b = static_cast<std::uint8_t>(std::clamp(fullZRange ? nz : (nz * 0.5f + 0.5f), 0.0f, 1.0f) * 255.0f + 0.5f);
                normalX[index] = r;
                normalY[index] = g;
                rgba[index * 4u + 0u] = r;
                rgba[index * 4u + 1u] = g;
                rgba[index * 4u + 2u] = b;
                rgba[index * 4u + 3u] = 255;
            }
        }
        if (format == 1) {
            std::vector<std::uint8_t> blocks;
            EncodeBC5(normalX, normalY, width, heightSize, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            rgbaMips.push_back(std::move(rgba));
        }
        sizes.emplace_back(width, heightSize);
        if (!mipmaps || (width == 1 && heightSize == 1)) break;
        std::vector<std::uint8_t> nextHeight;
        int nextWidth = 1;
        int nextHeightSize = 1;
        DownsampleGray(currentHeight, width, heightSize, nextHeight, nextWidth, nextHeightSize);
        currentHeight = std::move(nextHeight);
        width = nextWidth;
        heightSize = nextHeightSize;
    }

    if (format == 2) {
        if (!CompressBC7(rgbaMips, sizes, mipData)) return false;
    } else if (format != 1) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC5_UNORM, 16, format == 1 ? FourCC('A', 'T', 'I', '2') : 0u);
}

bool GenerateGlossMapDDS(const std::string& source, const std::string& outputPath, float contrast, float brightness, float power, bool invert, int metric, float lowerThreshold, float upperThreshold, bool normalize, bool mipmaps, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;
    std::vector<std::uint8_t> luma;
    BuildLuma(pixels, luma);
    const float safeContrast = std::max(0.0f, contrast);
    const float safePower = std::max(0.01f, power);
    const float lower = std::clamp(std::min(lowerThreshold, upperThreshold), 0.0f, 1.0f);
    const float upper = std::clamp(std::max(lowerThreshold, upperThreshold), 0.0f, 1.0f);
    std::vector<std::uint8_t> current;
    current.resize(luma.size());
    for (std::size_t i = 0; i < luma.size(); ++i) {
        const float r = pixels.rgba[i * 4u + 0u] / 255.0f;
        const float g = pixels.rgba[i * 4u + 1u] / 255.0f;
        const float b = pixels.rgba[i * 4u + 2u] / 255.0f;
        const float y = luma[i] / 255.0f;
        const float dr = std::fabs(r - y);
        const float dg = std::fabs(g - y);
        const float db = std::fabs(b - y);
        float distance = metric == 0 ? (dr + dg + db) / 3.0f : (metric == 1 ? std::max({dr, dg, db}) : std::sqrt(dr * dr + dg * dg + db * db));
        float value = normalize ? std::clamp((distance - lower) / std::max(0.0001f, upper - lower), 0.0f, 1.0f) : std::clamp(distance, 0.0f, 1.0f);
        if (invert) value = 1.0f - value;
        value = std::clamp((value - 0.5f) * safeContrast + 0.5f + brightness, 0.0f, 1.0f);
        value = std::pow(value, safePower);
        current[i] = static_cast<std::uint8_t>(value * 255.0f + 0.5f);
    }

    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int height = pixels.height;
    while (true) {
        if (format == 2) {
            std::vector<std::uint8_t> rgba(current.size() * 4u);
            for (std::size_t i = 0; i < current.size(); ++i) {
                rgba[i * 4u + 0u] = current[i];
                rgba[i * 4u + 1u] = current[i];
                rgba[i * 4u + 2u] = current[i];
                rgba[i * 4u + 3u] = 255;
            }
            rgbaMips.push_back(std::move(rgba));
        } else if (format == 0) {
            std::vector<std::uint8_t> blocks;
            EncodeBC4(current, width, height, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            return false;
        }
        sizes.emplace_back(width, height);
        if (!mipmaps || (width == 1 && height == 1)) break;
        if (format == 2) {
            std::vector<std::uint8_t> next;
            int nextWidth = 1;
            int nextHeight = 1;
            DownsampleRGBA(rgbaMips.back(), width, height, next, nextWidth, nextHeight);
            current.resize(static_cast<std::size_t>(nextWidth) * static_cast<std::size_t>(nextHeight));
            for (std::size_t i = 0; i < current.size(); ++i) current[i] = next[i * 4u];
            width = nextWidth;
            height = nextHeight;
        } else {
            std::vector<std::uint8_t> next;
            int nextWidth = 1;
            int nextHeight = 1;
            DownsampleGray(current, width, height, next, nextWidth, nextHeight);
            current = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }
    }
    if (format == 2) {
        if (!CompressBC7(rgbaMips, sizes, mipData)) return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC4_UNORM, format == 2 ? 16 : 8, format == 0 ? FourCC('A', 'T', 'I', '1') : 0u);
}


bool GenerateBumpMapDDS(const std::string& source, const std::string& outputPath, int heightChannel, bool invert, float contrast, float brightness, bool normalize, bool mipmaps, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;

    std::vector<std::uint8_t> current;
    BuildHeight(pixels, heightChannel, invert, current);
    ApplyBumpHeightSettings(current, contrast, brightness, normalize);

    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int height = pixels.height;
    while (true) {
        if (format == 2) {
            std::vector<std::uint8_t> rgba(current.size() * 4u);
            for (std::size_t i = 0; i < current.size(); ++i) {
                rgba[i * 4u + 0u] = current[i];
                rgba[i * 4u + 1u] = current[i];
                rgba[i * 4u + 2u] = current[i];
                rgba[i * 4u + 3u] = 255;
            }
            rgbaMips.push_back(std::move(rgba));
        } else if (format == 0) {
            std::vector<std::uint8_t> blocks;
            EncodeBC4(current, width, height, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            return false;
        }
        sizes.emplace_back(width, height);
        if (!mipmaps || (width == 1 && height == 1)) break;
        std::vector<std::uint8_t> next;
        int nextWidth = 1;
        int nextHeight = 1;
        DownsampleGray(current, width, height, next, nextWidth, nextHeight);
        current = std::move(next);
        width = nextWidth;
        height = nextHeight;
    }
    if (format == 2 && !CompressBC7(rgbaMips, sizes, mipData)) return false;
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC4_UNORM, format == 2 ? 16 : 8, format == 0 ? FourCC('A', 'T', 'I', '1') : 0u);
}
