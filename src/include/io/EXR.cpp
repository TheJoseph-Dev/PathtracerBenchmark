#include "EXR.h"
#define TINYEXR_NO_EXR_READER
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

void EXR::Save(const std::string& filepath, const std::vector<glm::vec4>& pixels, uint32_t width, uint32_t height) {
    if (pixels.size() != width * height)
        throw std::runtime_error("EXR::Save - pixel buffer size mismatch");

    std::vector<float> data(width * height * 3);

    for (uint32_t i = 0; i < width * height; ++i) {
        data[3 * i + 0] = pixels[i].r;
        data[3 * i + 1] = pixels[i].g;
        data[3 * i + 2] = pixels[i].b;
    }

    const char* err = nullptr;
    int ret = SaveEXR(
        data.data(),
        width,
        height,
        3,      // RGB
        0,      // fp32
        filepath.c_str(),
        &err
    );

    if (ret != TINYEXR_SUCCESS) {
        std::string msg = err ? err : "Unknown EXR error";
        FreeEXRErrorMessage(err);
        throw std::runtime_error("SaveEXR failed: " + msg);
    }
}

void EXR::Load(const std::string& filepath, std::vector<glm::vec4>& pixels, uint32_t& width, uint32_t& height) {
    float* out = nullptr;
    const char* err = nullptr;

    int w, h;
    int ret = LoadEXR(&out, &w, &h, filepath.c_str(), &err);

    if (ret != TINYEXR_SUCCESS) {
        std::string msg = err ? err : "Unknown EXR error";
        FreeEXRErrorMessage(err);
        throw std::runtime_error("LoadEXR failed: " + msg);
    }

    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);

    // TinyEXR loads RGBA
    pixels.resize(width * height);

    for (uint32_t i = 0; i < width * height; ++i) {
        pixels[i].r = out[4 * i + 0];
        pixels[i].g = out[4 * i + 1];
        pixels[i].b = out[4 * i + 2];
        pixels[i].a = 1.0f; // ignore EXR alpha
    }

    free(out);
}