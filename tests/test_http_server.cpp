#include "server/http_server.hpp"

#include "httplib.h"

#include <chrono>
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
    constexpr size_t max_body = 1000;
    std::cerr << result->status << " " << result->body.substr(0, max_body);
    if (result->body.size() > max_body) {
        std::cerr << "... [" << result->body.size() << " bytes]";
    }
    std::cerr << '\n';
}

bool wait_for_job(httplib::Client &client, const std::string &location, httplib::Result &result,
                  std::string &status_body) {
    if (location.empty()) {
        return false;
    }
    for (size_t attempt = 0; attempt < 400; ++attempt) {
        const httplib::Result status = client.Get(location);
        if (!status) {
            return false;
        }
        status_body = status->body;
        if (status_body.find("\"status\":\"completed\"") != std::string::npos ||
            status_body.find("\"status\":\"failed\"") != std::string::npos ||
            status_body.find("\"status\":\"cancelled\"") != std::string::npos) {
            result = client.Get(location + "/result");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool run_requests(imagecpp::server::HttpServerConfig config, const std::string &image_bytes) {
    config.port = 0;
    imagecpp::server::HttpServer server(std::move(config));
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    const httplib::Result root = client.Get("/");
    const httplib::Headers browser_headers = {{"Accept", "text/html,application/xhtml+xml"}};
    const httplib::Result playground = client.Get("/", browser_headers);
    const httplib::Result playground_direct = client.Get("/playground");
    const httplib::Result playground_css = client.Get("/assets/playground.css");
    const httplib::Result playground_javascript = client.Get("/assets/playground.js");
    const httplib::Result service_info = client.Get("/v1/info");
    const httplib::Result health = client.Get("/healthz");
    const httplib::Result models = client.Get("/v1/models");
    const httplib::Result cleared_models = client.Delete("/v1/models/cache");
    const httplib::Result operations = client.Get("/v1/operations");
    const httplib::Result missing = client.Get("/does-not-exist");
    const std::string resize_source = "P6\n2 2\n255\n\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
    const httplib::Result resized =
        client.Post("/v1/resize?width=3&height=4&filter=nearest", resize_source, "image/x-portable-pixmap");
    const httplib::Result oversized =
        client.Post("/v1/resize?width=100000&height=100000", resize_source, "image/x-portable-pixmap");
    const httplib::Headers async_headers = {{"Prefer", "respond-async"}};
    const httplib::Result submitted =
        client.Post("/v1/resize?width=5&height=6", async_headers, resize_source, "image/x-portable-pixmap");
    const std::string job_location = submitted ? submitted->get_header_value("Location") : "";
    httplib::Result async_result;
    std::string async_status;
    const bool async_finished = wait_for_job(client, job_location, async_result, async_status);
    const httplib::Result jobs = client.Get("/v1/jobs");
    const httplib::Result missing_job = client.Get("/v1/jobs/job-does-not-exist");
    const httplib::Headers async_stream_headers = {{"Prefer", "respond-async"}, {"Accept", "text/event-stream"}};
    const httplib::Result async_stream =
        client.Post("/v1/caption", async_stream_headers, resize_source, "image/x-portable-pixmap");
    bool passed =
        root && root->status == 200 && root->get_header_value("Content-Type").find("application/json") == 0 &&
        root->body.find("\"/playground\"") != std::string::npos && playground && playground->status == 200 &&
        playground->get_header_value("Content-Type").find("text/html") == 0 &&
        playground->get_header_value("Content-Security-Policy").find("default-src 'self'") != std::string::npos &&
        playground->body.find("image.cpp studio") != std::string::npos && playground_direct &&
        playground_direct->status == 200 && playground_css && playground_css->status == 200 &&
        playground_css->get_header_value("Content-Type").find("text/css") == 0 &&
        playground_css->body.find("--acid: #c9ff43") != std::string::npos && playground_javascript &&
        playground_javascript->status == 200 &&
        playground_javascript->get_header_value("Content-Type").find("text/javascript") == 0 &&
        playground_javascript->body.find("/v1/remove-background") != std::string::npos &&
        playground_javascript->body.find("/v1/jobs?limit=50") != std::string::npos &&
        playground_javascript->body.find("imagecpp.presets.v1") != std::string::npos && service_info &&
        service_info->status == 200 && service_info->body.find("\"name\":\"image.cpp\"") != std::string::npos &&
        health && health->status == 200 && health->body.find("\"status\":\"ok\"") != std::string::npos &&
        health->body.find("\"worker_count\":1") != std::string::npos && operations && models && models->status == 200 &&
        models->body.find("\"capacity\":1") != std::string::npos &&
        models->body.find("\"loaded_families\":[]") != std::string::npos && cleared_models &&
        cleared_models->status == 200 && cleared_models->body.find("\"removed\":0") != std::string::npos &&
        operations->status == 200 && operations->body.find("\"operations\"") != std::string::npos && missing &&
        missing->status == 404 && missing->body.find("not_found") != std::string::npos && resized &&
        resized->status == 200 && resized->get_header_value("Content-Type") == "image/png" &&
        resized->body.size() > 8 && resized->body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 && oversized &&
        oversized->status == 400 && oversized->body.find("output pixel limit") != std::string::npos && submitted &&
        submitted->status == 202 && submitted->get_header_value("Preference-Applied") == "respond-async" &&
        async_finished && async_result && async_result->status == 200 &&
        async_result->get_header_value("Content-Type") == "image/png" &&
        async_result->body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 &&
        async_status.find("\"progress\":1.0") != std::string::npos && jobs && jobs->status == 200 &&
        jobs->body.find("\"operation\":\"resize\"") != std::string::npos && missing_job && missing_job->status == 404 &&
        async_stream && async_stream->status == 400 &&
        async_stream->body.find("async_stream_unsupported") != std::string::npos;

    if (image_bytes.empty()) {
        const httplib::Result unavailable = client.Post("/v1/caption", "image", "application/octet-stream");
        const httplib::Result unavailable_ocr = client.Post("/v1/ocr", resize_source, "image/x-portable-pixmap");
        const httplib::Result unavailable_depth = client.Post("/v1/depth", resize_source, "image/x-portable-pixmap");
        const httplib::Result unavailable_clip =
            client.Post("/v1/embed/image", resize_source, "image/x-portable-pixmap");
        const httplib::Result unavailable_generate =
            client.Post("/v1/generate", "{\"prompt\":\"test\"}", "application/json");
        const httplib::Result unavailable_upscale =
            client.Post("/v1/upscale", resize_source, "image/x-portable-pixmap");
        passed = passed && health->body.find("\"vlm_loaded\":false") != std::string::npos && unavailable &&
                 unavailable->status == 503 && unavailable->body.find("model_not_loaded") != std::string::npos &&
                 unavailable_ocr && unavailable_ocr->status == 503 &&
                 unavailable_ocr->body.find("model_not_configured") != std::string::npos && unavailable_depth &&
                 unavailable_depth->status == 503 && unavailable_clip && unavailable_clip->status == 503 &&
                 unavailable_generate && unavailable_generate->status == 503 && unavailable_upscale &&
                 unavailable_upscale->status == 503;
    } else {
        const httplib::Result caption =
            client.Post("/v1/caption?temperature=0&max_tokens=16", image_bytes, "image/png");
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
        const httplib::Result submitted_caption =
            client.Post("/v1/caption?temperature=0&max_tokens=8", async_headers, image_bytes, "image/png");
        httplib::Result async_caption;
        std::string async_caption_status;
        const bool async_caption_finished =
            wait_for_job(client, submitted_caption ? submitted_caption->get_header_value("Location") : "",
                         async_caption, async_caption_status);
        passed = passed && health->body.find("\"vlm_loaded\":true") != std::string::npos && caption &&
                 caption->status == 200 && caption->body.find("\"text\"") != std::string::npos && answer &&
                 answer->status == 200 &&
                 (answer->body.find("cat") != std::string::npos || answer->body.find("Cat") != std::string::npos) &&
                 stream && stream->status == 200 && stream->body.find("event: delta") != std::string::npos &&
                 stream->body.find("event: done") != std::string::npos &&
                 (stream->body.find("cat") != std::string::npos || stream->body.find("Cat") != std::string::npos) &&
                 invalid && (invalid->status == 400 || invalid->status == 415) && submitted_caption &&
                 submitted_caption->status == 202 && async_caption_finished && async_caption &&
                 async_caption->status == 200 && async_caption->body.find("\"text\"") != std::string::npos;
        if (!passed) {
            print_result("caption", caption);
            print_result("answer", answer);
            print_result("stream", stream);
            print_result("invalid", invalid);
            print_result("submitted caption", submitted_caption);
            print_result("async caption", async_caption);
        }
    }

    server.stop();
    listener.join();
    if (!passed) {
        print_result("root", root);
        print_result("playground", playground);
        print_result("playground direct", playground_direct);
        print_result("playground CSS", playground_css);
        print_result("playground JavaScript", playground_javascript);
        print_result("service info", service_info);
        print_result("health", health);
        print_result("models", models);
        print_result("cleared models", cleared_models);
        print_result("operations", operations);
        print_result("missing", missing);
        print_result("resized", resized);
        print_result("oversized", oversized);
        print_result("submitted job", submitted);
        print_result("async result", async_result);
        print_result("jobs", jobs);
        print_result("missing job", missing_job);
        print_result("async stream", async_stream);
    }
    return passed;
}

bool run_analysis_requests(imagecpp::server::HttpServerConfig config, const std::string &image_bytes) {
    config.port = 0;
    config.device = IMAGECPP_DEVICE_CPU;
    imagecpp::server::HttpServer server(std::move(config));
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(120);
    const httplib::Result health = client.Get("/healthz");
    const httplib::Result ocr = client.Post("/v1/ocr?psm=auto&dpi=300", image_bytes, "image/png");
    const httplib::Result depth = client.Post("/v1/depth", image_bytes, "image/png");
    const httplib::Result image_embedding = client.Post("/v1/embed/image", image_bytes, "image/png");
    const httplib::Result text_embedding = client.Post("/v1/embed/text", "{\"text\":\"a cat\"}", "application/json");
    const httplib::UploadFormDataItems classification_request = {
        {"image", image_bytes, "cat.png", "image/png"},
        {"labels", "[\"cat\",\"dog\",\"car\"]", "", ""},
    };
    const httplib::Result classification = client.Post("/v1/classify", classification_request);
    const httplib::Result models = client.Get("/v1/models");
    const httplib::Result cleared_models = client.Delete("/v1/models/cache");
    const bool passed =
        health && health->status == 200 && health->body.find("\"depth\":true") != std::string::npos &&
        health->body.find("\"clip\":true") != std::string::npos &&
        health->body.find("\"ocr\":true") != std::string::npos && ocr && ocr->status == 200 &&
        ocr->body.find("\"regions\"") != std::string::npos && depth && depth->status == 200 &&
        depth->body.find("\"depth\":{") != std::string::npos && depth->body.find("\"base64\"") != std::string::npos &&
        image_embedding && image_embedding->status == 200 &&
        image_embedding->body.find("\"embedding\"") != std::string::npos && text_embedding &&
        text_embedding->status == 200 && text_embedding->body.find("\"embedding\"") != std::string::npos &&
        classification && classification->status == 200 &&
        classification->body.find("\"label\":\"cat\"") != std::string::npos && models && models->status == 200 &&
        models->body.find("\"hits\":2") != std::string::npos &&
        models->body.find("\"loaded_families\":[\"clip\"]") != std::string::npos && cleared_models &&
        cleared_models->status == 200 && cleared_models->body.find("\"removed\":1") != std::string::npos &&
        cleared_models->body.find("\"loaded_families\":[]") != std::string::npos;
    server.stop();
    listener.join();
    if (!passed) {
        print_result("analysis health", health);
        print_result("ocr", ocr);
        print_result("depth", depth);
        print_result("image embedding", image_embedding);
        print_result("text embedding", text_embedding);
        print_result("classification", classification);
        print_result("models", models);
        print_result("cleared models", cleared_models);
    }
    return passed;
}

bool run_vision_requests(imagecpp::server::HttpServerConfig config, const std::string &image_bytes) {
    config.port = 0;
    imagecpp::server::HttpServer server(std::move(config));
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(180);
    const httplib::UploadFormDataItems segment_request = {
        {"image", image_bytes, "cat.png", "image/png"},
        {"points", "[[256,256,true]]", "", ""},
    };
    const httplib::Result segment = client.Post("/v1/segment", segment_request);
    httplib::UploadFormDataItems cutout_request = segment_request;
    cutout_request.push_back({"upscale", "4", "", ""});
    const httplib::Result cutout = client.Post("/v1/cutout", cutout_request);
    const httplib::UploadFormDataItems detect_request = {
        {"image", image_bytes, "cat.png", "image/png"},
        {"prompt", "cat", "", ""},
        {"threshold", "0.3", "", ""},
    };
    const httplib::Result detect = client.Post("/v1/detect", detect_request);
    httplib::UploadFormDataItems extract_request = detect_request;
    extract_request.push_back({"response", "json", "", ""});
    const httplib::Result extract = client.Post("/v1/extract", extract_request);
    const bool passed = segment && segment->status == 200 && segment->body.find("\"segments\"") != std::string::npos &&
                        segment->body.find("\"base64\"") != std::string::npos && cutout && cutout->status == 200 &&
                        cutout->get_header_value("Content-Type") == "image/png" && cutout->body.size() > 8 && detect &&
                        detect->status == 200 && detect->body.find("\"detections\"") != std::string::npos &&
                        detect->body.find("\"label\":\"cat\"") != std::string::npos && extract &&
                        extract->status == 200 && extract->body.find("\"image\":{") != std::string::npos &&
                        extract->body.find("\"matched_detection_count\"") != std::string::npos;
    server.stop();
    listener.join();
    if (!passed) {
        print_result("segment", segment);
        print_result("cutout", cutout);
        print_result("detect", detect);
        print_result("extract", extract);
    }
    return passed;
}

bool run_creative_requests(imagecpp::server::HttpServerConfig config, const std::string &image_bytes,
                           const std::string &small_image_bytes) {
    config.port = 0;
    imagecpp::server::HttpServer server(std::move(config));
    const int port = server.bind();
    std::thread listener([&server] { (void)server.listen(); });
    server.wait_until_ready();

    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(240);
    const std::string generation_body = "{\"prompt\":\"a red square on a white background\",\"width\":64,\"height\":64,"
                                        "\"steps\":1,\"seed\":1,\"response\":\"image\"}";
    const httplib::Result generated = client.Post("/v1/generate", generation_body, "application/json");
    const httplib::UploadFormDataItems edit_request = {
        {"image", image_bytes, "cat.png", "image/png"},
        {"prompt", "a warm photograph", "", ""},
        {"steps", "1", "", ""},
        {"seed", "1", "", ""},
    };
    const httplib::Result edited = client.Post("/v1/edit", edit_request);
    const httplib::Result upscaled = client.Post("/v1/upscale?factor=4", small_image_bytes, "image/x-portable-pixmap");
    const bool passed = generated && generated->status == 200 &&
                        generated->get_header_value("Content-Type") == "image/png" && generated->body.size() > 8 &&
                        edited && edited->status == 200 && edited->body.find("\"images\"") != std::string::npos &&
                        edited->body.find("\"base64\"") != std::string::npos && upscaled && upscaled->status == 200 &&
                        upscaled->get_header_value("Content-Type") == "image/png" && upscaled->body.size() > 8;
    server.stop();
    listener.join();
    if (!passed) {
        print_result("generated", generated);
        print_result("edited", edited);
        print_result("upscaled", upscaled);
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
        if (argc == 6 && std::string(argv[1]) == "--analysis") {
            config.ocr_model_path = argv[2];
            config.depth_model_path = argv[3];
            config.clip_model_path = argv[4];
            const std::string image = read_file(argv[5]);
            return !image.empty() && run_analysis_requests(std::move(config), image) ? 0 : 1;
        }
        if (argc == 6 && std::string(argv[1]) == "--vision") {
            config.segment_model_path = argv[2];
            config.detect_model_path = argv[3];
            config.upscaler_model_path = argv[4];
            const std::string image = read_file(argv[5]);
            return !image.empty() && run_vision_requests(std::move(config), image) ? 0 : 1;
        }
        if (argc == 6 && std::string(argv[1]) == "--creative") {
            config.diffusion_checkpoint_path = argv[2];
            config.upscaler_model_path = argv[3];
            const std::string image = read_file(argv[4]);
            const std::string small_image = read_file(argv[5]);
            return !image.empty() && !small_image.empty() &&
                           run_creative_requests(std::move(config), image, small_image)
                       ? 0
                       : 1;
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
