#include "depth_command.hpp"

#include "imagecpp/imagecpp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

int32_t positive_int32(const std::string &value, const char *name) {
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (consumed != value.size() || parsed == 0 ||
        parsed > static_cast<unsigned long>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    return static_cast<int32_t>(parsed);
}

imagecpp::Image visualized_depth(const imagecpp_const_image_view &depth, bool invert) {
    if (depth.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_F32 || depth.data == nullptr) {
        throw std::runtime_error("depth provider returned an invalid float map");
    }
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (uint32_t row = 0; row < depth.height; ++row) {
        const auto *values = reinterpret_cast<const float *>(static_cast<const uint8_t *>(depth.data) +
                                                             static_cast<size_t>(row) * depth.row_stride);
        for (uint32_t column = 0; column < depth.width; ++column) {
            if (std::isfinite(values[column])) {
                minimum = std::min(minimum, values[column]);
                maximum = std::max(maximum, values[column]);
            }
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        throw std::runtime_error("depth provider returned no finite values");
    }

    const imagecpp_image_desc description{
        sizeof(imagecpp_image_desc),  depth.width, depth.height, 0, IMAGECPP_PIXEL_FORMAT_GRAY_U8,
        IMAGECPP_COLOR_SPACE_UNKNOWN,
    };
    imagecpp::Image output(description);
    imagecpp_image_view output_view = output.view();
    const float range = maximum - minimum;
    for (uint32_t row = 0; row < depth.height; ++row) {
        const auto *input = reinterpret_cast<const float *>(static_cast<const uint8_t *>(depth.data) +
                                                            static_cast<size_t>(row) * depth.row_stride);
        auto *destination =
            static_cast<uint8_t *>(output_view.data) + static_cast<size_t>(row) * output_view.row_stride;
        for (uint32_t column = 0; column < depth.width; ++column) {
            float normalized = range > 0.0F && std::isfinite(input[column]) ? (input[column] - minimum) / range : 0.0F;
            if (invert) {
                normalized = 1.0F - normalized;
            }
            destination[column] = static_cast<uint8_t>(std::clamp(normalized * 255.0F + 0.5F, 0.0F, 255.0F));
        }
    }
    return output;
}

} // namespace

int depth_command(int argc, char **argv) {
    if (argc < 5) {
        throw std::runtime_error("depth requires a model, input image, and output image");
    }
    bool include_pose = false;
    bool invert = true;
    int32_t threads = 0;
    for (int index = 5; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--pose") {
            include_pose = true;
        } else if (option == "--no-invert") {
            invert = false;
        } else if (option == "--threads") {
            if (++index >= argc) {
                throw std::runtime_error("--threads requires a count");
            }
            threads = positive_int32(argv[index], "thread count");
        } else {
            throw std::runtime_error("unknown depth option: " + option);
        }
    }

    imagecpp::Runtime runtime;
    imagecpp_model_options model_options{};
    imagecpp_model_options_init(&model_options);
    model_options.model_path = argv[2];
    model_options.threads = threads;
    imagecpp::Model model(runtime, "image.depth.depth-anything", model_options);
    imagecpp::Image input = imagecpp::load(argv[3]);
    imagecpp_depth_options options{};
    imagecpp_depth_options_init(&options);
    options.include_pose = include_pose ? 1 : 0;
    imagecpp::DepthResult result = imagecpp::depth(model, input, options);
    const imagecpp_depth_info info = result.info();
    imagecpp::Image visualization = visualized_depth(info.depth, invert);
    imagecpp::save(argv[4], visualization);
    std::cout << "estimated " << info.depth.width << 'x' << info.depth.height << ' '
              << (info.is_metric != 0 ? "metric" : "relative") << " depth\n";
    if (info.has_pose != 0) {
        std::cout << "extrinsics:";
        for (float value : info.extrinsics) {
            std::cout << ' ' << value;
        }
        std::cout << "\nintrinsics:";
        for (float value : info.intrinsics) {
            std::cout << ' ' << value;
        }
        std::cout << '\n';
    }
    return 0;
}
