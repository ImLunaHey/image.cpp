#include "imagecpp/imagecpp.hpp"
#include "model_commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream &output) {
    output << "image.cpp " << imagecpp_version_string() << '\n'
           << "usage:\n"
           << "  imagecpp inspect\n"
           << "  imagecpp resize <input-image> <output-image> <width>x<height> [nearest|bilinear]\n"
           << "  imagecpp segment <model> <input-image> <output-mask> [prompt options]\n"
           << "  imagecpp remove-background <model> <input-image> <output-image> [prompt options]\n"
           << "  imagecpp generate <model> <output-image> <prompt> [generation options]\n"
           << "  imagecpp edit <model> <input-image> <output-image> <prompt> [generation options]\n"
           << "  imagecpp upscale <model> <input-image> <output-image> [upscale options]\n"
           << "\nprompt options:\n"
           << "  --point <x>,<y>        positive point (repeatable)\n"
           << "  --negative <x>,<y>     negative point (repeatable)\n"
           << "  --box <x0>,<y0>,<x1>,<y1>\n"
           << "  --multimask            ask the model for multiple candidates\n"
           << "  --cpu | --gpu          select a compute device (default: auto)\n"
           << "  --threads <count>      CPU worker threads\n"
           << "\nformats: PNG, JPEG, WebP, BMP, and TGA (selected by output extension)\n";
    print_model_command_usage(output);
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

float parse_coordinate(const std::string &value) {
    size_t consumed = 0;
    float parsed = 0.0F;
    try {
        parsed = std::stof(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error("invalid prompt coordinate");
    }
    if (consumed != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error("invalid prompt coordinate");
    }
    return parsed;
}

template <size_t Count> std::array<float, Count> parse_coordinates(const std::string &value) {
    std::array<float, Count> result{};
    size_t begin = 0;
    for (size_t index = 0; index < Count; ++index) {
        const size_t end = value.find(',', begin);
        if ((index + 1 < Count && end == std::string::npos) || (index + 1 == Count && end != std::string::npos)) {
            throw std::runtime_error("prompt coordinates have the wrong number of values");
        }
        result[index] = parse_coordinate(value.substr(begin, end == std::string::npos ? end : end - begin));
        begin = end == std::string::npos ? value.size() : end + 1;
    }
    return result;
}

struct PromptArguments {
    std::string model_path;
    std::string input_path;
    std::string output_path;
    std::vector<imagecpp_point_prompt> points;
    imagecpp_box box{};
    bool use_box = false;
    bool multimask = false;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
    int32_t threads = 0;
};

PromptArguments parse_prompt_arguments(int argc, char **argv) {
    if (argc < 5) {
        throw std::runtime_error("segment commands require a model, input image, output image, and prompt");
    }
    PromptArguments result;
    result.model_path = argv[2];
    result.input_path = argv[3];
    result.output_path = argv[4];
    for (int index = 5; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--point" || option == "--negative") {
            if (++index >= argc) {
                throw std::runtime_error(option + " requires coordinates");
            }
            const auto coordinates = parse_coordinates<2>(argv[index]);
            result.points.push_back({coordinates[0], coordinates[1], option == "--point" ? 1 : 0});
        } else if (option == "--box") {
            if (++index >= argc) {
                throw std::runtime_error("--box requires coordinates");
            }
            const auto coordinates = parse_coordinates<4>(argv[index]);
            result.box = {coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
            result.use_box = true;
        } else if (option == "--multimask") {
            result.multimask = true;
        } else if (option == "--cpu") {
            result.device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            result.device = IMAGECPP_DEVICE_GPU;
        } else if (option == "--threads") {
            if (++index >= argc) {
                throw std::runtime_error("--threads requires a count");
            }
            const uint32_t count = parse_size_part(argv[index], "thread count");
            if (count > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("thread count is too large");
            }
            result.threads = static_cast<int32_t>(count);
        } else {
            throw std::runtime_error("unknown prompt option: " + option);
        }
    }
    if (result.points.empty() && !result.use_box) {
        throw std::runtime_error("provide at least one --point, --negative, or --box prompt");
    }
    return result;
}

struct SegmentationRun {
    imagecpp::Image source;
    imagecpp::SegmentResult result;
};

SegmentationRun run_segmentation(const PromptArguments &arguments) {
    imagecpp::Runtime runtime;
    imagecpp_model_options model_options{};
    imagecpp_model_options_init(&model_options);
    model_options.model_path = arguments.model_path.c_str();
    model_options.threads = arguments.threads;
    model_options.device = arguments.device;
    imagecpp::Model model(runtime, "image.segment.sam", model_options);
    imagecpp::Session session(model);
    imagecpp::Image source = imagecpp::load(arguments.input_path);
    session.set_image(source);

    imagecpp_segment_options segment_options{};
    imagecpp_segment_options_init(&segment_options);
    segment_options.points = arguments.points.data();
    segment_options.point_count = arguments.points.size();
    segment_options.box = arguments.box;
    segment_options.use_box = arguments.use_box ? 1 : 0;
    segment_options.multimask = arguments.multimask ? 1 : 0;
    imagecpp::SegmentResult result = session.segment(segment_options);
    if (result.size() == 0) {
        throw std::runtime_error("the model returned no masks for this prompt");
    }
    return {std::move(source), std::move(result)};
}

size_t best_mask(const imagecpp::SegmentResult &result) {
    size_t best = 0;
    for (size_t index = 1; index < result.size(); ++index) {
        if (result.at(index).iou_score > result.at(best).iou_score) {
            best = index;
        }
    }
    return best;
}

int segment_image(int argc, char **argv) {
    const PromptArguments arguments = parse_prompt_arguments(argc, argv);
    SegmentationRun run = run_segmentation(arguments);
    const size_t best = best_mask(run.result);
    const imagecpp::SegmentInfo info = run.result.at(best);
    imagecpp::save(arguments.output_path, info.mask);
    std::cout << "masks: " << run.result.size() << ", selected: " << best << ", IoU score: " << info.iou_score << '\n';
    return 0;
}

int remove_background(int argc, char **argv) {
    const PromptArguments arguments = parse_prompt_arguments(argc, argv);
    SegmentationRun run = run_segmentation(arguments);
    const imagecpp_const_image_view source = static_cast<const imagecpp::Image &>(run.source).view();
    const imagecpp::SegmentInfo selection = run.result.at(best_mask(run.result));
    if (selection.mask.width != source.width || selection.mask.height != source.height) {
        throw std::runtime_error("segmentation mask dimensions do not match the source image");
    }

    imagecpp_image_desc output_desc{
        sizeof(imagecpp_image_desc), source.width, source.height, 0, IMAGECPP_PIXEL_FORMAT_RGBA_U8,
        IMAGECPP_COLOR_SPACE_SRGB,
    };
    imagecpp::Image output(output_desc);
    imagecpp_image_view output_view = output.view();
    const auto *source_bytes = static_cast<const uint8_t *>(source.data);
    const auto *mask_bytes = static_cast<const uint8_t *>(selection.mask.data);
    auto *output_bytes = static_cast<uint8_t *>(output_view.data);
    for (uint32_t row = 0; row < source.height; ++row) {
        const uint8_t *source_row = source_bytes + static_cast<size_t>(row) * source.row_stride;
        const uint8_t *mask_row = mask_bytes + static_cast<size_t>(row) * selection.mask.row_stride;
        uint8_t *output_row = output_bytes + static_cast<size_t>(row) * output_view.row_stride;
        for (uint32_t column = 0; column < source.width; ++column) {
            const size_t output_offset = static_cast<size_t>(column) * 4;
            uint8_t original_alpha = 255;
            switch (source.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                output_row[output_offset] = source_row[column];
                output_row[output_offset + 1] = source_row[column];
                output_row[output_offset + 2] = source_row[column];
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8:
                std::memcpy(output_row + output_offset, source_row + static_cast<size_t>(column) * 3, 3);
                break;
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
                std::memcpy(output_row + output_offset, source_row + static_cast<size_t>(column) * 4, 3);
                original_alpha = source_row[static_cast<size_t>(column) * 4 + 3];
                break;
            default:
                throw std::runtime_error("remove-background supports gray, RGB, or RGBA source images");
            }
            output_row[output_offset + 3] =
                static_cast<uint8_t>((static_cast<unsigned>(original_alpha) * mask_row[column] + 127U) / 255U);
        }
    }
    imagecpp::save(arguments.output_path, output);
    return 0;
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
        if (argc >= 2 && std::string(argv[1]) == "segment") {
            return segment_image(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "remove-background") {
            return remove_background(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "generate") {
            return generate_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "edit") {
            return edit_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "upscale") {
            return upscale_image_command(argc, argv);
        }
        print_usage(argc == 1 ? std::cout : std::cerr);
        return argc == 1 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << "imagecpp: " << error.what() << '\n';
        return 1;
    }
}
