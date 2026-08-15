#include "server/http_server.hpp"

#include "httplib.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

namespace {

std::string read_file(const char *path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void print_result(const char *name, const httplib::Result &result) {
    std::cerr << name << ": ";
    if (!result) {
        std::cerr << "request failed\n";
        return;
    }
    std::cerr << result->status << " " << result->body << '\n';
}

bool run_requests(imagecpp::server::HttpServerConfig config, const std::string &image_bytes) {
    config.port = 0;
    imagecpp::server::HttpServer server(std::move(config));
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    const httplib::Result health = client.Get("/healthz");
    const httplib::Result operations = client.Get("/v1/operations");
    const httplib::Result missing = client.Get("/does-not-exist");
    const std::string resize_source = "P6\n2 2\n255\n\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
    const httplib::Result resized =
        client.Post("/v1/resize?width=3&height=4&filter=nearest", resize_source, "image/x-portable-pixmap");
    const httplib::Result oversized = client.Post("/v1/resize?width=100000&height=100000", resize_source,
                                                   "image/x-portable-pixmap");
    bool passed = health && health->status == 200 && health->body.find("\"status\":\"ok\"") != std::string::npos &&
                  operations && operations->status == 200 &&
                  operations->body.find("\"operations\"") != std::string::npos && missing && missing->status == 404 &&
                  missing->body.find("not_found") != std::string::npos && resized && resized->status == 200 &&
                  resized->get_header_value("Content-Type") == "image/png" && resized->body.size() > 8 &&
                  resized->body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 && oversized && oversized->status == 400 &&
                  oversized->body.find("output pixel limit") != std::string::npos;

    if (image_bytes.empty()) {
        const httplib::Result unavailable = client.Post("/v1/caption", "image", "application/octet-stream");
        passed = passed && health->body.find("\"vlm_loaded\":false") != std::string::npos && unavailable &&
                 unavailable->status == 503 && unavailable->body.find("model_not_loaded") != std::string::npos;
    } else {
        const httplib::Result caption = client.Post("/v1/caption?temperature=0&max_tokens=16", image_bytes, "image/png");
        const httplib::UploadFormDataItems question = {
            {"image", image_bytes, "cat.png", "image/png"},
            {"question", "What animal is in the image? Answer with one word.", "", ""},
            {"temperature", "0", "", ""},
            {"max_tokens", "8", "", ""},
        };
        const httplib::Result answer = client.Post("/v1/ask", question);
        const httplib::Headers stream_headers = {{"Accept", "text/event-stream"}};
        const httplib::Result stream = client.Post("/v1/ask", stream_headers, question);
        const httplib::Result invalid = client.Post("/v1/caption", "not an image", "application/octet-stream");
        passed = passed && health->body.find("\"vlm_loaded\":true") != std::string::npos && caption &&
                 caption->status == 200 && caption->body.find("\"text\"") != std::string::npos && answer &&
                 answer->status == 200 &&
                 (answer->body.find("cat") != std::string::npos || answer->body.find("Cat") != std::string::npos) &&
                 stream && stream->status == 200 && stream->body.find("event: delta") != std::string::npos &&
                 stream->body.find("event: done") != std::string::npos &&
                 (stream->body.find("cat") != std::string::npos || stream->body.find("Cat") != std::string::npos) &&
                 invalid && (invalid->status == 400 || invalid->status == 415);
        if (!passed) {
            print_result("caption", caption);
            print_result("answer", answer);
            print_result("stream", stream);
            print_result("invalid", invalid);
        }
    }

    server.stop();
    listener.join();
    if (!passed) {
        print_result("health", health);
        print_result("operations", operations);
        print_result("missing", missing);
        print_result("resized", resized);
        print_result("oversized", oversized);
    }
    return passed;
}

} // namespace

int main(int argc, char **argv) {
    try {
        imagecpp::server::HttpServerConfig config;
        if (argc == 1) {
            return run_requests(std::move(config), {}) ? 0 : 1;
        }
        if (argc != 4) {
            return 1;
        }
        config.vlm_model_path = argv[1];
        config.vlm_projection_model_path = argv[2];
        config.device = IMAGECPP_DEVICE_CPU;
        const std::string image = read_file(argv[3]);
        return !image.empty() && run_requests(std::move(config), image) ? 0 : 1;
    } catch (...) {
        return 1;
    }
}
