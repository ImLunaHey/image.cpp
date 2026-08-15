#include "vlm_command.hpp"

#include "imagecpp/imagecpp.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

uint32_t parse_uint32(const std::string &value, const char *name, bool allow_zero = false) {
    size_t consumed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (consumed != value.size() || (!allow_zero && parsed == 0) || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    return static_cast<uint32_t>(parsed);
}

float parse_float(const std::string &value, const char *name) {
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

void write_json_string(std::ostream &output, const std::string &value) {
    output << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
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
            if (byte < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(byte)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(byte);
            }
        }
    }
    output << '"';
}

struct Arguments {
    std::string model_path;
    std::string projection_path;
    std::string image_path;
    std::string prompt;
    int32_t threads = 0;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
    uint32_t context_size = 4096;
    uint32_t max_tokens = 128;
    float temperature = 0.1F;
    float top_p = 0.9F;
    int32_t top_k = 40;
    uint32_t seed = 0;
    bool json = false;
    bool stream = false;
};

Arguments parse_arguments(int argc, char **argv, bool ask) {
    const int required = ask ? 6 : 5;
    if (argc < required) {
        throw std::runtime_error(std::string(ask ? "ask" : "caption") +
                                 " requires a language model, projection model, and input image" +
                                 (ask ? ", followed by a question" : ""));
    }
    Arguments result;
    result.model_path = argv[2];
    result.projection_path = argv[3];
    result.image_path = argv[4];
    int index = 5;
    if (ask) {
        result.prompt = argv[index++];
        if (result.prompt.empty()) {
            throw std::runtime_error("question cannot be empty");
        }
    }
    for (; index < argc; ++index) {
        const std::string option = argv[index];
        if (!ask && option == "--prompt") {
            if (++index >= argc) {
                throw std::runtime_error("--prompt requires text");
            }
            result.prompt = argv[index];
        } else if (option == "--threads") {
            if (++index >= argc) {
                throw std::runtime_error("--threads requires a count");
            }
            const uint32_t value = parse_uint32(argv[index], "thread count");
            if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("thread count is too large");
            }
            result.threads = static_cast<int32_t>(value);
        } else if (option == "--context") {
            if (++index >= argc) {
                throw std::runtime_error("--context requires a token count");
            }
            result.context_size = parse_uint32(argv[index], "context size");
        } else if (option == "--max-tokens") {
            if (++index >= argc) {
                throw std::runtime_error("--max-tokens requires a count");
            }
            result.max_tokens = parse_uint32(argv[index], "maximum token count");
        } else if (option == "--temperature") {
            if (++index >= argc) {
                throw std::runtime_error("--temperature requires a value");
            }
            result.temperature = parse_float(argv[index], "temperature");
        } else if (option == "--top-p") {
            if (++index >= argc) {
                throw std::runtime_error("--top-p requires a value");
            }
            result.top_p = parse_float(argv[index], "top-p");
        } else if (option == "--top-k") {
            if (++index >= argc) {
                throw std::runtime_error("--top-k requires a count");
            }
            const uint32_t value = parse_uint32(argv[index], "top-k", true);
            if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("top-k is too large");
            }
            result.top_k = static_cast<int32_t>(value);
        } else if (option == "--seed") {
            if (++index >= argc) {
                throw std::runtime_error("--seed requires a value");
            }
            result.seed = parse_uint32(argv[index], "seed", true);
        } else if (option == "--cpu") {
            result.device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            result.device = IMAGECPP_DEVICE_GPU;
        } else if (option == "--json") {
            result.json = true;
        } else if (option == "--stream") {
            result.stream = true;
        } else {
            throw std::runtime_error("unknown visual query option: " + option);
        }
    }
    if (result.json && result.stream) {
        throw std::runtime_error("--json and --stream cannot be used together");
    }
    return result;
}

const char *finish_reason_name(imagecpp_text_finish_reason reason) {
    switch (reason) {
    case IMAGECPP_TEXT_FINISH_END_OF_GENERATION:
        return "end_of_generation";
    case IMAGECPP_TEXT_FINISH_LENGTH:
        return "length";
    case IMAGECPP_TEXT_FINISH_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

int run(const Arguments &arguments) {
    imagecpp::Runtime runtime;
    imagecpp_vlm_model_options model_options{};
    imagecpp_vlm_model_options_init(&model_options);
    model_options.model_path = arguments.model_path.c_str();
    model_options.projection_model_path = arguments.projection_path.c_str();
    model_options.threads = arguments.threads;
    model_options.device = arguments.device;
    model_options.context_size = arguments.context_size;
    imagecpp::Model model(runtime, model_options);

    imagecpp::Image image = imagecpp::load(arguments.image_path);
    imagecpp_visual_query_options query_options{};
    imagecpp_visual_query_options_init(&query_options);
    query_options.prompt = arguments.prompt.empty() ? nullptr : arguments.prompt.c_str();
    query_options.max_tokens = arguments.max_tokens;
    query_options.temperature = arguments.temperature;
    query_options.top_p = arguments.top_p;
    query_options.top_k = arguments.top_k;
    query_options.seed = arguments.seed;
    imagecpp::TextResult generated = arguments.stream
                                         ? imagecpp::visual_query_stream(
                                               model, image, query_options, [](std::string_view chunk) {
                                                   std::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                                                   std::cout.flush();
                                                   return true;
                                               })
                                         : imagecpp::visual_query(model, image, query_options);
    const imagecpp::TextInfo result = generated.info();
    if (arguments.stream) {
        std::cout << '\n';
        return 0;
    }
    if (!arguments.json) {
        std::cout << result.text << '\n';
        return 0;
    }
    std::cout << "{\"text\":";
    write_json_string(std::cout, result.text);
    std::cout << ",\"prompt_tokens\":" << result.prompt_tokens << ",\"generated_tokens\":" << result.generated_tokens
              << ",\"finish_reason\":\"" << finish_reason_name(result.finish_reason) << "\"}\n";
    return 0;
}

} // namespace

int caption_image_command(int argc, char **argv) { return run(parse_arguments(argc, argv, false)); }

int ask_image_command(int argc, char **argv) { return run(parse_arguments(argc, argv, true)); }

void print_vlm_command_usage(std::ostream &output) {
    output << "\nvisual query options:\n"
           << "  --prompt <text>        override the default caption prompt (caption only)\n"
           << "  --max-tokens <count>  maximum generated tokens (default: 128)\n"
           << "  --temperature <value> sampling temperature; zero is greedy (default: 0.1)\n"
           << "  --top-p <value>       nucleus probability (default: 0.9)\n"
           << "  --top-k <count>       candidate token count (default: 40)\n"
           << "  --seed <value>        sampling seed (default: 0)\n"
           << "  --context <count>     model context tokens (default: 4096)\n"
           << "  --threads <count>     CPU worker threads\n"
           << "  --cpu | --gpu         select a compute device (default: auto)\n"
           << "  --json                emit text and token metadata as JSON\n"
           << "  --stream              print generated text fragments immediately\n";
}
