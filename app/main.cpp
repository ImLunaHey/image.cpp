#include "depth_command.hpp"
#include "detect_command.hpp"
#include "imagecpp/imagecpp.hpp"
#include "model_commands.hpp"
#include "ocr_command.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
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
           << "  imagecpp cutout <model> <input-image> <output-image> [workflow and prompt options]\n"
           << "  imagecpp generate <model> <output-image> <prompt> [generation options]\n"
           << "  imagecpp edit <model> <input-image> <output-image> <prompt> [generation options]\n"
           << "  imagecpp upscale <model> <input-image> <output-image> [upscale options]\n"
           << "  imagecpp depth <model> <input-image> <output-image> [--pose] [--no-invert] [--threads N]\n"
           << "  imagecpp embed-image <model> <input-image> [--threads N]\n"
           << "  imagecpp embed-text <model> <text> [--threads N]\n"
           << "  imagecpp classify <model> <input-image> <label> [label ...] [--threads N]\n"
           << "  imagecpp detect <model> <input-image> <text-prompt> [detection options]\n"
           << "  imagecpp ground <model> <input-image> <output-mask> <text-prompt> [detection options]\n"
           << "  imagecpp extract <model> <input-image> <output-image> <text-prompt> [workflow options]\n"
           << "  imagecpp ocr <traineddata-model> <input-image> [OCR options]\n"
           << "\nprompt options:\n"
           << "  --point <x>,<y>        positive point (repeatable)\n"
           << "  --negative <x>,<y>     negative point (repeatable)\n"
           << "  --box <x0>,<y0>,<x1>,<y1>\n"
           << "  --multimask            ask the model for multiple candidates\n"
           << "  --upscaler <model>      upscale the cutout with an ESRGAN model\n"
           << "  --factor <count>        cutout upscale factor (default: 4)\n"
           << "  --padding <pixels>      retain pixels around the mask when cropping\n"
           << "  --keep-canvas           retain the source canvas instead of cropping\n"
           << "  --cpu | --gpu          select a compute device (default: auto)\n"
           << "  --threads <count>      CPU worker threads\n"
           << "\nformats: PNG, JPEG, WebP, BMP, and TGA (selected by output extension)\n";
    print_model_command_usage(output);
    print_detect_command_usage(output);
    print_ocr_command_usage(output);
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

uint32_t parse_nonnegative_part(const std::string &value, const char *name) {
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (consumed != value.size() || parsed > std::numeric_limits<uint32_t>::max()) {
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
    std::string upscaler_path;
    uint32_t upscale_factor = 4;
    uint32_t padding = 0;
    bool crop_to_mask = true;
    bool upscale_factor_set = false;
};

PromptArguments parse_prompt_arguments(int argc, char **argv, bool workflow_options = false) {
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
        } else if (workflow_options && option == "--upscaler") {
            if (++index >= argc) {
                throw std::runtime_error("--upscaler requires a model path");
            }
            result.upscaler_path = argv[index];
        } else if (workflow_options && option == "--factor") {
            if (++index >= argc) {
                throw std::runtime_error("--factor requires a count");
            }
            result.upscale_factor = parse_size_part(argv[index], "upscale factor");
            result.upscale_factor_set = true;
        } else if (workflow_options && option == "--padding") {
            if (++index >= argc) {
                throw std::runtime_error("--padding requires a pixel count");
            }
            result.padding = parse_nonnegative_part(argv[index], "padding");
        } else if (workflow_options && option == "--keep-canvas") {
            result.crop_to_mask = false;
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
    if (workflow_options && result.upscaler_path.empty() && result.upscale_factor_set) {
        throw std::runtime_error("--factor requires --upscaler");
    }
    if (workflow_options && !result.upscaler_path.empty() && result.upscale_factor < 2) {
        throw std::runtime_error("cutout upscale factor must be at least two");
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

int run_cutout(const PromptArguments &arguments, bool keep_canvas) {
    imagecpp::Runtime runtime;
    imagecpp_model_options segment_model_options{};
    imagecpp_model_options_init(&segment_model_options);
    segment_model_options.model_path = arguments.model_path.c_str();
    segment_model_options.threads = arguments.threads;
    segment_model_options.device = arguments.device;
    imagecpp::Model segment_model(runtime, "image.segment.sam", segment_model_options);
    imagecpp::Session segment_session(segment_model);
    imagecpp::Image source = imagecpp::load(arguments.input_path);

    std::optional<imagecpp::Model> upscaler;
    if (!arguments.upscaler_path.empty()) {
        imagecpp_upscaler_model_options upscaler_options{};
        imagecpp_upscaler_model_options_init(&upscaler_options);
        upscaler_options.model_path = arguments.upscaler_path.c_str();
        upscaler_options.threads = arguments.threads;
        upscaler_options.device = arguments.device;
        upscaler.emplace(runtime, upscaler_options);
    }

    imagecpp_cutout_options options{};
    imagecpp_cutout_options_init(&options);
    options.segment.points = arguments.points.data();
    options.segment.point_count = arguments.points.size();
    options.segment.box = arguments.box;
    options.segment.use_box = arguments.use_box ? 1 : 0;
    options.segment.multimask = arguments.multimask ? 1 : 0;
    options.crop_to_mask = keep_canvas ? 0 : 1;
    options.padding = arguments.padding;
    options.upscale_factor = upscaler.has_value() ? arguments.upscale_factor : 1;

    imagecpp::CutoutResult result = imagecpp::cutout(segment_session, upscaler ? &*upscaler : nullptr, source, options);
    const imagecpp::CutoutInfo info = result.info();
    imagecpp::save(arguments.output_path, info.image);
    std::cout << "selected mask: " << info.selected_mask_index << ", IoU score: " << info.iou_score
              << ", output: " << info.image.width << 'x' << info.image.height << '\n';
    return 0;
}

int remove_background(int argc, char **argv) { return run_cutout(parse_prompt_arguments(argc, argv), true); }

int cutout_image(int argc, char **argv) {
    const PromptArguments arguments = parse_prompt_arguments(argc, argv, true);
    return run_cutout(arguments, !arguments.crop_to_mask);
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

int32_t parse_optional_threads(int argc, char **argv, int first_option) {
    int32_t threads = 0;
    for (int index = first_option; index < argc; ++index) {
        if (std::string(argv[index]) != "--threads" || ++index >= argc) {
            throw std::runtime_error("expected --threads <count>");
        }
        const uint32_t count = parse_size_part(argv[index], "thread count");
        if (count > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            throw std::runtime_error("thread count is too large");
        }
        threads = static_cast<int32_t>(count);
    }
    return threads;
}

imagecpp::Model load_clip_model(const imagecpp::Runtime &runtime, const std::string &model_path, int32_t threads,
                                const char *operation) {
    imagecpp_model_options options{};
    imagecpp_model_options_init(&options);
    options.model_path = model_path.c_str();
    options.threads = threads;
    options.device = IMAGECPP_DEVICE_CPU;
    return imagecpp::Model(runtime, operation, options);
}

void print_embedding(const imagecpp::EmbeddingResult &embedding) {
    std::cout << "{\"dimension\":" << embedding.size() << ",\"embedding\":[";
    std::cout << std::setprecision(9);
    for (size_t index = 0; index < embedding.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << embedding.data()[index];
    }
    std::cout << "]}\n";
}

int embed_image_command(int argc, char **argv) {
    if (argc < 4) {
        throw std::runtime_error("embed-image requires a model and input image");
    }
    const int32_t threads = parse_optional_threads(argc, argv, 4);
    imagecpp::Runtime runtime;
    imagecpp::Model model = load_clip_model(runtime, argv[2], threads, "image.embed.clip");
    imagecpp::Image image = imagecpp::load(argv[3]);
    print_embedding(imagecpp::embed_image(model, image));
    return 0;
}

int embed_text_command(int argc, char **argv) {
    if (argc < 4) {
        throw std::runtime_error("embed-text requires a model and text");
    }
    const int32_t threads = parse_optional_threads(argc, argv, 4);
    imagecpp::Runtime runtime;
    imagecpp::Model model = load_clip_model(runtime, argv[2], threads, "image.embed.clip");
    print_embedding(imagecpp::embed_text(model, argv[3]));
    return 0;
}

int classify_command(int argc, char **argv) {
    if (argc < 5) {
        throw std::runtime_error("classify requires a model, input image, and at least one label");
    }
    std::vector<std::string> labels;
    int32_t threads = 0;
    for (int index = 4; index < argc; ++index) {
        if (std::string(argv[index]) == "--threads") {
            if (++index >= argc || index + 1 != argc) {
                throw std::runtime_error("--threads must be the final option and requires a count");
            }
            const uint32_t count = parse_size_part(argv[index], "thread count");
            if (count > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("thread count is too large");
            }
            threads = static_cast<int32_t>(count);
        } else {
            labels.emplace_back(argv[index]);
        }
    }
    if (labels.empty()) {
        throw std::runtime_error("classify requires at least one label");
    }

    imagecpp::Runtime runtime;
    imagecpp::Model model = load_clip_model(runtime, argv[2], threads, "image.classify.clip");
    imagecpp::Image image = imagecpp::load(argv[3]);
    imagecpp::ClassificationResult result = imagecpp::classify(model, image, labels);
    std::cout << std::fixed << std::setprecision(6);
    for (size_t index = 0; index < result.size(); ++index) {
        const imagecpp::ClassificationInfo item = result.at(index);
        std::cout << item.label << '\t' << item.score << '\n';
    }
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
        if (argc >= 2 && std::string(argv[1]) == "cutout") {
            return cutout_image(argc, argv);
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
        if (argc >= 2 && std::string(argv[1]) == "depth") {
            return depth_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "embed-image") {
            return embed_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "embed-text") {
            return embed_text_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "classify") {
            return classify_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "detect") {
            return detect_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "ground") {
            return ground_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "extract") {
            return extract_image_command(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "ocr") {
            return ocr_image_command(argc, argv);
        }
        print_usage(argc == 1 ? std::cout : std::cerr);
        return argc == 1 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << "imagecpp: " << error.what() << '\n';
        return 1;
    }
}
