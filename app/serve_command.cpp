#include "serve_command.hpp"

#include "server/http_server.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

uint64_t positive_integer(const std::string &value, const char *name) {
    size_t consumed = 0;
    uint64_t parsed = 0;
    try {
        parsed = std::stoull(value, &consumed);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    if (parsed == 0 || consumed != value.size()) {
        throw std::runtime_error(std::string("invalid ") + name);
    }
    return parsed;
}

const char *next_value(int argc, char **argv, int &index, const std::string &option) {
    if (++index >= argc) {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[index];
}

std::string display_host(const std::string &host) {
    return host.find(':') == std::string::npos ? host : "[" + host + "]";
}

} // namespace

int serve_command(int argc, char **argv) {
    imagecpp::server::HttpServerConfig config;
    for (int index = 2; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            print_serve_command_usage(std::cout);
            return 0;
        }
        if (option == "--host") {
            config.host = next_value(argc, argv, index, option);
        } else if (option == "--port") {
            const uint64_t port = positive_integer(next_value(argc, argv, index, option), "server port");
            if (port > 65535) {
                throw std::runtime_error("server port is too large");
            }
            config.port = static_cast<int>(port);
        } else if (option == "--max-upload-mb") {
            const uint64_t megabytes = positive_integer(next_value(argc, argv, index, option), "upload limit");
            constexpr size_t bytes_per_megabyte = 1024U * 1024U;
            if (megabytes > std::numeric_limits<size_t>::max() / bytes_per_megabyte) {
                throw std::runtime_error("upload limit is too large");
            }
            config.max_upload_bytes = static_cast<size_t>(megabytes) * bytes_per_megabyte;
        } else if (option == "--max-output-mp") {
            const uint64_t megapixels = positive_integer(next_value(argc, argv, index, option), "output pixel limit");
            constexpr uint64_t pixels_per_megapixel = 1000U * 1000U;
            if (megapixels > std::numeric_limits<uint64_t>::max() / pixels_per_megapixel) {
                throw std::runtime_error("output pixel limit is too large");
            }
            config.max_output_pixels = megapixels * pixels_per_megapixel;
        } else if (option == "--vlm-model") {
            config.vlm_model_path = next_value(argc, argv, index, option);
        } else if (option == "--vlm-projection") {
            config.vlm_projection_model_path = next_value(argc, argv, index, option);
        } else if (option == "--segment-model") {
            config.segment_model_path = next_value(argc, argv, index, option);
        } else if (option == "--detect-model") {
            config.detect_model_path = next_value(argc, argv, index, option);
        } else if (option == "--depth-model") {
            config.depth_model_path = next_value(argc, argv, index, option);
        } else if (option == "--clip-model") {
            config.clip_model_path = next_value(argc, argv, index, option);
        } else if (option == "--ocr-model") {
            config.ocr_model_path = next_value(argc, argv, index, option);
        } else if (option == "--diffusion-checkpoint") {
            config.diffusion_checkpoint_path = next_value(argc, argv, index, option);
        } else if (option == "--diffusion-model") {
            config.diffusion_model_path = next_value(argc, argv, index, option);
        } else if (option == "--vae") {
            config.vae_model_path = next_value(argc, argv, index, option);
        } else if (option == "--clip-l") {
            config.clip_l_model_path = next_value(argc, argv, index, option);
        } else if (option == "--clip-g") {
            config.clip_g_model_path = next_value(argc, argv, index, option);
        } else if (option == "--t5xxl") {
            config.t5xxl_model_path = next_value(argc, argv, index, option);
        } else if (option == "--llm") {
            config.llm_model_path = next_value(argc, argv, index, option);
        } else if (option == "--upscaler-model") {
            config.upscaler_model_path = next_value(argc, argv, index, option);
        } else if (option == "--upscaler-tile") {
            const uint64_t tile = positive_integer(next_value(argc, argv, index, option), "upscaler tile size");
            if (tile > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("upscaler tile size is too large");
            }
            config.upscaler_tile_size = static_cast<int32_t>(tile);
        } else if (option == "--threads") {
            const uint64_t threads = positive_integer(next_value(argc, argv, index, option), "thread count");
            if (threads > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("thread count is too large");
            }
            config.threads = static_cast<int32_t>(threads);
        } else if (option == "--context") {
            const uint64_t context = positive_integer(next_value(argc, argv, index, option), "context size");
            if (context > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("context size is too large");
            }
            config.context_size = static_cast<uint32_t>(context);
        } else if (option == "--cpu") {
            config.device = IMAGECPP_DEVICE_CPU;
        } else if (option == "--gpu") {
            config.device = IMAGECPP_DEVICE_GPU;
        } else {
            throw std::runtime_error("unknown server option: " + option);
        }
    }

    imagecpp::server::HttpServer server(config);
    const int port = server.bind();
    std::cout << "imagecpp: listening on http://" << display_host(config.host) << ':' << port << '\n';
    if (config.vlm_model_path.empty()) {
        std::cout << "imagecpp: VLM endpoints are disabled; pass --vlm-model and --vlm-projection to enable them\n";
    }
    std::cout.flush();
    return server.listen() ? 0 : 1;
}

void print_serve_command_usage(std::ostream &output) {
    output << "\nserver options:\n"
           << "  --host <address>       bind address (default: 127.0.0.1)\n"
           << "  --port <number>        HTTP port (default: 8080)\n"
           << "  --max-upload-mb <n>    maximum request size in MiB (default: 32)\n"
           << "  --max-output-mp <n>    maximum output size in megapixels (default: 67)\n"
           << "  --vlm-model <path>     language-model GGUF for caption and VQA\n"
           << "  --vlm-projection <path> matching vision-projection GGUF\n"
           << "  --segment-model <path> SAM 2, SAM 3, or EdgeTAM segmentation model\n"
           << "  --detect-model <path>  full SAM 3 detection and grounded-cutout model\n"
           << "  --depth-model <path>   Depth Anything model\n"
           << "  --clip-model <path>    CLIP embedding and classification model\n"
           << "  --ocr-model <path>     Tesseract traineddata model\n"
           << "  --diffusion-checkpoint <path> monolithic diffusion checkpoint\n"
           << "  --diffusion-model/--vae/--clip-l/--clip-g/--t5xxl/--llm <path> split diffusion components\n"
           << "  --upscaler-model <path> ESRGAN-family upscaler model\n"
           << "  --upscaler-tile <n>    upscaler tile size (default: 128)\n"
           << "  --context <count>      VLM context tokens (default: 4096)\n"
           << "  --threads <count>      CPU worker threads\n"
           << "  --cpu | --gpu          select the model compute device (default: auto)\n";
}
