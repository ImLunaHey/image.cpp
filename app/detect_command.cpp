#include "detect_command.hpp"

#include "imagecpp/imagecpp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

float finite_float(const std::string &value, const char *name) {
    size_t consumed = 0;
    float parsed = 0.0F;
    try {
        parsed = std::stof(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (consumed != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    return parsed;
}

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

imagecpp_box parse_box(const std::string &value) {
    std::array<float, 4> coordinates{};
    size_t begin = 0;
    for (size_t index = 0; index < coordinates.size(); ++index) {
        const size_t end = value.find(',', begin);
        if ((index + 1 < coordinates.size() && end == std::string::npos) ||
            (index + 1 == coordinates.size() && end != std::string::npos)) {
            throw std::runtime_error("exemplar boxes must use x0,y0,x1,y1");
        }
        coordinates[index] =
            finite_float(value.substr(begin, end == std::string::npos ? end : end - begin), "exemplar coordinate");
        begin = end == std::string::npos ? value.size() : end + 1;
    }
    if (coordinates[2] <= coordinates[0] || coordinates[3] <= coordinates[1]) {
        throw std::runtime_error("exemplar boxes must have positive area");
    }
    return {coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
}

const char *next_value(int argc, char **argv, int &index, const std::string &option) {
    if (++index >= argc) {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[index];
}

struct DetectArguments {
    std::string model_path;
    std::string input_path;
    std::string output_path;
    std::string prompt;
    std::vector<imagecpp_box> positive_exemplars;
    std::vector<imagecpp_box> negative_exemplars;
    float score_threshold = 0.5F;
    float nms_threshold = 0.1F;
    int32_t threads = 0;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
};

DetectArguments parse_arguments(int argc, char **argv, bool write_mask) {
    const int required = write_mask ? 6 : 5;
    if (argc < required) {
        throw std::runtime_error(write_mask ? "ground requires a model, input, output mask, and text prompt"
                                            : "detect requires a model, input, and text prompt");
    }
    DetectArguments result;
    result.model_path = argv[2];
    result.input_path = argv[3];
    int option_index = 5;
    if (write_mask) {
        result.output_path = argv[4];
        result.prompt = argv[5];
        option_index = 6;
    } else {
        result.prompt = argv[4];
    }
    if (result.prompt.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
        throw std::runtime_error("detection prompt is empty");
    }

    for (int index = option_index; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--threshold") {
            result.score_threshold = finite_float(next_value(argc, argv, index, option), "score threshold");
        } else if (option == "--nms") {
            result.nms_threshold = finite_float(next_value(argc, argv, index, option), "NMS threshold");
        } else if (option == "--positive-box") {
            result.positive_exemplars.push_back(parse_box(next_value(argc, argv, index, option)));
        } else if (option == "--negative-box") {
            result.negative_exemplars.push_back(parse_box(next_value(argc, argv, index, option)));
        } else if (option == "--threads") {
            result.threads = positive_int32(next_value(argc, argv, index, option), "thread count");
        } else if (option == "--cpu") {
            result.device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            result.device = IMAGECPP_DEVICE_GPU;
        } else {
            throw std::runtime_error("unknown detection option: " + option);
        }
    }
    if (result.score_threshold < 0.0F || result.score_threshold > 1.0F || result.nms_threshold < 0.0F ||
        result.nms_threshold > 1.0F) {
        throw std::runtime_error("detection thresholds must be between zero and one");
    }
    return result;
}

struct DetectionRun {
    imagecpp::Image source;
    imagecpp::DetectionResult result;
};

DetectionRun run_detection(const DetectArguments &arguments) {
    imagecpp::Runtime runtime;
    imagecpp_model_options model_options{};
    imagecpp_model_options_init(&model_options);
    model_options.model_path = arguments.model_path.c_str();
    model_options.threads = arguments.threads;
    model_options.device = arguments.device;
    imagecpp::Model model(runtime, "image.detect.sam3", model_options);
    imagecpp::Session session(model);
    imagecpp::Image source = imagecpp::load(arguments.input_path);
    session.set_image(source);

    imagecpp_detect_options options{};
    imagecpp_detect_options_init(&options);
    options.prompt = arguments.prompt.c_str();
    options.positive_exemplars = arguments.positive_exemplars.data();
    options.positive_exemplar_count = arguments.positive_exemplars.size();
    options.negative_exemplars = arguments.negative_exemplars.data();
    options.negative_exemplar_count = arguments.negative_exemplars.size();
    options.score_threshold = arguments.score_threshold;
    options.nms_threshold = arguments.nms_threshold;
    return {std::move(source), session.detect(options)};
}

void print_json_string(const std::string &value) {
    std::cout << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            std::cout << "\\\"";
            break;
        case '\\':
            std::cout << "\\\\";
            break;
        case '\b':
            std::cout << "\\b";
            break;
        case '\f':
            std::cout << "\\f";
            break;
        case '\n':
            std::cout << "\\n";
            break;
        case '\r':
            std::cout << "\\r";
            break;
        case '\t':
            std::cout << "\\t";
            break;
        default:
            if (character < 0x20) {
                std::cout << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(character)
                          << std::dec << std::setfill(' ');
            } else {
                std::cout << character;
            }
        }
    }
    std::cout << '"';
}

} // namespace

void print_detect_command_usage(std::ostream &output) {
    output << "\ndetection options:\n"
           << "  --threshold <0..1>     minimum concept score (default: 0.5)\n"
           << "  --nms <0..1>           box NMS IoU threshold (default: 0.1)\n"
           << "  --positive-box <x0>,<y0>,<x1>,<y1>  positive exemplar (repeatable)\n"
           << "  --negative-box <x0>,<y0>,<x1>,<y1>  negative exemplar (repeatable)\n";
}

int detect_image_command(int argc, char **argv) {
    const DetectArguments arguments = parse_arguments(argc, argv, false);
    DetectionRun run = run_detection(arguments);
    std::cout << std::fixed << std::setprecision(6) << "{\"prompt\":";
    print_json_string(arguments.prompt);
    std::cout << ",\"count\":" << run.result.size() << ",\"detections\":[";
    for (size_t index = 0; index < run.result.size(); ++index) {
        const imagecpp::DetectionInfo info = run.result.at(index);
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << "{\"index\":" << index << ",\"score\":" << info.score << ",\"iou_score\":" << info.iou_score
                  << ",\"box\":[" << info.box.x0 << ',' << info.box.y0 << ',' << info.box.x1 << ',' << info.box.y1
                  << "],\"mask_size\":[" << info.mask.width << ',' << info.mask.height << "]}";
    }
    std::cout << "]}\n";
    return 0;
}

int ground_image_command(int argc, char **argv) {
    const DetectArguments arguments = parse_arguments(argc, argv, true);
    DetectionRun run = run_detection(arguments);
    const imagecpp_const_image_view source = static_cast<const imagecpp::Image &>(run.source).view();
    imagecpp_image_desc mask_desc{
        sizeof(imagecpp_image_desc), source.width, source.height, 0, IMAGECPP_PIXEL_FORMAT_GRAY_U8,
        IMAGECPP_COLOR_SPACE_UNKNOWN};
    imagecpp::Image combined(mask_desc);
    imagecpp_image_view combined_view = combined.view();
    auto *combined_bytes = static_cast<uint8_t *>(combined_view.data);
    for (size_t index = 0; index < run.result.size(); ++index) {
        const imagecpp::DetectionInfo detection = run.result.at(index);
        if (detection.mask.width != source.width || detection.mask.height != source.height ||
            detection.mask.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_U8) {
            throw std::runtime_error("detection mask does not match the source image");
        }
        const auto *mask_bytes = static_cast<const uint8_t *>(detection.mask.data);
        for (uint32_t row = 0; row < source.height; ++row) {
            uint8_t *output_row = combined_bytes + static_cast<size_t>(row) * combined_view.row_stride;
            const uint8_t *mask_row = mask_bytes + static_cast<size_t>(row) * detection.mask.row_stride;
            for (uint32_t column = 0; column < source.width; ++column) {
                output_row[column] = std::max(output_row[column], mask_row[column]);
            }
        }
    }
    imagecpp::save(arguments.output_path, combined);
    std::cout << "detections: " << run.result.size() << '\n';
    return 0;
}
