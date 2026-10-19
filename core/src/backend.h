#pragma once
#include <stdint.h>
#include <string>

namespace backend {
    int init(std::string resDir = "");
    void beginFrame();
    void render(bool vsync = true);
    void getMouseScreenPos(double& x, double& y);
    void setMouseScreenPos(double x, double y);
    int renderLoop();
    int end();
    // Incremented every time the GL context is (re)created. On Android the
    // context is destroyed when the window goes away, which invalidates all
    // texture names; code keeping textures must recreate them when this changes.
    uint64_t glContextGeneration();

}

