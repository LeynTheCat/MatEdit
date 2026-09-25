#include "Config.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::filesystem::path GetConfigPath() {
#ifdef _WIN32
    char buffer[4096] = {};
    unsigned long length = GetModuleFileNameA(nullptr, buffer, static_cast<unsigned long>(sizeof(buffer)));
    if (length > 0 && length < sizeof(buffer)) {
        return std::filesystem::path(buffer).parent_path() / "editor_config.txt";
    }
#else
    std::error_code ec;
    std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) return executable.parent_path() / "editor_config.txt";
#endif
    return std::filesystem::current_path() / "editor_config.txt";
}
}

void LoadConfig(EditorConfig& cfg) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    if (lines.size() < 8) return;

    cfg.gamePath = lines[0];
    cfg.lastMatFile = lines[1];
    try { cfg.shapeType = std::stoi(lines[2]); } catch (...) {}
    try { cfg.lightMode = std::stoi(lines[3]); } catch (...) {}

    float probeR = 0.0f, probeG = 0.0f, probeB = 0.0f;
    const bool newFormat = lines.size() >= 15 && std::sscanf(lines[8].c_str(), "%f %f %f", &probeR, &probeG, &probeB) == 3;
    if (newFormat) {
        try { cfg.dynamicLightSpeed = std::stof(lines[4]); } catch (...) {}
        try { cfg.dynamicLightRadius = std::stof(lines[5]); } catch (...) {}
        cfg.autoLoadWads = lines[6] != "0";
        try { cfg.backgroundMode = std::stoi(lines[7]); } catch (...) {}
        float r = cfg.backgroundColor[0], g = cfg.backgroundColor[1], b = cfg.backgroundColor[2];
        if (std::sscanf(lines[8].c_str(), "%f %f %f", &r, &g, &b) == 3) {
            cfg.backgroundColor[0] = r;
            cfg.backgroundColor[1] = g;
            cfg.backgroundColor[2] = b;
        }
        cfg.skyboxName.clear();
        cfg.useNormal = lines[10] == "1";
        cfg.useGloss = lines[11] == "1";
        cfg.useLuma = lines[12] == "1";
        cfg.useBump = lines[13] == "1";
        cfg.loadedWads.clear();
        if (lines.size() > 14) {
            try {
                const int count = (std::max)(0, std::stoi(lines[14]));
                for (int i = 0; i < count && 15 + i < static_cast<int>(lines.size()); ++i) {
                    if (!lines[15 + i].empty()) cfg.loadedWads.push_back(lines[15 + i]);
                }
                const int skyboxLine = 15 + count;
                if (skyboxLine < static_cast<int>(lines.size()) && lines[skyboxLine].rfind("SKYBOX=", 0) == 0) {
                    cfg.skyboxName = lines[skyboxLine].substr(7);
                }
            } catch (...) {}
        }
        for (const std::string& configLine : lines) {
            if (configLine.rfind("SHOW_FPS=", 0) == 0) cfg.showFps = configLine.substr(9) != "0";
            else if (configLine.rfind("HIGH_LIGHT_INTENSITY=", 0) == 0) cfg.allowHighLightIntensity = configLine.substr(21) != "0";
            else if (configLine.rfind("FOV=", 0) == 0) {
                try { cfg.fov = std::clamp(std::stof(configLine.substr(4)), 60.0f, 120.0f); } catch (...) {}
            } else if (configLine.rfind("UNLIMITED_ZOOM=", 0) == 0) {
                cfg.unlimitedZoom = configLine.substr(15) != "0";
            }
        }
        return;
    }

    cfg.useNormal = lines[4] == "1";
    cfg.useGloss = lines[5] == "1";
    cfg.useLuma = lines[6] == "1";
    cfg.useBump = lines[7] == "1";
    cfg.loadedWads.clear();
    if (lines.size() > 8) {
        try {
            const int count = (std::max)(0, std::stoi(lines[8]));
            for (int i = 0; i < count && 9 + i < static_cast<int>(lines.size()); ++i) {
                if (!lines[9 + i].empty()) cfg.loadedWads.push_back(lines[9 + i]);
            }
        } catch (...) {}
    }
    for (const std::string& configLine : lines) {
        if (configLine.rfind("SHOW_FPS=", 0) == 0) cfg.showFps = configLine.substr(9) != "0";
        else if (configLine.rfind("HIGH_LIGHT_INTENSITY=", 0) == 0) cfg.allowHighLightIntensity = configLine.substr(21) != "0";
        else if (configLine.rfind("FOV=", 0) == 0) {
            try { cfg.fov = std::clamp(std::stof(configLine.substr(4)), 60.0f, 120.0f); } catch (...) {}
        } else if (configLine.rfind("UNLIMITED_ZOOM=", 0) == 0) {
            cfg.unlimitedZoom = configLine.substr(15) != "0";
        }
    }
}

void SaveConfig(const EditorConfig& cfg) {
    std::ofstream file(GetConfigPath());
    if (!file.is_open()) return;

    file << cfg.gamePath << "\n";
    file << cfg.lastMatFile << "\n";
    file << cfg.shapeType << "\n";
    file << cfg.lightMode << "\n";
    file << cfg.dynamicLightSpeed << "\n";
    file << cfg.dynamicLightRadius << "\n";
    file << (cfg.autoLoadWads ? "1" : "0") << "\n";
    file << cfg.backgroundMode << "\n";
    file << cfg.backgroundColor[0] << " " << cfg.backgroundColor[1] << " " << cfg.backgroundColor[2] << "\n";
    file << "" << "\n";
    file << (cfg.useNormal ? "1" : "0") << "\n";
    file << (cfg.useGloss ? "1" : "0") << "\n";
    file << (cfg.useLuma ? "1" : "0") << "\n";
    file << (cfg.useBump ? "1" : "0") << "\n";
    file << cfg.loadedWads.size() << "\n";
    for (const auto& wad : cfg.loadedWads) file << wad << "\n";
    file << "SKYBOX=" << cfg.skyboxName << "\n";
    file << "SHOW_FPS=" << (cfg.showFps ? "1" : "0") << "\n";
    file << "HIGH_LIGHT_INTENSITY=" << (cfg.allowHighLightIntensity ? "1" : "0") << "\n";
    file << "FOV=" << cfg.fov << "\n";
    file << "UNLIMITED_ZOOM=" << (cfg.unlimitedZoom ? "1" : "0") << "\n";
}
