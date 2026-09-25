#include "EditorUI.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>

#define _CRT_SECURE_NO_WARNINGS

namespace fs = std::filesystem;

namespace {

std::string LoadTextFile(const std::string& relativePath) {
    if (relativePath.empty() || relativePath == "None") return {};
    const fs::path path = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : (gameRootPath / relativePath);
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool SaveTextFile(const std::string& relativePath, const std::string& text) {
    if (relativePath.empty() || relativePath == "None") return false;
    const fs::path path = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : (gameRootPath / relativePath);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

int ResizeTextCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* buffer = static_cast<std::vector<char>*>(data->UserData);
        buffer->resize(static_cast<size_t>(data->BufTextLen) + 1u);
        data->Buf = buffer->data();
    }
    return 0;
}

void SetTextBuffer(std::vector<char>& buffer, const std::string& text) {
    buffer.resize(text.size() + 1u);
    if (!text.empty()) std::memcpy(buffer.data(), text.data(), text.size());
    buffer[text.size()] = '\0';
}

void DrawTextDocumentEditor(
    const char* id,
    const char* label,
    std::string& path,
    std::vector<char>& buffer,
    std::string& loadedPath,
    const std::vector<std::string>& files,
    const std::function<void(const std::string&)>& selectFile) {
    ImGui::PushID(id);
    const char* popupId = "change_popup";

    if (path.empty() || path == "None") {
        ImGui::TextDisabled("NO %s FILE SELECTED", label);
        if (!files.empty()) {
            if (ImGui::Button("CHANGE##change")) ImGui::OpenPopup(popupId);
            if (ImGui::BeginPopup(popupId)) {
                for (const std::string& file : files) {
                    if (ImGui::Selectable(file.c_str())) {
                        path = file;
                        loadedPath.clear();
                        if (selectFile) selectFile(file);
                    }
                }
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
        return;
    }

    if (loadedPath != path) {
        SetTextBuffer(buffer, LoadTextFile(path));
        loadedPath = path;
    }

    ImGui::Text("%s", path.c_str());
    ImGui::SameLine();
    if (ImGui::Button("CHANGE##change")) ImGui::OpenPopup(popupId);
    if (ImGui::BeginPopup(popupId)) {
        for (const std::string& file : files) {
            if (ImGui::Selectable(file.c_str(), path == file)) {
                path = file;
                loadedPath.clear();
                if (selectFile) selectFile(file);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("RELOAD##reload")) {
        SetTextBuffer(buffer, LoadTextFile(path));
        loadedPath = path;
    }

    if (buffer.empty()) SetTextBuffer(buffer, {});
    ImGui::SetNextItemWidth(-1.0f);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize;
    if (ImGui::InputTextMultiline("##document", buffer.data(), buffer.size(), ImVec2(-1.0f, -1.0f), flags, ResizeTextCallback, &buffer)) {
        SaveTextFile(path, std::string(buffer.data()));
    }
    ImGui::PopID();
}

struct PreviewState {
    std::string reference;
    std::string kind;
    TexturePreviewInfo info;
};

struct BrowserEntry {
    fs::path path;
    std::string relative;
    bool directory = false;
    bool wad = false;
    bool dds = false;
    bool mat = false;
    bool def = false;
};

struct FolderCache {
    std::vector<BrowserEntry*> children;
};
std::unordered_map<std::string, FolderCache> g_folderCache;
PreviewState g_preview;
std::string g_browserRoot;
std::string g_searchIndexRoot;
bool g_searchIndexValid = false;
std::vector<BrowserEntry> g_searchEntries;

const WadArchive* FindLoadedWad(const std::string& relativePath);
std::deque<BrowserEntry> g_browserEntries;
char g_searchBuffer[256] = {};
bool g_viewportHovered = false;
ImVec2 g_viewportPos(0.0f, 0.0f);
ImVec2 g_viewportSize(0.0f, 0.0f);
ImDrawList* g_viewportDrawList = nullptr;
bool g_showAbout = false;
bool g_showSettings = false;
bool g_showBrowser = true;
bool g_showTexturePreview = true;
bool g_showMaterialEditor = true;
bool g_showMatCreator = false;
bool g_showDefCreator = false;
char g_newMatFile[128] = "materials.mat";
char g_newMatTexture[256] = "";
char g_newDefFile[128] = "materials.def";
char g_newDefName[128] = "default";
char g_newDefImpactDecal[128] = "shot";
char g_newDefImpactSound[512] = "";
char g_newDefStepSound[1024] = "";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool MatchesFilter(const std::string& value, const std::string& filter) {
    if (filter.empty()) return true;
    return ToLower(value).find(ToLower(filter)) != std::string::npos;
}

void ShowTooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool IsExtension(const fs::path& path, const char* ext) {
    return ToLower(path.extension().string()) == ext;
}

const WadArchive* FindLoadedWad(const std::string& relativePath) {
    for (const auto& wad : GetWadArchives()) {
        if (ToLower(wad.relativePath) == ToLower(relativePath)) return &wad;
    }
    return nullptr;
}

void SaveWads(EditorConfig& cfg) {
    cfg.loadedWads = GetLoadedWadPaths();
    SaveConfig(cfg);
}

void ReleasePreview() {
    ReleaseTexturePreview(g_preview.info);
    g_preview.reference.clear();
    g_preview.kind.clear();
}

void SelectTexture(const std::string& reference, const std::string& kind) {
    if (reference == g_preview.reference && kind == g_preview.kind) return;
    if (reference.empty()) return;

    TexturePreviewInfo next = LoadTexturePreview(reference);
    if (!next.valid || next.texture == 0) {
        ReleaseTexturePreview(next);
        return;
    }

    ReleasePreview();
    g_preview.info = next;
    g_preview.reference = reference;
    g_preview.kind = kind;
}

fs::path ResolveSkyboxEnvPath(const fs::path& root) {
    std::error_code ec;
    if (root.empty()) return {};
    fs::path candidate = root / "gfx" / "env";
    if (fs::is_directory(candidate, ec)) return candidate;
    ec.clear();
    if (root.filename() == "env" && fs::is_directory(root, ec)) return root;
    ec.clear();
    if (root.filename() == "gfx" && fs::is_directory(root / "env", ec)) return root / "env";
    ec.clear();
    if (fs::is_directory(root / "env", ec)) return root / "env";
    return {};
}

std::vector<std::string> FindDDSCubemapNames(const fs::path& envPath) {
    std::vector<std::string> result;
    std::error_code ec;
    if (!fs::is_directory(envPath, ec)) return result;

    const std::array<std::string, 6> suffixes = {"bk", "dn", "ft", "lf", "rt", "up"};
    struct Group {
        std::string base;
        std::array<bool, 6> found{};
    };
    std::unordered_map<std::string, Group> groups;

    for (const auto& item : fs::directory_iterator(envPath, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code typeEc;
        if (!item.is_regular_file(typeEc) || typeEc) continue;

        const fs::path filePath = item.path();
        if (ToLower(filePath.extension().string()) != ".dds") continue;
        const std::string stem = filePath.stem().string();
        if (stem.size() <= 2) continue;

        const std::string stemLower = ToLower(stem);
        const std::string suffix = stemLower.substr(stemLower.size() - 2);
        const auto suffixIt = std::find(suffixes.begin(), suffixes.end(), suffix);
        if (suffixIt == suffixes.end()) continue;

        const std::string base = stem.substr(0, stem.size() - 2);
        const std::string baseLower = ToLower(base);
        Group& group = groups[baseLower];
        group.base = base;
        group.found[static_cast<std::size_t>(suffixIt - suffixes.begin())] = true;
    }

    for (const auto& [key, group] : groups) {
        if (std::all_of(group.found.begin(), group.found.end(), [](bool value) { return value; })) {
            result.push_back(group.base);
        }
    }
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return ToLower(a) < ToLower(b);
    });
    return result;
}

void PopulateBrowserFolder(const fs::path& dir) {
    const std::string key = dir.generic_string();
    if (g_folderCache.find(key) != g_folderCache.end()) return;
    FolderCache cache;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const fs::path path = it->path();
        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            const std::size_t index = g_browserEntries.size();
            g_browserEntries.push_back({path, fs::relative(path, gameRootPath, ec).generic_string(), true, false, false, false, false});
            ec.clear();
            cache.children.push_back(&g_browserEntries[index]);
        } else if (it->is_regular_file(typeEc) && !typeEc) {
            const bool wad = IsExtension(path, ".wad");
            const bool dds = IsExtension(path, ".dds");
            const bool def = IsExtension(path, ".def");
            if (wad || dds || def) {
                const std::size_t index = g_browserEntries.size();
                const std::string relative = fs::relative(path, gameRootPath, ec).generic_string();
                ec.clear();
                g_browserEntries.push_back({path, relative, false, wad, dds, false, def});
                cache.children.push_back(&g_browserEntries[index]);
            }
        }
        it.increment(ec);
    }

    std::sort(cache.children.begin(), cache.children.end(), [](BrowserEntry* a, BrowserEntry* b) {
        if (a->directory != b->directory) return a->directory > b->directory;
        return ToLower(a->path.filename().string()) < ToLower(b->path.filename().string());
    });

    g_folderCache.emplace(key, std::move(cache));
}

void RebuildBrowser() {
    const std::string root = gameRootPath.string();
    if (root == g_browserRoot && !g_folderCache.empty()) return;
    g_browserRoot = root;
    g_browserEntries.clear();
    g_folderCache.clear();
    g_searchEntries.clear();
    g_searchIndexRoot.clear();
    g_searchIndexValid = false;
    if (gameRootPath.empty() || !fs::exists(gameRootPath) || !fs::is_directory(gameRootPath)) return;
    PopulateBrowserFolder(gameRootPath);
}

void SelectMat(const std::string& path, std::vector<Material>& materials, int& currentMatIndex, std::string& currentFileName) {
    if (currentFileName == path && !materials.empty()) return;
    for (auto& material : materials) material.releaseTextures();
    LoadAllMaterials(path, materials);
    currentFileName = path;
    currentMatIndex = materials.empty() ? -1 : 0;
    if (currentMatIndex >= 0) materials[currentMatIndex].loadTextures();
}

void SelectDef(const std::string& path, std::vector<PhysicalMaterialEntry>& physicalMaterials, int& currentPhysMatIndex, std::string& currentDefFile) {
    LoadAllPhysicalMaterials(path, physicalMaterials);
    currentDefFile = path;
    currentPhysMatIndex = physicalMaterials.empty() ? -1 : 0;
    if (currentPhysMatIndex >= 0) physicalMaterials[currentPhysMatIndex].updateBuffers();
}

void AssignTexture(Material& material, const char* field, const std::string& reference) {
    char* target = nullptr;
    std::size_t size = 0;
    if (std::strcmp(field, "diffuse") == 0) { target = material.diffusePath; size = sizeof(material.diffusePath); }
    else if (std::strcmp(field, "normal") == 0) { target = material.normalPath; size = sizeof(material.normalPath); }
    else if (std::strcmp(field, "gloss") == 0) { target = material.glossPath; size = sizeof(material.glossPath); }
    else if (std::strcmp(field, "luma") == 0) { target = material.lumaPath; size = sizeof(material.lumaPath); }
    else if (std::strcmp(field, "bump") == 0) { target = material.bumpPath; size = sizeof(material.bumpPath); }
    else if (std::strcmp(field, "detail") == 0) { target = material.detailPath; size = sizeof(material.detailPath); }
    if (!target || size == 0) return;
    std::strncpy(target, reference.c_str(), size - 1);
    target[size - 1] = '\0';
}

void AssignSelected(Material* material, const char* field) {
    if (!material || g_preview.reference.empty() || !g_preview.info.valid || g_preview.info.texture == 0) return;
    AssignTexture(*material, field, g_preview.reference);
    material->syncParams();
    material->loadTextures();
}

void DrawAssignGrid(Material* material) {
    ImGui::TextDisabled("ASSIGN SELECTED TEXTURE AS");
    const char* labels[] = {"DIFFUSE", "NORMAL", "GLOSS", "LUMA", "BUMP", "DETAIL"};
    const char* fields[] = {"diffuse", "normal", "gloss", "luma", "bump", "detail"};
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int i = 0; i < 6; ++i) {
        if ((i & 1) != 0) ImGui::SameLine();
        if (ImGui::Button(labels[i], ImVec2(std::max(60.0f, width), 22.0f))) AssignSelected(material, fields[i]);
    }
}

void DrawPreview(Material* material, float width) {
    ImGui::BeginChild("TexturePreviewContent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

    if (g_preview.reference.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(8.0f, (avail.x - 150.0f) * 0.5f), std::max(20.0f, (avail.y - 20.0f) * 0.5f)));
        ImGui::TextDisabled("CLICK A TEXTURE");
        ImGui::EndChild();
        return;
    }

    ImGui::TextWrapped("%s", g_preview.reference.c_str());
    ImGui::TextDisabled("%s", g_preview.kind.c_str());
    if (g_preview.info.valid) {
        ImGui::TextDisabled("%d x %d", g_preview.info.width, g_preview.info.height);
        ImGui::Spacing();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float assignHeight = 92.0f;
        const float imageAreaHeight = std::clamp(avail.y - assignHeight, 96.0f, 280.0f);
        ImGui::BeginChild("##TextureImageArea", ImVec2(0.0f, imageAreaHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 imageAvail = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(g_preview.info.width) /
                             static_cast<float>(std::max(1, g_preview.info.height));
        float imageW = std::max(32.0f, imageAvail.x - 12.0f);
        float imageH = imageW / std::max(0.01f, aspect);
        if (imageH > imageAvail.y - 8.0f) {
            imageH = std::max(32.0f, imageAvail.y - 8.0f);
            imageW = imageH * aspect;
        }
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + std::max(4.0f, (imageAvail.x - imageW) * 0.5f),
                                   cursor.y + std::max(4.0f, (imageAvail.y - imageH) * 0.5f)));
        ImGui::Image(static_cast<ImTextureID>(g_preview.info.texture), ImVec2(imageW, imageH));
        ImGui::EndChild();
        ImGui::Spacing();
    } else {
        ImGui::BeginChild("NoPreview", ImVec2(0, 100.0f), true);
        ImGui::TextDisabled("PREVIEW UNAVAILABLE");
        ImGui::EndChild();
    }

    if (material) DrawAssignGrid(material);
    else ImGui::TextDisabled("LOAD A .MAT FILE TO ASSIGN TEXTURES");
    (void)width;
    ImGui::EndChild();
}

void DrawWadNode(const WadArchive& wad, const std::string& filter) {
    const bool hasMatch = filter.empty() || std::any_of(wad.textures.begin(), wad.textures.end(), [&](const WadTexture& tex) {
        return MatchesFilter(tex.name, filter) || MatchesFilter(MakeWadTextureReference(wad, tex), filter);
    });
    if (!hasMatch) return;

    const std::string id = "WAD  " + wad.displayName + "  [" + std::to_string(wad.textures.size()) + "]##" + wad.relativePath;
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | (!filter.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags);
    if (!open) return;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(wad.textures.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& texture = wad.textures[static_cast<std::size_t>(i)];
            const std::string ref = MakeWadTextureReference(wad, texture);
            if (!MatchesFilter(texture.name, filter) && !MatchesFilter(ref, filter)) continue;
            const std::string label = "TEXTURE  " + texture.name + "##" + ref;
            if (ImGui::Selectable(label.c_str(), g_preview.reference == ref, ImGuiSelectableFlags_None)) SelectTexture(ref, "WAD");
        }
    }
    ImGui::TreePop();
}

void DrawDirectory(const fs::path&, const std::string&, EditorConfig&, std::vector<Material>&, std::vector<PhysicalMaterialEntry>&, std::string&, int&, std::string&, int&);

void BuildSearchIndex() {
    const std::string root = gameRootPath.string();
    if (g_searchIndexValid && g_searchIndexRoot == root) return;

    g_searchEntries.clear();
    g_searchIndexRoot = root;
    g_searchIndexValid = true;
    if (gameRootPath.empty() || !fs::is_directory(gameRootPath)) return;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(gameRootPath, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc) || typeEc) continue;
        const fs::path path = it->path();
        const bool wad = IsExtension(path, ".wad");
        const bool dds = IsExtension(path, ".dds");
        const bool def = IsExtension(path, ".def");
        if (!wad && !dds && !def) continue;
        std::error_code relEc;
        const std::string relative = fs::relative(path, gameRootPath, relEc).generic_string();
        if (relEc) continue;
        g_searchEntries.push_back({path, relative, false, wad, dds, false, def});
    }
}

void DrawSearchResults(const std::string& filter, EditorConfig& cfg, std::vector<Material>& materials,
                       std::vector<PhysicalMaterialEntry>& physicalMaterials, std::string& currentFileName,
                       int& currentMatIndex, std::string& currentDefFile, int& currentPhysMatIndex) {
    if (filter.empty()) {
        DrawDirectory(gameRootPath, filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
        return;
    }

    BuildSearchIndex();
    int resultCount = 0;
    for (const BrowserEntry& entry : g_searchEntries) {
        const bool fileMatch = MatchesFilter(entry.path.filename().string(), filter) || MatchesFilter(entry.relative, filter);
        if (!fileMatch && !entry.wad) continue;
        if (entry.wad) {
            const WadArchive* wad = FindLoadedWad(entry.relative);
            if (wad) {
                for (const auto& texture : wad->textures) {
                    const std::string ref = MakeWadTextureReference(*wad, texture);
                    if (!MatchesFilter(texture.name, filter) && !MatchesFilter(ref, filter)) continue;
                    const std::string label = "TEXTURE  " + texture.name + "  [" + wad->relativePath + "]##SEARCHWAD:" + ref;
                    if (ImGui::Selectable(label.c_str(), g_preview.reference == ref)) SelectTexture(ref, "WAD");
                    ++resultCount;
                }
            } else if (fileMatch) {
                const std::string label = "WAD  " + entry.relative + "  [CLICK TO LOAD]##SEARCHWADFILE:" + entry.relative;
                if (ImGui::Selectable(label.c_str(), false) && AddWadArchive(entry.relative)) SaveWads(cfg);
                ++resultCount;
            }
            continue;
        }
        if (!fileMatch) continue;
        const std::string label = (entry.dds ? "DDS  " : "DEF  ") + entry.relative + "##SEARCH:" + entry.relative;
        if (entry.dds) {
            if (ImGui::Selectable(label.c_str(), g_preview.reference == entry.relative)) SelectTexture(entry.relative, "DDS");
        } else if (entry.def) {
            if (ImGui::Selectable(label.c_str(), currentDefFile == entry.relative)) SelectDef(entry.relative, physicalMaterials, currentPhysMatIndex, currentDefFile);
        }
        ++resultCount;
    }
    if (resultCount == 0) ImGui::TextDisabled("NO SEARCH RESULTS");
}

void DrawDirectory(const fs::path& dir, const std::string& filter, EditorConfig& cfg, std::vector<Material>& materials,
                   std::vector<PhysicalMaterialEntry>& physicalMaterials, std::string& currentFileName, int& currentMatIndex,
                   std::string& currentDefFile, int& currentPhysMatIndex) {
    if (!filter.empty()) {
        DrawSearchResults(filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
        return;
    }


    PopulateBrowserFolder(dir);
    auto it = g_folderCache.find(dir.generic_string());
    if (it == g_folderCache.end()) return;
    auto& children = it->second.children;
    if (children.empty()) return;

    const std::string dirLabel = std::string("FOLDER  ") + (dir == gameRootPath ? gameRootPath.filename().string() : dir.filename().string()) + "##DIR:" + dir.generic_string();
    ImGui::PushID(dir.generic_string().c_str());
    const ImGuiTreeNodeFlags rootFlags = (dir == gameRootPath || !filter.empty()) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    const bool open = ImGui::TreeNodeEx(dirLabel.c_str(), rootFlags | ImGuiTreeNodeFlags_SpanAvailWidth);
    if (!open) {
        ImGui::PopID();
        return;
    }

    std::size_t directoryCount = 0;
    while (directoryCount < children.size() && children[directoryCount]->directory) {
        ++directoryCount;
    }

    for (std::size_t i = 0; i < directoryCount; ++i) {
        const BrowserEntry* entry = children[i];
        DrawDirectory(entry->path, filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
    }

    const int fileCount = static_cast<int>(children.size() - directoryCount);
    ImGuiListClipper clipper;
    clipper.Begin(fileCount);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const BrowserEntry* entry = children[directoryCount + static_cast<std::size_t>(i)];
            if (entry->wad) {
                const WadArchive* loaded = FindLoadedWad(entry->relative);
                if (loaded) {
                    DrawWadNode(*loaded, filter);
                } else if (MatchesFilter(entry->relative, filter) || MatchesFilter(entry->path.filename().string(), filter)) {
                    const std::string label = "WAD  " + entry->path.filename().string() + "  [CLICK TO LOAD]##" + entry->relative;
                    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None)) {
                        if (AddWadArchive(entry->relative)) SaveWads(cfg);
                    }
                }
            } else if (entry->dds) {
                const std::string label = "DDS  " + entry->path.filename().string() + "##" + entry->relative;
                if (ImGui::Selectable(label.c_str(), g_preview.reference == entry->relative, ImGuiSelectableFlags_None)) SelectTexture(entry->relative, "DDS");
            } else if (entry->def) {
                const std::string label = "DEF  " + entry->path.filename().string() + "##" + entry->relative;
                if (ImGui::Selectable(label.c_str(), currentDefFile == entry->relative, ImGuiSelectableFlags_None)) SelectDef(entry->relative, physicalMaterials, currentPhysMatIndex, currentDefFile);
            }
        }
    }

    ImGui::TreePop();
    ImGui::PopID();
}

bool DrawResettableInput(const char* id, const char* label, char* buffer, size_t bufferSize) {
    const float buttonWidth = 24.0f;
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float inputWidth = std::max(80.0f, available - buttonWidth - labelWidth - 16.0f);
    bool changed = false;
    if (ImGui::Button((std::string("X##reset_") + id).c_str(), ImVec2(buttonWidth, 0.0f))) {
        if (buffer[0] != '\0') {
            buffer[0] = '\0';
            changed = true;
        }
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputText((std::string("##") + id).c_str(), buffer, bufferSize)) changed = true;
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextUnformatted(label);
    return changed;
}

void DrawOldEditorPanels(
    int display_w, int display_h,
    EditorConfig& editorCfg,
    std::vector<Material>& materials,
    std::vector<PhysicalMaterialEntry>& physicalMaterials,
    std::vector<std::string>& matFiles,
    std::string& currentFileName,
    int& currentMatIndex,
    std::string& currentDefFile,
    int& currentPhysMatIndex,
    int& shapeType,
    int& lightMode,
    bool& useNormal,
    bool& useGloss,
    bool& useLuma,
    bool& useBump,
    float& lightIntensity,
    float* lightColor,
    std::function<void(std::string&, int&)> refreshDataFunc
) {
    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Visual Materials (.mat)")) {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float panelHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - 4.0f);
            const float colGap = ImGui::GetStyle().ItemSpacing.x;
            const float colWidth = std::max(180.0f, (availWidth - colGap * 2.0f) / 3.0f);

            ImGui::BeginChild("MatFilesChild", ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Material Files");
            if (!matFiles.empty()) {
                if (ImGui::BeginCombo("Select .mat File", currentFileName.c_str())) {
                    for (size_t n = 0; n < matFiles.size(); ++n) {
                        if (ImGui::Selectable(matFiles[n].c_str(), currentFileName == matFiles[n])) {
                            currentFileName = matFiles[n];
                            editorCfg.lastMatFile = currentFileName;
                            materials.clear();
                            LoadAllMaterials(currentFileName, materials);
                            currentMatIndex = materials.empty() ? -1 : 0;
                            if (currentMatIndex >= 0) materials[currentMatIndex].loadTextures();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("No .mat files found!");
            }

            if (!matFiles.empty() && currentFileName != "None") {
                if (ImGui::Button("Add New Material", ImVec2(-1, 0))) {
                    Material newMat;
                    newMat.name = "new_material";
                    std::strncpy(newMat.diffusePath, "textures/default", sizeof(newMat.diffusePath) - 1);
                    newMat.smoothness = 1.0f;
                    newMat.reflectScale = 0.3f;
                    newMat.syncParams();
                    materials.push_back(newMat);
                    currentMatIndex = static_cast<int>(materials.size()) - 1;
                    materials[currentMatIndex].loadTextures();
                    SaveAllMaterials(currentFileName, materials);
                }
            }

            if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                static char nameBuffer[128] = {};
                static int lastMatIndex = -1;
                if (lastMatIndex != currentMatIndex) {
                    std::strncpy(nameBuffer, materials[currentMatIndex].name.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    lastMatIndex = currentMatIndex;
                }
                if (ImGui::BeginCombo("Select Material", materials[currentMatIndex].name.c_str())) {
                    for (size_t n = 0; n < materials.size(); ++n) {
                        if (ImGui::Selectable(materials[n].name.c_str(), currentMatIndex == static_cast<int>(n))) {
                            currentMatIndex = static_cast<int>(n);
                            materials[currentMatIndex].loadTextures();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Text("MATERIAL: %s", materials[currentMatIndex].name.c_str());
                if (ImGui::Button("X##reset_material_name", ImVec2(24.0f, 0.0f))) { nameBuffer[0] = '\0'; materials[currentMatIndex].name.clear(); }
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##MaterialName", nameBuffer, sizeof(nameBuffer))) materials[currentMatIndex].name = nameBuffer;
                Material& mat = materials[currentMatIndex];
                bool textureFieldsChanged = false;
                textureFieldsChanged |= DrawResettableInput("diffuse", "Diffuse", mat.diffusePath, sizeof(mat.diffusePath));
                textureFieldsChanged |= DrawResettableInput("normal", "Normal", mat.normalPath, sizeof(mat.normalPath));
                textureFieldsChanged |= DrawResettableInput("gloss", "Gloss", mat.glossPath, sizeof(mat.glossPath));
                textureFieldsChanged |= DrawResettableInput("luma", "Luma", mat.lumaPath, sizeof(mat.lumaPath));
                textureFieldsChanged |= DrawResettableInput("bump", "Bump Map", mat.bumpPath, sizeof(mat.bumpPath));
                textureFieldsChanged |= DrawResettableInput("detail", "Detail", mat.detailPath, sizeof(mat.detailPath));
                if (textureFieldsChanged) {
                    mat.syncParams();
                    mat.loadTextures();
                    SaveAllMaterials(currentFileName, materials);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("MatParamsChild", ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Parameters & Model");
            if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                Material& mat = materials[currentMatIndex];
                bool materialParamsChanged = false;
                materialParamsChanged |= ImGui::SliderFloat("Smoothness", &mat.smoothness, 0.0f, 1.0f);
                materialParamsChanged |= ImGui::SliderFloat("Reflect", &mat.reflectScale, 0.0f, 1.0f);
                materialParamsChanged |= ImGui::SliderFloat("Relief", &mat.reliefScale, 0.0f, 1.0f);
                materialParamsChanged |= ImGui::SliderFloat("Refract", &mat.refractScale, 0.0f, 1.0f);
                materialParamsChanged |= ImGui::SliderFloat("Abberation", &mat.aberrationScale, 0.0f, 1.0f);
                std::vector<const char*> physMatPtrs;
                for (const auto& s : physicalMaterialTypes) physMatPtrs.push_back(s.c_str());
                if (!physMatPtrs.empty() && ImGui::Combo("Phys Material", &mat.matTypeIndex, physMatPtrs.data(), static_cast<int>(physMatPtrs.size()))) {
                    if (mat.matTypeIndex >= 0 && static_cast<size_t>(mat.matTypeIndex) < physicalMaterialTypes.size()) {
                        for (auto& p : mat.params) if (p.first == "material") p.second = physicalMaterialTypes[mat.matTypeIndex];
                    }
                }
                if (ImGui::Button("Apply Changes")) {
                    materialParamsChanged = true;
                    mat.syncParams();
                    mat.loadTextures();
                }
                if (materialParamsChanged) {
                    mat.smoothness = std::clamp(mat.smoothness, 0.0f, 1.0f);
                    mat.reflectScale = std::max(mat.reflectScale, 0.0f);
                    mat.reliefScale = std::max(mat.reliefScale, 0.0f);
                    mat.refractScale = std::max(mat.refractScale, 0.0f);
                    mat.aberrationScale = std::max(mat.aberrationScale, 0.0f);
                    mat.syncParams();
                    SaveAllMaterials(currentFileName, materials);
                }
                ImGui::SameLine();
                if (ImGui::Button("Save All")) {
                    mat.syncParams();
                    SaveAllMaterials(currentFileName, materials);
                }
            }
            ImGui::Separator();
            ImGui::Combo("Model Shape", &shapeType, "Cube\0Sphere\0Plane\0Cylinder\0Cone\0Torus\0Newell Teapot\0");
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("MatLightChild", ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Lighting & Maps");
            ImGui::Checkbox("Normal Map", &useNormal);
            ImGui::Checkbox("Gloss Map", &useGloss);
            ImGui::Checkbox("Luma Map", &useLuma);
            ImGui::Checkbox("Use Bump", &useBump);
            ImGui::Combo("Light Mode", &lightMode, "Camera\0Fixed\0Dynamic\0");
            if (lightMode == 2) {
                ImGui::SliderFloat("Dynamic Speed", &editorCfg.dynamicLightSpeed, 0.1f, 5.0f);
                ImGui::SliderFloat("Dynamic Radius", &editorCfg.dynamicLightRadius, 1.0f, 6.0f);
            }
            const float intensityMax = editorCfg.allowHighLightIntensity ? 25.0f : 5.0f;
            ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, intensityMax);
            lightIntensity = std::clamp(lightIntensity, 0.0f, intensityMax);
            ImGui::ColorEdit3("Color", lightColor);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Physical Materials (.def)")) {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float panelHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - 4.0f);
            const float colGap = ImGui::GetStyle().ItemSpacing.x;
            const float colWidth = std::max(220.0f, (availWidth - colGap) * 0.5f);

            ImGui::BeginChild("DefListChild", ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Physical Material Entries");
            if (!physicalMaterials.empty() && currentPhysMatIndex >= 0 && static_cast<size_t>(currentPhysMatIndex) < physicalMaterials.size()) {
                if (ImGui::BeginCombo("Select Physical Material", physicalMaterials[currentPhysMatIndex].name.c_str())) {
                    for (size_t n = 0; n < physicalMaterials.size(); ++n) {
                        if (ImGui::Selectable(physicalMaterials[n].name.c_str(), currentPhysMatIndex == static_cast<int>(n))) {
                            currentPhysMatIndex = static_cast<int>(n);
                            physicalMaterials[currentPhysMatIndex].updateBuffers();
                        }
                    }
                    ImGui::EndCombo();
                }
                static char defNameBuf[128] = {};
                static int lastDefIndex = -1;
                if (lastDefIndex != currentPhysMatIndex) {
                    std::strncpy(defNameBuf, physicalMaterials[currentPhysMatIndex].name.c_str(), sizeof(defNameBuf) - 1);
                    defNameBuf[sizeof(defNameBuf) - 1] = '\0';
                    lastDefIndex = currentPhysMatIndex;
                }
                if (ImGui::InputText("Material Name", defNameBuf, sizeof(defNameBuf))) physicalMaterials[currentPhysMatIndex].name = defNameBuf;
                if (ImGui::Button("Add New Def Entry")) {
                    PhysicalMaterialEntry newEntry;
                    newEntry.name = "new_material_type";
                    newEntry.multiParams["impact_decal"] = {"shot"};
                    newEntry.updateBuffers();
                    physicalMaterials.push_back(newEntry);
                    currentPhysMatIndex = static_cast<int>(physicalMaterials.size()) - 1;
                }
            } else {
                ImGui::TextDisabled("No physical materials loaded");
                if (ImGui::Button("Load / Create Default")) {
                    fs::path defPath = gameRootPath / "scripts" / "materials.def";
                    if (!fs::exists(defPath)) defPath = gameRootPath / "materials.def";
                    LoadAllPhysicalMaterials(defPath.string(), physicalMaterials);
                    if (physicalMaterials.empty()) {
                        PhysicalMaterialEntry defMat;
                        defMat.name = "default";
                        defMat.multiParams["impact_decal"] = {"shot"};
                        defMat.updateBuffers();
                        physicalMaterials.push_back(defMat);
                    }
                    currentPhysMatIndex = 0;
                    physicalMaterials[currentPhysMatIndex].updateBuffers();
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("DefParamsChild", ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Parameters Editor");
            if (!physicalMaterials.empty() && currentPhysMatIndex >= 0 && static_cast<size_t>(currentPhysMatIndex) < physicalMaterials.size()) {
                PhysicalMaterialEntry& pMat = physicalMaterials[currentPhysMatIndex];
                ImGui::InputText("Impact Decal", pMat.impactDecal, sizeof(pMat.impactDecal));
                ImGui::InputText("Impact Parts", pMat.impactPartsBuf, sizeof(pMat.impactPartsBuf));
                ImGui::InputText("Impact Sound", pMat.impactSoundBuf, sizeof(pMat.impactSoundBuf));
                ImGui::InputText("Step Sound", pMat.stepSoundBuf, sizeof(pMat.stepSoundBuf));
                if (ImGui::Button("Apply Def Changes")) pMat.syncParams();
                ImGui::SameLine();
                if (ImGui::Button("Save materials.def")) {
                    pMat.syncParams();
                    fs::path defPath = gameRootPath / "scripts" / "materials.def";
                    if (!fs::exists(defPath.parent_path())) fs::create_directories(defPath.parent_path());
                    SaveAllPhysicalMaterials(defPath.string(), physicalMaterials);
                    physicalMaterialTypes = LoadPhysicalMaterialTypes();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    (void)display_w;
    (void)display_h;
    (void)currentDefFile;
    (void)refreshDataFunc;
}

}

void DrawEditorUI(
    int display_w, int display_h,
    EditorConfig& editorCfg,
    std::vector<Material>& materials,
    std::vector<PhysicalMaterialEntry>& physicalMaterials,
    std::vector<std::string>& matFiles,
    std::vector<std::string>& defFiles,
    std::vector<std::string>& ddsFiles,
    std::string& currentFileName,
    int& currentMatIndex,
    std::string& currentDefFile,
    int& currentPhysMatIndex,
    int& shapeType,
    int& lightMode,
    bool& useNormal,
    bool& useGloss,
    bool& useLuma,
    bool& useBump,
    float& lightIntensity,
    float* lightColor,
    GLuint& skyboxTexture,
    std::function<void(std::string&, int&)> refreshDataFunc
) {
    g_viewportHovered = false;
    g_viewportPos = ImVec2(0.0f, 0.0f);
    g_viewportSize = ImVec2(0.0f, 0.0f);
    g_viewportDrawList = nullptr;
    RebuildBrowser();

    const float width = static_cast<float>(std::max(1, display_w));
    const float height = static_cast<float>(std::max(1, display_h));
    const float gap = 6.0f;
    const bool leftVisible = g_showBrowser || g_showTexturePreview;
    const float leftWidth = leftVisible ? std::clamp(width * 0.26f, 300.0f, 390.0f) : 0.0f;
    const float bottomHeight = g_showMaterialEditor ? std::clamp(height * 0.31f, 255.0f, 315.0f) : 0.0f;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##MatEditLayout", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Load All WADs")) {
                ScanAndLoadAllWads();
                SaveWads(editorCfg);
                ReleasePreview();
            }
            if (ImGui::MenuItem("Reload Saved WADs")) {
                LoadWadArchives(editorCfg.loadedWads);
                ReleasePreview();
            }
            if (ImGui::MenuItem("Clear Loaded WADs")) {
                ClearWadArchives();
                editorCfg.loadedWads.clear();
                SaveConfig(editorCfg);
                ReleasePreview();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quick Create .MAT")) g_showMatCreator = true;
            if (ImGui::MenuItem("Quick Create .DEF")) g_showDefCreator = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Open Settings")) g_showSettings = true;
            ImGui::Separator();
            if (ImGui::BeginMenu("Panels")) {
                ImGui::MenuItem("File Browser", nullptr, &g_showBrowser);
                ImGui::MenuItem("Texture Preview", nullptr, &g_showTexturePreview);
                ImGui::MenuItem("Material Editor", nullptr, &g_showMaterialEditor);
                ImGui::Separator();
                if (ImGui::MenuItem("Show All Panels")) {
                    g_showBrowser = true;
                    g_showTexturePreview = true;
                    g_showMaterialEditor = true;
                }
                if (ImGui::MenuItem("Hide All Panels")) {
                    g_showBrowser = false;
                    g_showTexturePreview = false;
                    g_showMaterialEditor = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Instruments")) {
            if (ImGui::MenuItem("Reset Material View")) {
                shapeType = 0;
                lightMode = 0;
                useNormal = true;
                useGloss = true;
                useLuma = true;
                useBump = true;
            }
            if (ImGui::MenuItem("Reset Lighting")) {
                lightMode = 0;
                lightIntensity = 1.0f;
                lightColor[0] = 1.0f;
                lightColor[1] = 1.0f;
                lightColor[2] = 1.0f;
            }
            if (ImGui::MenuItem("Reload Current Material")) {
                if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                    materials[currentMatIndex].loadTextures();
                    ReleasePreview();
                }
            }
            if (ImGui::MenuItem("Clear Texture Assignments")) {
                if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                    Material& mat = materials[currentMatIndex];
                    mat.diffusePath[0] = '\0';
                    mat.normalPath[0] = '\0';
                    mat.glossPath[0] = '\0';
                    mat.lumaPath[0] = '\0';
                    mat.bumpPath[0] = '\0';
                    mat.detailPath[0] = '\0';
                    mat.syncParams();
                    mat.loadTextures();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About MatEdit")) g_showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (leftVisible) {
        ImGui::BeginChild("##BrowserColumn", ImVec2(leftWidth, 0.0f), false);

        if (g_showBrowser) {
            const float browserAvail = ImGui::GetContentRegionAvail().y;
            const float searchHeight = 32.0f;
            const float previewHeight = g_showTexturePreview ? std::clamp(browserAvail * 0.52f, 340.0f, 480.0f) : 0.0f;
            const float treeHeight = std::max(100.0f, browserAvail - previewHeight - searchHeight - (g_showTexturePreview ? gap * 2.0f : gap));
            ImGui::BeginChild("##FileTree", ImVec2(0.0f, treeHeight), true);
            DrawDirectory(gameRootPath, std::string(g_searchBuffer), editorCfg, materials, physicalMaterials,
                          currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##FileSearch", "Search files / WAD textures...", g_searchBuffer, sizeof(g_searchBuffer));
            ShowTooltip("Searches game files, material files, model textures and loaded WAD textures. Type part of a name or path.");
            ImGui::Dummy(ImVec2(0.0f, 1.5f));
        }

        if (g_showTexturePreview) {
            if (g_showBrowser) ImGui::Spacing();
            const float previewHeight = g_showBrowser ? std::clamp(ImGui::GetContentRegionAvail().y, 340.0f, 480.0f) : ImGui::GetContentRegionAvail().y;
            ImGui::BeginChild("##PreviewPanel", ImVec2(0.0f, previewHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            Material* activeMaterial = (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) ? &materials[currentMatIndex] : nullptr;
            DrawPreview(activeMaterial, leftWidth);
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    if (leftVisible) ImGui::SameLine(0.0f, gap);

    const float rightWidth = std::max(100.0f, width - leftWidth - (leftVisible ? gap : 0.0f) - 10.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##RightLayout", ImVec2(rightWidth, 0.0f), false);
    ImGui::PopStyleColor();

    static int topViewTab = 0;
    static std::vector<char> matTextBuffer;
    static std::vector<char> defTextBuffer;
    static std::string loadedMatTextPath;
    static std::string loadedDefTextPath;

    if (ImGui::Button("SCENE", ImVec2(76.0f, 26.0f))) topViewTab = 0;
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("MAT TEXT", ImVec2(96.0f, 26.0f))) topViewTab = 1;
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("DEF TEXT", ImVec2(96.0f, 26.0f))) topViewTab = 2;

    const float rightAvail = ImGui::GetContentRegionAvail().y;
    const float centerHeight = std::max(80.0f, rightAvail - bottomHeight - (g_showMaterialEditor ? gap : 0.0f));

    if (topViewTab == 0) {
        ImGui::BeginChild("##ViewportSpacer", ImVec2(0.0f, centerHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        g_viewportPos = ImGui::GetWindowPos();
        g_viewportSize = ImGui::GetWindowSize();
        g_viewportDrawList = ImGui::GetWindowDrawList();
        g_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##TextDocument", ImVec2(0.0f, centerHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (topViewTab == 1) {
            DrawTextDocumentEditor("##MatTextEditor", ".MAT", currentFileName, matTextBuffer, loadedMatTextPath, matFiles,
                                [&](const std::string& file) {
                                    SelectMat(file, materials, currentMatIndex, currentFileName);
                                    editorCfg.lastMatFile = currentFileName;
                                });
        } else {
            DrawTextDocumentEditor("##DefTextEditor", ".DEF", currentDefFile, defTextBuffer, loadedDefTextPath, defFiles,
                                [&](const std::string& file) {
                                    SelectDef(file, physicalMaterials, currentPhysMatIndex, currentDefFile);
                                });
        }
        ImGui::EndChild();
        g_viewportPos = ImVec2(0.0f, 0.0f);
        g_viewportSize = ImVec2(0.0f, 0.0f);
        g_viewportHovered = false;
        g_viewportDrawList = nullptr;
    }

    if (g_showMaterialEditor) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
        ImGui::BeginChild("##EditorPanels", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawOldEditorPanels(display_w, display_h, editorCfg, materials, physicalMaterials, matFiles,
                            currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex,
                            shapeType, lightMode, useNormal, useGloss, useLuma, useBump,
                            lightIntensity, lightColor, refreshDataFunc);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    static char settingsGamePath[1024] = {};
    static bool settingsInitialized = false;
    if (!settingsInitialized) {
        std::strncpy(settingsGamePath, gameRootPath.string().c_str(), sizeof(settingsGamePath) - 1);
        settingsInitialized = true;
    }

    const ImVec2 mainCenter = ImGui::GetMainViewport()->GetCenter();

    if (g_showMatCreator) {
        ImGui::OpenPopup("Quick Create .MAT");
        g_showMatCreator = false;
    }
    if (g_showDefCreator) {
        ImGui::OpenPopup("Quick Create .DEF");
        g_showDefCreator = false;
    }

    ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quick Create .MAT", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled("Creates a PrimeXT texture material in scripts/*.mat.");
        ImGui::InputTextWithHint("File", "materials.mat", g_newMatFile, sizeof(g_newMatFile));
        ShowTooltip("MAT file name. The file is created inside scripts/. The .mat extension is added automatically if missing.");
        ImGui::InputTextWithHint("Texture / model texture", "wall1 or model/body", g_newMatTexture, sizeof(g_newMatTexture));
        ShowTooltip("Base texture name or model texture reference used by the material. Keep it identical to the texture name in the WAD or model.");
        ImGui::TextDisabled("Tip: the texture name must match the WAD/model texture. Extra maps can use PrimeXT suffixes such as _norm, _gloss and _hmap.");
        const char* phys = physicalMaterialTypes.empty() ? "default" : physicalMaterialTypes[0].c_str();
        static int createMatPhysIndex = 0;
        std::vector<const char*> physPtrs;
        for (const auto& name : physicalMaterialTypes) physPtrs.push_back(name.c_str());
        if (!physPtrs.empty()) {
            createMatPhysIndex = std::clamp(createMatPhysIndex, 0, static_cast<int>(physPtrs.size()) - 1);
            ImGui::Combo("Physical Material", &createMatPhysIndex, physPtrs.data(), static_cast<int>(physPtrs.size()));
            ShowTooltip("Physical material type written to the MAT definition, such as concrete, wood or metal.");
            phys = physPtrs[createMatPhysIndex];
        }
        if (ImGui::Button("Create & Open", ImVec2(130.0f, 0.0f))) {
            std::string fileName = g_newMatFile;
            if (fileName.empty()) fileName = "materials.mat";
            if (ToLower(fs::path(fileName).extension().string()) != ".mat") fileName += ".mat";
            fs::path outPath = gameRootPath / "scripts" / fileName;
            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);
            std::ofstream out(outPath);
            if (out.is_open()) {
                const std::string textureName = g_newMatTexture;
                out << "\"" << textureName << "\"\n{\n";
                if (phys && *phys) out << "\t\"material\"\t\"" << phys << "\"\n";
                out << "}\n";
                out.close();
                const std::string relative = fs::relative(outPath, gameRootPath).generic_string();
                std::vector<Material> createdMaterials;
                LoadAllMaterials(relative, createdMaterials);
                for (auto& material : materials) material.releaseTextures();
                for (auto& material : createdMaterials) material.loadTextures();
                materials = std::move(createdMaterials);
                currentFileName = relative;
                currentMatIndex = materials.empty() ? -1 : 0;
                editorCfg.lastMatFile = relative;
                SaveConfig(editorCfg);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quick Create .DEF", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled("Creates a PrimeXT physical-material definition in scripts/*.def.");
        ImGui::InputTextWithHint("File", "materials.def", g_newDefFile, sizeof(g_newDefFile));
        ImGui::InputTextWithHint("Material name", "concrete", g_newDefName, sizeof(g_newDefName));
        ShowTooltip("Name of the physical material definition. This is the name used by the game when resolving the material type.");
        ImGui::InputTextWithHint("Impact decal", "shot", g_newDefImpactDecal, sizeof(g_newDefImpactDecal));
        ShowTooltip("Decal name used when a bullet or impact hits this material.");
        ImGui::InputTextWithHint("Impact sounds", "materials/debris_concrete_01.wav materials/debris_concrete_02.wav", g_newDefImpactSound, sizeof(g_newDefImpactSound));
        ShowTooltip("Space-separated impact sound paths. Up to 8 sounds are written to the DEF file.");
        ImGui::InputTextWithHint("Step sounds", "materials/walk_concrete_01.wav materials/walk_concrete_02.wav", g_newDefStepSound, sizeof(g_newDefStepSound));
        ShowTooltip("Space-separated footstep sound paths. Up to 8 sounds are written to the DEF file.");
        ImGui::TextDisabled("Tip: sound paths are relative to sound/. Up to 8 impact/step sounds are supported.");
        if (ImGui::Button("Create & Open", ImVec2(130.0f, 0.0f))) {
            std::string fileName = g_newDefFile;
            if (fileName.empty()) fileName = "materials.def";
            if (ToLower(fs::path(fileName).extension().string()) != ".def") fileName += ".def";
            fs::path outPath = gameRootPath / "scripts" / fileName;
            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);
            std::ofstream out(outPath);
            if (out.is_open()) {
                out << "\"" << g_newDefName << "\"\n{\n";
                if (g_newDefImpactDecal[0]) out << "\t\"impact_decal\"\t\"" << g_newDefImpactDecal << "\"\n";
                auto writeList = [&](const char* key, const char* text) {
                    std::stringstream ss(text);
                    std::string item;
                    std::vector<std::string> values;
                    while (ss >> item && values.size() < 8) values.push_back(item);
                    if (!values.empty()) {
                        out << "\t\"" << key << "\"";
                        for (const auto& value : values) out << "\t\"" << value << "\"";
                        out << "\n";
                    }
                };
                writeList("impact_sound", g_newDefImpactSound);
                writeList("step_sound", g_newDefStepSound);
                out << "}\n";
                out.close();
                const std::string relative = fs::relative(outPath, gameRootPath).generic_string();
                currentDefFile = relative;
                LoadAllPhysicalMaterials(relative, physicalMaterials);
                currentPhysMatIndex = physicalMaterials.empty() ? -1 : 0;
                if (currentPhysMatIndex >= 0) physicalMaterials[currentPhysMatIndex].updateBuffers();
                physicalMaterialTypes = LoadPhysicalMaterialTypes();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_showSettings) {
        const fs::path skyboxEnvPath = ResolveSkyboxEnvPath(gameRootPath);
        const std::vector<std::string> skyboxes = FindDDSCubemapNames(skyboxEnvPath);
        if (skyboxes.empty() && !gameRootPath.empty()) {
            const fs::path envPath = gameRootPath / "gfx" / "env";
            std::error_code envError;
            if (!fs::is_directory(envPath, envError)) {
                ImGui::TextDisabled("DDS SKYBOX DIRECTORY NOT FOUND: gfx/env");
            }
        }

        if (editorCfg.skyboxName.empty() && !skyboxes.empty()) {
            const std::string defaultSkybox = skyboxes.front();
            const GLuint loaded = LoadDDS_Cubemap((skyboxEnvPath / defaultSkybox).string());
            if (loaded != 0) {
                skyboxTexture = loaded;
                editorCfg.skyboxName = defaultSkybox;
            }
        }

        ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::clamp(width * 0.62f, 620.0f, 820.0f), std::clamp(height * 0.62f, 500.0f, 660.0f)), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
        if (ImGui::Begin("Settings", &g_showSettings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
            if (ImGui::BeginTabBar("SettingsTabs")) {
                if (ImGui::BeginTabItem("General")) {
                    ImGui::TextUnformatted("Game Root Directory");
                    ImGui::SetNextItemWidth(-120.0f);
                    ImGui::InputText("##SettingsGameRoot", settingsGamePath, sizeof(settingsGamePath));
                    ImGui::SameLine();
                    if (ImGui::Button("Apply")) {
                        fs::path newPath(settingsGamePath);
                        std::error_code ec;
                        newPath = fs::weakly_canonical(newPath, ec);
                        if (!ec && fs::is_directory(newPath)) {
                            gameRootPath = newPath;
                            editorCfg.gamePath = gameRootPath.string();
                            g_browserRoot.clear();
                            refreshDataFunc(currentFileName, currentMatIndex);
                            RebuildBrowser();
                            std::strncpy(settingsGamePath, gameRootPath.string().c_str(), sizeof(settingsGamePath) - 1);
                        }
                    }
                    ImGui::Checkbox("Show FPS", &editorCfg.showFps);
                    ImGui::Checkbox("Allow Light Intensity > 5", &editorCfg.allowHighLightIntensity);
                    if (!editorCfg.allowHighLightIntensity) lightIntensity = std::clamp(lightIntensity, 0.0f, 5.0f);
                    if (ImGui::SliderFloat("FOV", &editorCfg.fov, 60.0f, 120.0f, "%.0f deg")) {
                        editorCfg.fov = std::clamp(editorCfg.fov, 60.0f, 120.0f);
                    }
                    ImGui::Checkbox("Unlimited Zoom In", &editorCfg.unlimitedZoom);
                    ShowTooltip("Removes the normal minimum camera distance. The camera can approach the model much more closely.");
                    ImGui::Checkbox("Load all WAD files on refresh", &editorCfg.autoLoadWads);
                    if (ImGui::Button("Refresh Files")) {
                        g_browserRoot.clear();
                        refreshDataFunc(currentFileName, currentMatIndex);
                        RebuildBrowser();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Viewport")) {
                    ImGui::TextUnformatted("Editor Background");
                    ImGui::ColorEdit3("Background Color", editorCfg.backgroundColor);
                    ImGui::Separator();
                    ImGui::TextUnformatted("Model Preview Skybox");
                    const char* currentSkybox = editorCfg.skyboxName.empty() ? "None" : editorCfg.skyboxName.c_str();
                    if (ImGui::BeginCombo("Skybox", currentSkybox)) {
                        if (ImGui::Selectable("None", editorCfg.skyboxName.empty())) {
                            editorCfg.skyboxName.clear();
                            if (skyboxTexture) {
                                glDeleteTextures(1, &skyboxTexture);
                                skyboxTexture = 0;
                            }
                        }
                        for (const std::string& name : skyboxes) {
                            const bool selected = ToLower(editorCfg.skyboxName) == ToLower(name);
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                const GLuint loaded = LoadDDS_Cubemap((skyboxEnvPath / name).string());
                                if (loaded != 0) {
                                    if (skyboxTexture) glDeleteTextures(1, &skyboxTexture);
                                    skyboxTexture = loaded;
                                    editorCfg.skyboxName = name;
                                }
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ShowTooltip("Model viewport skybox only. Matched DDS sets are read from gfx/env using the six suffixes: bk, lf, rt, ft, up and dn.");
                    if (skyboxes.empty()) ImGui::TextDisabled("NO COMPLETE DDS SKYBOXES IN gfx/env");
                    ImGui::TextDisabled("Expected six files: namebk, namelf, namert, nameft, nameup, namedn");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 148.0f);
            if (ImGui::Button("Save Settings", ImVec2(140.0f, 0.0f))) {
                editorCfg.gamePath = gameRootPath.string();
                editorCfg.lightMode = lightMode;
                SaveConfig(editorCfg);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
    }

    if (g_showAbout) ImGui::OpenPopup("About MatEdit");
    const float aboutWidth = std::clamp(width * 0.62f, 560.0f, 760.0f);
    ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(aboutWidth, 0.0f), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("About MatEdit", &g_showAbout, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::Text("MatEdit");
        ImGui::Separator();
        ImGui::TextWrapped("Material Editor for PrimeXT and similar projects running on Xash3D / Xash3D FWGS.");
        ImGui::TextWrapped("Created for editing game materials, textures and related material definitions used by these projects.");
        ImGui::Spacing();
        ImGui::TextWrapped("Author: hgruntt");
        ImGui::TextWrapped("License: GPL-3.0");
        ImGui::TextWrapped("MatEdit is absolutely free. If somebody charged you money for this program, you were scammed.");
        ImGui::Spacing();
        ImGui::TextWrapped("Third-party components include Dear ImGui, GLFW, GLM, GLI and GLAD, each distributed under its respective license.");
        if (ImGui::Button("OK", ImVec2(140.0f, 0.0f))) {
            g_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    (void)defFiles;
    (void)ddsFiles;
}

void ToggleEditorPanels() {
    const bool show = !(g_showBrowser || g_showTexturePreview || g_showMaterialEditor);
    g_showBrowser = show;
    g_showTexturePreview = show;
    g_showMaterialEditor = show;
}

bool IsEditorViewportHovered() {
    return g_viewportHovered;
}

void GetEditorViewportRect(float& x, float& y, float& w, float& h) {
    x = g_viewportPos.x;
    y = g_viewportPos.y;
    w = g_viewportSize.x;
    h = g_viewportSize.y;
}

void DrawEditorViewportTexture(GLuint texture) {
    if (texture == 0 || g_viewportSize.x <= 1.0f || g_viewportSize.y <= 1.0f) return;
    ImDrawList* drawList = g_viewportDrawList ? g_viewportDrawList : ImGui::GetBackgroundDrawList();
    const float rounding = ImGui::GetStyle().ChildRounding;
    const ImVec2 maxPos(g_viewportPos.x + g_viewportSize.x, g_viewportPos.y + g_viewportSize.y);
    drawList->PushClipRect(g_viewportPos, maxPos, true);
    drawList->AddImageRounded(static_cast<ImTextureID>(texture), g_viewportPos, maxPos,
                              ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
                              IM_COL32_WHITE, rounding);
    drawList->AddRect(g_viewportPos, maxPos, ImGui::GetColorU32(ImGuiCol_Border), rounding);
    drawList->PopClipRect();
}

void DrawEditorViewportFPS(float fps) {
    if (!g_viewportDrawList || g_viewportSize.x <= 1.0f || g_viewportSize.y <= 1.0f) return;
    const ImVec2 maxPos(g_viewportPos.x + g_viewportSize.x, g_viewportPos.y + g_viewportSize.y);
    const char* textFormat = "FPS: %.1f";
    char text[32]{};
    std::snprintf(text, sizeof(text), textFormat, fps);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 8.0f;
    const float padY = 5.0f;
    const ImVec2 textPos(maxPos.x - textSize.x - 14.0f, g_viewportPos.y + 10.0f);
    const ImVec2 bgMin(textPos.x - padX, textPos.y - padY);
    const ImVec2 bgMax(textPos.x + textSize.x + padX, textPos.y + textSize.y + padY);
    g_viewportDrawList->PushClipRect(g_viewportPos, maxPos, true);
    g_viewportDrawList->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 150), 4.0f);
    g_viewportDrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), text);
    g_viewportDrawList->PopClipRect();
}

void DrawEditorViewportFreeCamHint() {
    if (!g_viewportDrawList || g_viewportSize.x <= 1.0f || g_viewportSize.y <= 1.0f) return;
    const ImVec2 maxPos(g_viewportPos.x + g_viewportSize.x, g_viewportPos.y + g_viewportSize.y);
    const char* text = "FreeCam - Z";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 textPos(g_viewportPos.x + 10.0f, g_viewportPos.y + 10.0f);
    const ImVec2 bgMin(textPos.x - 8.0f, textPos.y - 5.0f);
    const ImVec2 bgMax(textPos.x + textSize.x + 8.0f, textPos.y + textSize.y + 5.0f);
    g_viewportDrawList->PushClipRect(g_viewportPos, maxPos, true);
    g_viewportDrawList->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 150), 4.0f);
    g_viewportDrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), text);
    g_viewportDrawList->PopClipRect();
}

void ReleaseEditorUIPreview() {
    ReleaseTexturePreview(g_preview.info);
    g_preview.reference.clear();
    g_preview.kind.clear();
}

void InitUI() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.93f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
    c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.16f, 0.19f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.32f, 0.38f, 0.55f);
    c[ImGuiCol_FrameBg] = ImVec4(0.21f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.31f, 0.39f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.38f, 0.48f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.36f, 0.54f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.46f, 0.69f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.55f, 0.83f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.36f, 0.54f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.55f, 0.83f, 1.0f);
}

