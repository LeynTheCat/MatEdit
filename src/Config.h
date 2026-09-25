#pragma once
#include <string>
#include <vector>
#define _CRT_SECURE_NO_WARNINGS

struct EditorConfig {
    std::string gamePath = "";
    std::string lastMatFile = "";
    int shapeType = 0;
    int lightMode = 0;
    float dynamicLightSpeed = 1.0f;
    float dynamicLightRadius = 3.0f;
    bool autoLoadWads = true;
    int backgroundMode = 0;
    float backgroundColor[3] = {0.10f, 0.10f, 0.10f};
    std::string skyboxName = "";
    bool useNormal = true;
    bool useGloss = true;
    bool useLuma = true;
    bool useBump = true;
    bool showFps = true;
    bool allowHighLightIntensity = false;
    float fov = 90.0f;
    bool unlimitedZoom = false;
    std::vector<std::string> loadedWads;
};

void LoadConfig(EditorConfig& cfg);
void SaveConfig(const EditorConfig& cfg);
