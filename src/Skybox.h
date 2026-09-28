#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <glad/glad.h>

std::vector<std::string> FindSkyboxNames(const std::filesystem::path& envPath);
GLuint LoadSkyboxCubemap(const std::string& path);
