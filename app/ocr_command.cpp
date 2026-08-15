#include "ocr_command.hpp"

#include "imagecpp/imagecpp.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

struct OcrArguments {
    std::string model_path;
    std::string input_path;
    imagecpp_ocr_page_segmentation page_segmentation = IMAGECPP_OCR_PAGE_AUTO;
    uint32_t source_dpi = 300;
    bool preserve_interword_spaces = false;
    bool json = false;
};

const char *next_value(int argc, char **argv, int &index, const std::string &option) {
    if (++index >= argc) {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[index];
}

uint32_t parse_dpi(const std::string &value) {
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error("invalid OCR source DPI");
    }
    if (consumed != value.size() || parsed == 0 || parsed > 2400) {
        throw std::runtime_error("OCR source DPI must be between 1 and 2400");
    }
    return static_cast<uint32_t>(parsed);
}

imagecpp_ocr_page_segmentation parse_page_segmentation(const std::string &value) {
    if (value == "auto") {
        return IMAGECPP_OCR_PAGE_AUTO;
    }
    if (value == "column") {
        return IMAGECPP_OCR_PAGE_SINGLE_COLUMN;
    }
    if (value == "block") {
        return IMAGECPP_OCR_PAGE_SINGLE_BLOCK;
    }
    if (value == "line") {
        return IMAGECPP_OCR_PAGE_SINGLE_LINE;
    }
    if (value == "word") {
        return IMAGECPP_OCR_PAGE_SINGLE_WORD;
    }
    if (value == "sparse") {
        return IMAGECPP_OCR_PAGE_SPARSE_TEXT;
    }
    if (value == "raw-line") {
        return IMAGECPP_OCR_PAGE_RAW_LINE;
    }
    throw std::runtime_error("OCR page mode must be auto, column, block, line, word, sparse, or raw-line");
}

OcrArguments parse_arguments(int argc, char **argv) {
    if (argc < 4) {
        throw std::runtime_error("ocr requires a traineddata model and input image");
    }
    OcrArguments result;
    result.model_path = argv[2];
    result.input_path = argv[3];
    for (int index = 4; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--json") {
            result.json = true;
        } else if (option == "--psm") {
            result.page_segmentation = parse_page_segmentation(next_value(argc, argv, index, option));
        } else if (option == "--dpi") {
            result.source_dpi = parse_dpi(next_value(argc, argv, index, option));
        } else if (option == "--preserve-spaces") {
            result.preserve_interword_spaces = true;
        } else if (option == "--cpu") {
            // Tesseract is CPU-only; this explicit selector is accepted for CLI consistency.
        } else {
            throw std::runtime_error("unknown OCR option: " + option);
        }
    }
    return result;
}

void json_string(std::ostream &output, const std::string &value) {
    static constexpr char HEX[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << HEX[character >> 4U] << HEX[character & 0x0fU];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
}

const char *region_level(imagecpp_text_region_level level) {
    switch (level) {
    case IMAGECPP_TEXT_REGION_BLOCK:
        return "block";
    case IMAGECPP_TEXT_REGION_PARAGRAPH:
        return "paragraph";
    case IMAGECPP_TEXT_REGION_LINE:
        return "line";
    case IMAGECPP_TEXT_REGION_WORD:
        return "word";
    }
    return "unknown";
}

const char *block_type(imagecpp_text_block_type type) {
    static constexpr const char *NAMES[] = {
        "unknown",         "flowing_text",  "heading_text",    "pullout_text",  "equation",
        "inline_equation", "table",         "vertical_text",   "caption_text",  "flowing_image",
        "heading_image",   "pullout_image", "horizontal_line", "vertical_line", "noise",
    };
    const auto index = static_cast<size_t>(type);
    return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "unknown";
}

const char *orientation(imagecpp_text_orientation value) {
    switch (value) {
    case IMAGECPP_TEXT_ORIENTATION_PAGE_UP:
        return "page_up";
    case IMAGECPP_TEXT_ORIENTATION_PAGE_RIGHT:
        return "page_right";
    case IMAGECPP_TEXT_ORIENTATION_PAGE_DOWN:
        return "page_down";
    case IMAGECPP_TEXT_ORIENTATION_PAGE_LEFT:
        return "page_left";
    case IMAGECPP_TEXT_ORIENTATION_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

const char *writing_direction(imagecpp_writing_direction value) {
    switch (value) {
    case IMAGECPP_WRITING_DIRECTION_LEFT_TO_RIGHT:
        return "left_to_right";
    case IMAGECPP_WRITING_DIRECTION_RIGHT_TO_LEFT:
        return "right_to_left";
    case IMAGECPP_WRITING_DIRECTION_TOP_TO_BOTTOM:
        return "top_to_bottom";
    case IMAGECPP_WRITING_DIRECTION_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

const char *textline_order(imagecpp_textline_order value) {
    switch (value) {
    case IMAGECPP_TEXTLINE_ORDER_LEFT_TO_RIGHT:
        return "left_to_right";
    case IMAGECPP_TEXTLINE_ORDER_RIGHT_TO_LEFT:
        return "right_to_left";
    case IMAGECPP_TEXTLINE_ORDER_TOP_TO_BOTTOM:
        return "top_to_bottom";
    case IMAGECPP_TEXTLINE_ORDER_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

void json_index(std::ostream &output, size_t value) {
    if (value == IMAGECPP_NO_INDEX) {
        output << "null";
    } else {
        output << value;
    }
}

void print_json(const imagecpp::OcrResult &result) {
    const imagecpp::OcrInfo info = result.info();
    std::cout << std::setprecision(7) << "{\"text\":";
    json_string(std::cout, info.text);
    std::cout << ",\"language\":";
    json_string(std::cout, info.language);
    std::cout << ",\"mean_confidence\":" << info.mean_confidence << ",\"regions\":[";
    for (size_t index = 0; index < result.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        const imagecpp::TextRegionInfo region = result.at(index);
        std::cout << "{\"level\":\"" << region_level(region.level) << "\",\"text\":";
        json_string(std::cout, region.text);
        std::cout << ",\"box\":[" << region.box.x0 << ',' << region.box.y0 << ',' << region.box.x1 << ','
                  << region.box.y1 << "],\"confidence\":" << region.confidence << ",\"block_index\":";
        json_index(std::cout, region.block_index);
        std::cout << ",\"paragraph_index\":";
        json_index(std::cout, region.paragraph_index);
        std::cout << ",\"line_index\":";
        json_index(std::cout, region.line_index);
        std::cout << ",\"word_index\":";
        json_index(std::cout, region.word_index);
        std::cout << ",\"block_type\":\"" << block_type(region.block_type) << "\",\"baseline\":";
        if (region.has_baseline) {
            std::cout << '[' << region.baseline.x0 << ',' << region.baseline.y0 << ',' << region.baseline.x1 << ','
                      << region.baseline.y1 << ']';
        } else {
            std::cout << "null";
        }
        std::cout << ",\"orientation\":\"" << orientation(region.orientation) << "\",\"writing_direction\":\""
                  << writing_direction(region.writing_direction) << "\",\"textline_order\":\""
                  << textline_order(region.textline_order)
                  << "\",\"deskew_angle_degrees\":" << region.deskew_angle_degrees << '}';
    }
    std::cout << "]}\n";
}

} // namespace

void print_ocr_command_usage(std::ostream &output) {
    output << "\nOCR options:\n"
           << "  --json                 emit structured document JSON\n"
           << "  --psm <mode>           auto, column, block, line, word, sparse, or raw-line\n"
           << "  --dpi <count>          source resolution from 1 to 2400 (default: 300)\n"
           << "  --preserve-spaces      preserve interword spacing where possible\n"
           << "  --cpu                  explicitly select CPU execution\n";
}

int ocr_image_command(int argc, char **argv) {
    const OcrArguments arguments = parse_arguments(argc, argv);
    imagecpp::Runtime runtime;
    imagecpp_model_options model_options{};
    imagecpp_model_options_init(&model_options);
    model_options.model_path = arguments.model_path.c_str();
    model_options.device = IMAGECPP_DEVICE_CPU;
    imagecpp::Model model(runtime, "image.ocr.tesseract", model_options);
    imagecpp::Image image = imagecpp::load(arguments.input_path);
    imagecpp_ocr_options options{};
    imagecpp_ocr_options_init(&options);
    options.page_segmentation = arguments.page_segmentation;
    options.source_dpi = arguments.source_dpi;
    options.preserve_interword_spaces = arguments.preserve_interword_spaces ? 1 : 0;
    imagecpp::OcrResult result = imagecpp::ocr(model, image, options);
    if (arguments.json) {
        print_json(result);
    } else {
        const std::string &text = result.info().text;
        std::cout << text;
        if (text.empty() || text.back() != '\n') {
            std::cout << '\n';
        }
    }
    return 0;
}
