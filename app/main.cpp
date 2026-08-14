#include "imagecpp/imagecpp.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void print_usage(std::ostream &output) {
    output << "image.cpp " << imagecpp_version_string() << '\n'
           << "usage:\n"
           << "  imagecpp inspect\n"
           << "  imagecpp resize <input-image> <output-image> <width>x<height> [nearest|bilinear]\n"
           << "\nformats: PNG, JPEG, WebP, BMP, and TGA (selected by output extension)\n";
}

uint32_t parse_size_part(const std::string &value, const char *name) {
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (consumed != value.size() || parsed == 0 || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    return static_cast<uint32_t>(parsed);
}

std::pair<uint32_t, uint32_t> parse_dimensions(const std::string &value) {
    const size_t separator = value.find_first_of("xX");
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        throw std::runtime_error("dimensions must use <width>x<height>");
    }
    return {
        parse_size_part(value.substr(0, separator), "width"),
        parse_size_part(value.substr(separator + 1), "height"),
    };
}

imagecpp_resize_filter parse_filter(const std::string &value) {
    if (value == "nearest") {
        return IMAGECPP_RESIZE_NEAREST;
    }
    if (value == "bilinear") {
        return IMAGECPP_RESIZE_BILINEAR;
    }
    throw std::runtime_error("filter must be nearest or bilinear");
}

int inspect() {
    imagecpp::Runtime runtime;
    std::cout << "image.cpp " << imagecpp_version_string() << '\n';
    const auto operations = runtime.operations();
    std::cout << "operations: " << operations.size() << '\n';
    for (const auto &operation : operations) {
        std::cout << "  " << operation.id << " - " << operation.description << '\n';
    }
    return 0;
}

int resize_image(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        print_usage(std::cerr);
        return 2;
    }
    const auto [width, height] = parse_dimensions(argv[4]);
    const imagecpp_resize_filter filter = argc == 6 ? parse_filter(argv[5]) : IMAGECPP_RESIZE_BILINEAR;

    imagecpp::Image source = imagecpp::load(argv[2]);
    const imagecpp_const_image_view source_view = static_cast<const imagecpp::Image &>(source).view();
    imagecpp_image_desc destination_desc{
        sizeof(imagecpp_image_desc), width, height, 0, source_view.pixel_format, source_view.color_space,
    };
    imagecpp::Image destination(destination_desc);
    imagecpp_image_view destination_view = destination.view();
    imagecpp::resize(source_view, destination_view, filter);
    imagecpp::save(argv[3], static_cast<const imagecpp::Image &>(destination).view());
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "inspect") {
            return inspect();
        }
        if (argc >= 2 && std::string(argv[1]) == "resize") {
            return resize_image(argc, argv);
        }
        print_usage(argc == 1 ? std::cout : std::cerr);
        return argc == 1 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << "imagecpp: " << error.what() << '\n';
        return 1;
    }
}
