#include "model_commands.hpp"

#include "imagecpp/imagecpp.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace {

uint32_t positive_integer(const std::string &value, const char *name) {
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

int32_t positive_int32(const std::string &value, const char *name) {
    const uint32_t parsed = positive_integer(value, name);
    if (parsed > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error(std::string(name) + " is too large");
    }
    return static_cast<int32_t>(parsed);
}

int64_t signed_integer(const std::string &value, const char *name) {
    size_t consumed = 0;
    try {
        const int64_t parsed = std::stoll(value, &consumed);
        if (consumed == value.size()) {
            return parsed;
        }
    } catch (const std::exception &) {
    }
    throw std::runtime_error(std::string("invalid ") + name);
}

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

std::pair<uint32_t, uint32_t> dimensions(const std::string &value) {
    const size_t separator = value.find_first_of("xX");
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        throw std::runtime_error("dimensions must use <width>x<height>");
    }
    return {positive_integer(value.substr(0, separator), "width"),
            positive_integer(value.substr(separator + 1), "height")};
}

imagecpp_sample_method sample_method(const std::string &value) {
    if (value == "auto") {
        return IMAGECPP_SAMPLE_METHOD_AUTO;
    }
    if (value == "euler") {
        return IMAGECPP_SAMPLE_METHOD_EULER;
    }
    if (value == "euler-a") {
        return IMAGECPP_SAMPLE_METHOD_EULER_A;
    }
    if (value == "dpm++2m") {
        return IMAGECPP_SAMPLE_METHOD_DPM_PLUS_PLUS_2M;
    }
    if (value == "lcm") {
        return IMAGECPP_SAMPLE_METHOD_LCM;
    }
    if (value == "ddim") {
        return IMAGECPP_SAMPLE_METHOD_DDIM;
    }
    throw std::runtime_error("sampler must be auto, euler, euler-a, dpm++2m, lcm, or ddim");
}

imagecpp_scheduler scheduler(const std::string &value) {
    if (value == "auto") {
        return IMAGECPP_SCHEDULER_AUTO;
    }
    if (value == "discrete") {
        return IMAGECPP_SCHEDULER_DISCRETE;
    }
    if (value == "karras") {
        return IMAGECPP_SCHEDULER_KARRAS;
    }
    if (value == "exponential") {
        return IMAGECPP_SCHEDULER_EXPONENTIAL;
    }
    if (value == "ays") {
        return IMAGECPP_SCHEDULER_AYS;
    }
    if (value == "sgm-uniform") {
        return IMAGECPP_SCHEDULER_SGM_UNIFORM;
    }
    if (value == "simple") {
        return IMAGECPP_SCHEDULER_SIMPLE;
    }
    throw std::runtime_error("scheduler must be auto, discrete, karras, exponential, ays, sgm-uniform, or simple");
}

struct GenerationArguments {
    std::string model_path;
    std::string input_path;
    std::string mask_path;
    std::string output_path;
    std::string prompt;
    std::string negative_prompt;
    std::string diffusion_model_path;
    std::string vae_path;
    std::string clip_l_path;
    std::string clip_g_path;
    std::string t5xxl_path;
    std::string llm_path;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t steps = 20;
    int32_t threads = 0;
    float guidance = 7.0F;
    float strength = 0.75F;
    int64_t seed = -1;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
    imagecpp_sample_method sampler = IMAGECPP_SAMPLE_METHOD_AUTO;
    imagecpp_scheduler schedule = IMAGECPP_SCHEDULER_AUTO;
    bool flash_attention = true;
    bool keep_text_encoder_on_cpu = false;
    bool keep_vae_on_cpu = false;
};

const char *next_value(int argc, char **argv, int &index, const std::string &option) {
    if (++index >= argc) {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[index];
}

GenerationArguments generation_arguments(int argc, char **argv, bool editing) {
    const int required = editing ? 6 : 5;
    if (argc < required) {
        throw std::runtime_error(editing ? "edit requires a model, input, output, and prompt"
                                         : "generate requires a model, output, and prompt");
    }

    GenerationArguments result;
    result.model_path = argv[2];
    int option_index = 0;
    if (editing) {
        result.input_path = argv[3];
        result.output_path = argv[4];
        result.prompt = argv[5];
        option_index = 6;
    } else {
        result.output_path = argv[3];
        result.prompt = argv[4];
        result.width = 512;
        result.height = 512;
        option_index = 5;
    }

    for (int index = option_index; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--negative") {
            result.negative_prompt = next_value(argc, argv, index, option);
        } else if (option == "--size") {
            std::tie(result.width, result.height) = dimensions(next_value(argc, argv, index, option));
        } else if (option == "--steps") {
            result.steps = positive_int32(next_value(argc, argv, index, option), "step count");
        } else if (option == "--guidance") {
            result.guidance = finite_float(next_value(argc, argv, index, option), "guidance");
        } else if (option == "--strength") {
            result.strength = finite_float(next_value(argc, argv, index, option), "strength");
        } else if (option == "--seed") {
            result.seed = signed_integer(next_value(argc, argv, index, option), "seed");
        } else if (option == "--sampler") {
            result.sampler = sample_method(next_value(argc, argv, index, option));
        } else if (option == "--scheduler") {
            result.schedule = scheduler(next_value(argc, argv, index, option));
        } else if (option == "--mask") {
            result.mask_path = next_value(argc, argv, index, option);
        } else if (option == "--threads") {
            result.threads = positive_int32(next_value(argc, argv, index, option), "thread count");
        } else if (option == "--diffusion-model") {
            result.diffusion_model_path = next_value(argc, argv, index, option);
        } else if (option == "--vae") {
            result.vae_path = next_value(argc, argv, index, option);
        } else if (option == "--clip-l") {
            result.clip_l_path = next_value(argc, argv, index, option);
        } else if (option == "--clip-g") {
            result.clip_g_path = next_value(argc, argv, index, option);
        } else if (option == "--t5xxl") {
            result.t5xxl_path = next_value(argc, argv, index, option);
        } else if (option == "--llm") {
            result.llm_path = next_value(argc, argv, index, option);
        } else if (option == "--cpu") {
            result.device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            result.device = IMAGECPP_DEVICE_GPU;
        } else if (option == "--no-flash-attention") {
            result.flash_attention = false;
        } else if (option == "--keep-text-encoder-on-cpu") {
            result.keep_text_encoder_on_cpu = true;
        } else if (option == "--keep-vae-on-cpu") {
            result.keep_vae_on_cpu = true;
        } else {
            throw std::runtime_error("unknown generation option: " + option);
        }
    }
    if (!editing && !result.mask_path.empty()) {
        throw std::runtime_error("--mask is only valid for edit");
    }
    if (result.strength < 0.0F || result.strength > 1.0F) {
        throw std::runtime_error("strength must be between zero and one");
    }
    return result;
}

int run_generation(const GenerationArguments &arguments, bool editing) {
    std::optional<imagecpp::Image> input;
    std::optional<imagecpp::Image> mask;
    uint32_t width = arguments.width;
    uint32_t height = arguments.height;
    if (editing) {
        input.emplace(imagecpp::load(arguments.input_path));
        const imagecpp_const_image_view input_view = static_cast<const imagecpp::Image &>(*input).view();
        if (width == 0 || height == 0) {
            width = input_view.width;
            height = input_view.height;
        }
        if (!arguments.mask_path.empty()) {
            mask.emplace(imagecpp::load(arguments.mask_path));
        }
    }

    imagecpp::Runtime runtime;
    imagecpp_diffusion_model_options model_options{};
    imagecpp_diffusion_model_options_init(&model_options);
    model_options.model_path = arguments.model_path == "-" ? nullptr : arguments.model_path.c_str();
    model_options.diffusion_model_path =
        arguments.diffusion_model_path.empty() ? nullptr : arguments.diffusion_model_path.c_str();
    model_options.vae_path = arguments.vae_path.empty() ? nullptr : arguments.vae_path.c_str();
    model_options.clip_l_path = arguments.clip_l_path.empty() ? nullptr : arguments.clip_l_path.c_str();
    model_options.clip_g_path = arguments.clip_g_path.empty() ? nullptr : arguments.clip_g_path.c_str();
    model_options.t5xxl_path = arguments.t5xxl_path.empty() ? nullptr : arguments.t5xxl_path.c_str();
    model_options.llm_path = arguments.llm_path.empty() ? nullptr : arguments.llm_path.c_str();
    model_options.threads = arguments.threads;
    model_options.device = arguments.device;
    model_options.flash_attention = arguments.flash_attention ? 1 : 0;
    model_options.keep_text_encoder_on_cpu = arguments.keep_text_encoder_on_cpu ? 1 : 0;
    model_options.keep_vae_on_cpu = arguments.keep_vae_on_cpu ? 1 : 0;
    imagecpp::Model model(runtime, model_options);

    imagecpp_generate_options options{};
    imagecpp_generate_options_init(&options);
    options.prompt = arguments.prompt.c_str();
    options.negative_prompt = arguments.negative_prompt.c_str();
    options.width = width;
    options.height = height;
    options.steps = arguments.steps;
    options.guidance = arguments.guidance;
    options.seed = arguments.seed;
    options.strength = arguments.strength;
    options.sample_method = arguments.sampler;
    options.scheduler = arguments.schedule;
    imagecpp_const_image_view input_view{};
    imagecpp_const_image_view mask_view{};
    if (input.has_value()) {
        input_view = static_cast<const imagecpp::Image &>(*input).view();
        options.init_image = &input_view;
    }
    if (mask.has_value()) {
        mask_view = static_cast<const imagecpp::Image &>(*mask).view();
        options.mask = &mask_view;
    }

    imagecpp::ImageResult result = imagecpp::generate(model, options);
    imagecpp::save(arguments.output_path, result.at(0));
    std::cout << "generated " << result.at(0).width << 'x' << result.at(0).height << " image\n";
    return 0;
}

} // namespace

void print_model_command_usage(std::ostream &output) {
    output << "\ngeneration options:\n"
           << "  --negative <prompt>    negative prompt\n"
           << "  --size <width>x<height>\n"
           << "  --steps <count>        sampling steps (default: 20)\n"
           << "  --guidance <value>     classifier-free guidance (default: 7)\n"
           << "  --seed <integer>       -1 selects a random seed\n"
           << "  --strength <0..1>      edit denoising strength (default: 0.75)\n"
           << "  --mask <image>         edit only; white pixels select replacement\n"
           << "  --sampler <name>       auto, euler, euler-a, dpm++2m, lcm, or ddim\n"
           << "  --scheduler <name>     auto, discrete, karras, exponential, ays, sgm-uniform, or simple\n"
           << "  --vae/--clip-l/--clip-g/--t5xxl/--llm <path> for split model families\n"
           << "  --cpu | --gpu          require a compute device\n"
           << "  --threads <count>      CPU worker threads\n"
           << "\nupscale options: --factor <count> --tile <pixels> --cpu --gpu --threads <count>\n";
}

int generate_image_command(int argc, char **argv) {
    return run_generation(generation_arguments(argc, argv, false), false);
}

int edit_image_command(int argc, char **argv) { return run_generation(generation_arguments(argc, argv, true), true); }

int upscale_image_command(int argc, char **argv) {
    if (argc < 5) {
        throw std::runtime_error("upscale requires a model, input image, and output image");
    }
    uint32_t factor = 4;
    int32_t tile_size = 0;
    int32_t threads = 0;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
    for (int index = 5; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--factor") {
            factor = positive_integer(next_value(argc, argv, index, option), "upscale factor");
        } else if (option == "--tile") {
            tile_size = positive_int32(next_value(argc, argv, index, option), "tile size");
        } else if (option == "--threads") {
            threads = positive_int32(next_value(argc, argv, index, option), "thread count");
        } else if (option == "--cpu") {
            device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            device = IMAGECPP_DEVICE_GPU;
        } else {
            throw std::runtime_error("unknown upscale option: " + option);
        }
    }

    imagecpp::Runtime runtime;
    imagecpp_upscaler_model_options model_options{};
    imagecpp_upscaler_model_options_init(&model_options);
    model_options.model_path = argv[2];
    model_options.threads = threads;
    model_options.device = device;
    model_options.tile_size = tile_size;
    imagecpp::Model model(runtime, model_options);
    imagecpp::Image input = imagecpp::load(argv[3]);
    imagecpp::ImageResult result = imagecpp::upscale(model, input, factor);
    imagecpp::save(argv[4], result.at(0));
    std::cout << "upscaled to " << result.at(0).width << 'x' << result.at(0).height << '\n';
    return 0;
}
