#pragma once
#include <string>

bool IsRasterImagePath(const std::string& path);
bool LoadRasterImage(const std::string& path, unsigned char*& pixels, int& width, int& height, int& channels);
void FreeRasterImage(unsigned char* pixels);
