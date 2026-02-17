#include "PPM.h"
#include <fstream>
#include <algorithm>

void PPM::Save(const std::string& filepath, const std::vector<glm::vec4>& pixels, uint32_t width, uint32_t height, bool useGamma) {
    std::ofstream out(filepath, std::ios::binary);
    if (!out)
        throw std::runtime_error("Failed to open file for writing PPM");

    auto linearToSRGB = [](float c) {
        c = std::clamp(c, 0.f, 1.f);
        return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        };

    // Header
    out << "P6\n" << width << " " << height << "\n255\n";

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const glm::vec4& c = pixels[y * width + x];
            float r = std::clamp(c.r, 0.0f, 1.0f);
            float g = std::clamp(c.g, 0.0f, 1.0f);
            float b = std::clamp(c.b, 0.0f, 1.0f);

            if (useGamma) {
                r = linearToSRGB(r);
                g = linearToSRGB(g);
                b = linearToSRGB(b);
            }

            unsigned char rgb[3];
            rgb[0] = static_cast<unsigned char>(r * 255.0f);
            rgb[1] = static_cast<unsigned char>(g * 255.0f);
            rgb[2] = static_cast<unsigned char>(b * 255.0f);

            out.write(reinterpret_cast<char*>(rgb), 3);
        }
    }

    out.close();
}

void PPM::Load(const std::string& filepath, std::vector<glm::vec4>& pixels, uint32_t& width, uint32_t& height) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open PPM file: " + filepath);

    std::string magic;
    file >> magic; // P6
    if (magic != "P6")
        throw std::runtime_error("Unsupported PPM format: " + magic);

    // Skip comments
    char c = file.peek();
    while (c == '#') {
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        c = file.peek();
    }

    int maxval;
    file >> width >> height >> maxval;

    file.get(); // consume single whitespace/newline after header

    pixels.reserve(width * height);

    for (int i = 0; i < width * height; ++i) {
        unsigned char rgb[3];
        file.read(reinterpret_cast<char*>(rgb), 3);

        glm::vec4 pixel;
        pixel.r = float(rgb[0]) / maxval;
        pixel.g = float(rgb[1]) / maxval;
        pixel.b = float(rgb[2]) / maxval;
        pixel.a = 1.0f; // alpha channel

        pixels.push_back(pixel);
    }
}