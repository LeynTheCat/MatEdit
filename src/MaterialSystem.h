#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdint>
#include <glad/glad.h>

namespace fs = std::filesystem;

extern fs::path gameRootPath;
extern std::vector<std::string> physicalMaterialTypes;

struct Material {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::map<std::string, GLuint> textures;

    char diffusePath[256] = "";
    char normalPath[256] = "";
    char glossPath[256] = "";
    char lumaPath[256] = "";
    char bumpPath[256] = "";     
    char detailPath[256] = "";
    char detailScale[64] = "1 1";
    float smoothness = 0.0f;
    float reflectScale = 0.0f;
    float refractScale = 0.0f;
    float aberrationScale = 0.00f;
    float reliefScale = 0.00f;
    int swayHeight = 0;
    int matTypeIndex = 0;

    void updateBuffers();
    void syncParams();
    void loadTextures();
    void releaseTextures();
};

struct PhysicalMaterialEntry {
    std::string name;
    std::map<std::string, std::vector<std::string>> multiParams;

    char impactDecal[128] = "";
    char impactPartsBuf[256] = "";
    char impactSoundBuf[512] = "";
    char stepSoundBuf[1024] = "";

    void updateBuffers();
    void syncParams();
};

std::vector<std::string> LoadPhysicalMaterialTypes();
GLuint LoadDDSTexture(const std::string& path);
void ReleaseDDSTexture(GLuint texture);
GLuint LoadTextureReference(const std::string& reference);
GLuint LoadDDS_Cubemap(const std::string& path);

void LoadAllMaterials(const std::string& path, std::vector<Material>& materials);
void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials);
void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats);
void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats);

struct TexturePreviewInfo {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
    bool cachedDDS = false;
};

struct WadTexture {
    std::string name;
    int width = 0;
    int height = 0;
    std::uint32_t pixelOffset = 0;
    std::uint32_t paletteOffset = 0;
};

struct WadArchive {
    std::string relativePath;
    std::string displayName;
    std::vector<WadTexture> textures;
};


struct TextureFormatInfo {
    bool valid = false;
    bool compressed = false;
    bool srgb = false;
    bool bc5 = false;
    int channels = 4;
};

TextureFormatInfo GetTextureFormatInfo(GLuint texture);

TexturePreviewInfo LoadTexturePreview(const std::string& reference);
void ReleaseTexturePreview(TexturePreviewInfo& preview);
const std::vector<WadArchive>& GetWadArchives();
bool AddWadArchive(const std::string& relativePath);
void LoadWadArchives(const std::vector<std::string>& paths);
void ScanAndLoadAllWads();
void ClearWadArchives();
std::vector<std::string> GetLoadedWadPaths();
std::string MakeWadTextureReference(const WadArchive& wad, const WadTexture& texture);
