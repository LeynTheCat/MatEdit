#pragma once
#include <glad/glad.h>

struct TAATarget {
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint history = 0;
    int width = 0;
    int height = 0;
    bool historyValid = false;
};

struct FXAATarget {
    GLuint fbo = 0;
    GLuint color = 0;
    int width = 0;
    int height = 0;
};

GLuint LoadTAAShader();
void DestroyTAATarget(TAATarget& target);
bool EnsureTAATarget(TAATarget& target, int width, int height);
void RenderTAA(GLuint shader, GLuint sourceTexture, TAATarget& target, bool resetHistory);
GLuint LoadFXAAShader();
void DestroyFXAATarget(FXAATarget& target);
bool EnsureFXAATarget(FXAATarget& target, int width, int height);
void RenderFXAA(GLuint shader, GLuint sourceTexture, FXAATarget& target);
void DestroyFXAAResources(GLuint& shader);
