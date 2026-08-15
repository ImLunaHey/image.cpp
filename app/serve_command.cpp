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
        } else if (option == "--vlm-model") {
            config.vlm_model_path = next_value(argc, argv, index, option);
        } else if (option == "--vlm-projection") {
            config.vlm_projection_model_path = next_value(argc, argv, index, option);
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
           << "  --vlm-model <path>     language-model GGUF for caption and VQA\n"
           << "  --vlm-projection <path> matching vision-projection GGUF\n"
           << "  --context <count>      VLM context tokens (default: 4096)\n"
           << "  --threads <count>      CPU worker threads\n"
           << "  --cpu | --gpu          select the VLM compute device (default: auto)\n";
}
