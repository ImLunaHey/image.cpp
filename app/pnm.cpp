#include "pnm.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace imagecpp::app {
namespace {

std::string read_token(std::istream &input) {
    std::string token;
    char character = '\0';
    while (input.get(character)) {
        const auto value = static_cast<unsigned char>(character);
        if (character == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (!std::isspace(value)) {
            token.push_back(character);
            break;
        }
    }
    while (input.get(character)) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isspace(value)) {
            break;
        }
        if (character == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        token.push_back(character);
    }
    if (token.empty()) {
        throw std::runtime_error("unexpected end of PNM header");
    }
    return token;
}

uint32_t parse_dimension(const std::string &token, const char *name) {
    size_t consumed = 0;
    unsigned long value = 0;
    try {
        value = std::stoul(token, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid PNM ") + name);
    }
    if (consumed != token.size() || value == 0 || value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("invalid PNM ") + name);
    }
    return static_cast<uint32_t>(value);
}

} // namespace

Image read_pnm(const std::string &filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input image: " + filename);
    }

    const std::string magic = read_token(input);
    imagecpp_pixel_format format = IMAGECPP_PIXEL_FORMAT_UNKNOWN;
    if (magic == "P5") {
        format = IMAGECPP_PIXEL_FORMAT_GRAY_U8;
    } else if (magic == "P6") {
        format = IMAGECPP_PIXEL_FORMAT_RGB_U8;
    } else {
        throw std::runtime_error("only binary P5 and P6 PNM images are supported");
    }

    const uint32_t width = parse_dimension(read_token(input), "width");
    const uint32_t height = parse_dimension(read_token(input), "height");
    const uint32_t max_value = parse_dimension(read_token(input), "maximum value");
    if (max_value != 255) {
        throw std::runtime_error("only 8-bit PNM images with maximum value 255 are supported");
    }

    imagecpp_image_desc desc{
        sizeof(imagecpp_image_desc), width, height, 0, format, IMAGECPP_COLOR_SPACE_SRGB,
    };
    Image image(desc);
    imagecpp_image_view view = image.view();
    const size_t row_bytes = static_cast<size_t>(width) * imagecpp_pixel_format_bytes_per_pixel(format);
    auto *bytes = static_cast<uint8_t *>(view.data);
    for (uint32_t row = 0; row < height; ++row) {
        input.read(reinterpret_cast<char *>(bytes + static_cast<size_t>(row) * view.row_stride),
                   static_cast<std::streamsize>(row_bytes));
        if (!input) {
            throw std::runtime_error("PNM pixel data is truncated");
        }
    }
    return image;
}

void write_pnm(const std::string &filename, const imagecpp_const_image_view &image) {
    imagecpp_error error{};
    check(imagecpp_validate_const_image_view(&image, &error), error);
    const char *magic = nullptr;
    if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_GRAY_U8) {
        magic = "P5";
    } else if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGB_U8) {
        magic = "P6";
    } else {
        throw std::runtime_error("PNM output supports only GRAY_U8 and RGB_U8 images");
    }

    std::ofstream output(filename, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open output image: " + filename);
    }
    output << magic << '\n' << image.width << ' ' << image.height << "\n255\n";

    const size_t row_bytes =
        static_cast<size_t>(image.width) * imagecpp_pixel_format_bytes_per_pixel(image.pixel_format);
    const auto *bytes = static_cast<const uint8_t *>(image.data);
    for (uint32_t row = 0; row < image.height; ++row) {
        output.write(reinterpret_cast<const char *>(bytes + static_cast<size_t>(row) * image.row_stride),
                     static_cast<std::streamsize>(row_bytes));
    }
    if (!output) {
        throw std::runtime_error("failed to write output image: " + filename);
    }
}

} // namespace imagecpp::app
