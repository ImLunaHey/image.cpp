#include "server/http_server.hpp"

#include "httplib.h"
#include "imagecpp/imagecpp.hpp"
#include "json.hpp"
#include "server/operation_api.hpp"
#include "server/playground.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace imagecpp::server {
namespace {

using Json = nlohmann::json;

void set_json(httplib::Response &response, int status, const Json &body) {
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

void set_error(httplib::Response &response, int status, const std::string &code, const std::string &message) {
    set_json(response, status, {{"error", {{"code", code}, {"message", message}}}});
}

void set_web_content(httplib::Response &response, std::string_view content, const char *content_type) {
    response.set_header("Cache-Control", "no-cache");
    response.set_content(content.data(), content.size(), content_type);
}

void set_playground(httplib::Response &response) {
    response.set_header("Content-Security-Policy",
                        "default-src 'self'; img-src 'self' data: blob:; style-src 'self'; script-src 'self'; "
                        "connect-src 'self'; object-src 'none'; base-uri 'none'");
    response.set_header("Referrer-Policy", "no-referrer");
    set_web_content(response, playground_html(), "text/html; charset=utf-8");
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

const char *status_code_name(imagecpp_status status) {
    switch (status) {
    case IMAGECPP_STATUS_OK:
        return "ok";
    case IMAGECPP_STATUS_INVALID_ARGUMENT:
        return "invalid_argument";
    case IMAGECPP_STATUS_OUT_OF_RANGE:
        return "out_of_range";
    case IMAGECPP_STATUS_UNSUPPORTED:
        return "unsupported";
    case IMAGECPP_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case IMAGECPP_STATUS_INTERNAL:
        return "internal_error";
    case IMAGECPP_STATUS_IO_ERROR:
        return "io_error";
    case IMAGECPP_STATUS_MODEL_ERROR:
        return "model_error";
    case IMAGECPP_STATUS_NOT_READY:
        return "not_ready";
    }
    return "unknown_error";
}

Json text_result_json(const imagecpp::TextInfo &result) {
    return {{"text", result.text},
            {"prompt_tokens", result.prompt_tokens},
            {"generated_tokens", result.generated_tokens},
            {"finish_reason", finish_reason_name(result.finish_reason)}};
}

Json model_cache_json(const ModelCacheInfo &info) {
    return {{"capacity", info.capacity},
            {"size", info.size},
            {"hits", info.hits},
            {"misses", info.misses},
            {"evictions", info.evictions},
            {"clears", info.clears},
            {"loaded_families", info.loaded_families}};
}

std::optional<std::string> request_value(const httplib::Request &request, const std::string &name) {
    if (request.is_multipart_form_data() && request.form.has_field(name)) {
        return request.form.get_field(name);
    }
    if (request.has_param(name)) {
        return request.get_param_value(name);
    }
    return std::nullopt;
}

std::string request_image_bytes(const httplib::Request &request) {
    if (request.is_multipart_form_data()) {
        if (!request.form.has_file("image")) {
            throw std::invalid_argument("multipart request is missing the image file field");
        }
        return request.form.get_file("image").content;
    }
    if (request.body.empty()) {
        throw std::invalid_argument("request image body is empty");
    }
    return request.body;
}

uint32_t parse_uint32(const std::string &value, const char *name, bool allow_zero = false) {
    uint64_t parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc() || conversion.ptr != value.data() + value.size() || (!allow_zero && parsed == 0) ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<uint32_t>(parsed);
}

float parse_float(const std::string &value, const char *name) {
    char *end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (errno == ERANGE || end != value.c_str() + value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return parsed;
}

bool parse_bool(const std::string &value, const char *name) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw std::invalid_argument(std::string("invalid ") + name);
}

struct QueryInput {
    std::string prompt;
    imagecpp_visual_query_options options{};
    bool stream = false;
};

QueryInput query_input(const httplib::Request &request, bool ask) {
    QueryInput result;
    imagecpp_visual_query_options_init(&result.options);
    const std::optional<std::string> prompt = request_value(request, ask ? "question" : "prompt");
    if (ask && (!prompt || prompt->empty())) {
        throw std::invalid_argument("visual question is empty");
    }
    result.prompt = prompt.value_or("");
    result.options.prompt = result.prompt.empty() ? nullptr : result.prompt.c_str();
    if (const auto value = request_value(request, "max_tokens")) {
        result.options.max_tokens = parse_uint32(*value, "max_tokens");
    }
    if (const auto value = request_value(request, "temperature")) {
        result.options.temperature = parse_float(*value, "temperature");
    }
    if (const auto value = request_value(request, "top_p")) {
        result.options.top_p = parse_float(*value, "top_p");
    }
    if (const auto value = request_value(request, "top_k")) {
        const uint32_t parsed = parse_uint32(*value, "top_k", true);
        if (parsed > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            throw std::invalid_argument("invalid top_k");
        }
        result.options.top_k = static_cast<int32_t>(parsed);
    }
    if (const auto value = request_value(request, "seed")) {
        result.options.seed = parse_uint32(*value, "seed", true);
    }
    if (const auto value = request_value(request, "stream")) {
        result.stream = parse_bool(*value, "stream");
    }
    const std::string accept = request.get_header_value("Accept");
    if (accept.find("text/event-stream") != std::string::npos) {
        result.stream = true;
    }
    return result;
}

size_t valid_utf8_prefix(const std::string &text) {
    size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        size_t length = 0;
        if (first <= 0x7F) {
            length = 1;
        } else if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
        } else {
            throw std::runtime_error("VLM stream produced invalid UTF-8");
        }
        if (text.size() - offset < length) {
            break;
        }
        for (size_t index = 1; index < length; ++index) {
            const auto continuation = static_cast<unsigned char>(text[offset + index]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::runtime_error("VLM stream produced invalid UTF-8");
            }
        }
        if ((length == 3 && first == 0xE0 && static_cast<unsigned char>(text[offset + 1]) < 0xA0) ||
            (length == 3 && first == 0xED && static_cast<unsigned char>(text[offset + 1]) >= 0xA0) ||
            (length == 4 && first == 0xF0 && static_cast<unsigned char>(text[offset + 1]) < 0x90) ||
            (length == 4 && first == 0xF4 && static_cast<unsigned char>(text[offset + 1]) >= 0x90)) {
            throw std::runtime_error("VLM stream produced invalid UTF-8");
        }
        offset += length;
    }
    return offset;
}

std::string sse_event(const char *event, const Json &data) {
    return std::string("event: ") + event + "\ndata: " + data.dump() + "\n\n";
}

int error_status(const imagecpp::Error &error) {
    switch (error.status()) {
    case IMAGECPP_STATUS_INVALID_ARGUMENT:
    case IMAGECPP_STATUS_OUT_OF_RANGE:
    case IMAGECPP_STATUS_IO_ERROR:
        return 400;
    case IMAGECPP_STATUS_UNSUPPORTED:
        return 415;
    case IMAGECPP_STATUS_NOT_READY:
    case IMAGECPP_STATUS_OUT_OF_MEMORY:
        return 503;
    case IMAGECPP_STATUS_MODEL_ERROR:
        return 422;
    case IMAGECPP_STATUS_INTERNAL:
    case IMAGECPP_STATUS_OK:
        return 500;
    }
    return 500;
}

} // namespace

class HttpServer::Impl final {
  public:
    explicit Impl(HttpServerConfig config) : config_(std::move(config)) {
        if (config_.host.empty()) {
            throw std::invalid_argument("HTTP server host cannot be empty");
        }
        if (config_.port < 0 || config_.port > 65535) {
            throw std::invalid_argument("HTTP server port must be between 0 and 65535");
        }
        if (config_.max_upload_bytes == 0) {
            throw std::invalid_argument("HTTP server upload limit must be positive");
        }
        if (config_.vlm_model_path.empty() != config_.vlm_projection_model_path.empty()) {
            throw std::invalid_argument("both VLM language and projection model paths are required");
        }
        if (!config_.vlm_model_path.empty()) {
            imagecpp_vlm_model_options options{};
            imagecpp_vlm_model_options_init(&options);
            options.model_path = config_.vlm_model_path.c_str();
            options.projection_model_path = config_.vlm_projection_model_path.c_str();
            options.threads = config_.threads;
            options.device = config_.device;
            options.context_size = config_.context_size;
            vlm_ = std::make_unique<imagecpp::Model>(runtime_, options);
        }
        server_.set_payload_max_length(config_.max_upload_bytes);
        server_.set_default_headers({{"X-Content-Type-Options", "nosniff"}});
        operation_api_ = std::make_unique<OperationApi>(runtime_, config_, model_mutex_);
        register_routes();
    }

    int bind() {
        if (bound_port_ >= 0) {
            return bound_port_;
        }
        if (config_.port == 0) {
            bound_port_ = server_.bind_to_any_port(config_.host);
        } else if (server_.bind_to_port(config_.host, config_.port)) {
            bound_port_ = config_.port;
        }
        if (bound_port_ < 0) {
            throw std::runtime_error("failed to bind HTTP server to " + config_.host + ":" +
                                     std::to_string(config_.port));
        }
        return bound_port_;
    }

    bool listen() {
        bind();
        return server_.listen_after_bind();
    }

    void wait_until_ready() const { server_.wait_until_ready(); }
    void stop() noexcept { server_.stop(); }
    int port() const noexcept { return bound_port_; }

  private:
    void register_routes() {
        const auto set_service_info = [](httplib::Response &response) {
            set_json(response, 200,
                     {{"name", "image.cpp"},
                      {"version", imagecpp_version_string()},
                      {"endpoints",
                       {"/playground",  "/healthz",     "/v1/info",   "/v1/operations",  "/v1/resize",
                        "/v1/models",   "/v1/ocr",      "/v1/depth",  "/v1/embed/image", "/v1/embed/text",
                        "/v1/classify", "/v1/segment",  "/v1/detect", "/v1/cutout",      "/v1/remove-background",
                        "/v1/extract",  "/v1/generate", "/v1/edit",   "/v1/upscale",     "/v1/caption",
                        "/v1/ask"}}});
        };
        server_.Get("/", [set_service_info](const httplib::Request &request, httplib::Response &response) {
            if (request.get_header_value("Accept").find("text/html") != std::string::npos) {
                set_playground(response);
                return;
            }
            set_service_info(response);
        });
        server_.Get("/playground",
                    [](const httplib::Request &, httplib::Response &response) { set_playground(response); });
        server_.Get("/assets/playground.css", [](const httplib::Request &, httplib::Response &response) {
            set_web_content(response, playground_css(), "text/css; charset=utf-8");
        });
        server_.Get("/assets/playground.js", [](const httplib::Request &, httplib::Response &response) {
            set_web_content(response, playground_javascript(), "text/javascript; charset=utf-8");
        });
        server_.Get("/v1/info", [set_service_info](const httplib::Request &, httplib::Response &response) {
            set_service_info(response);
        });
        server_.Get("/healthz", [this](const httplib::Request &, httplib::Response &response) {
            set_json(
                response, 200,
                {{"status", "ok"},
                 {"version", imagecpp_version_string()},
                 {"vlm_loaded", vlm_ != nullptr},
                 {"model_cache", model_cache_json(operation_api_->model_cache_info())},
                 {"configured_models",
                  {{"segment", !config_.segment_model_path.empty()},
                   {"detect", !config_.detect_model_path.empty()},
                   {"depth", !config_.depth_model_path.empty()},
                   {"clip", !config_.clip_model_path.empty()},
                   {"ocr", !config_.ocr_model_path.empty()},
                   {"diffusion", !config_.diffusion_checkpoint_path.empty() || !config_.diffusion_model_path.empty()},
                   {"upscaler", !config_.upscaler_model_path.empty()},
                   {"vlm", vlm_ != nullptr}}}});
        });
        server_.Get("/v1/models", [this](const httplib::Request &, httplib::Response &response) {
            set_json(
                response, 200,
                {{"cache", model_cache_json(operation_api_->model_cache_info())}, {"resident_vlm", vlm_ != nullptr}});
        });
        server_.Delete("/v1/models/cache", [this](const httplib::Request &, httplib::Response &response) {
            const size_t removed = operation_api_->clear_model_cache();
            set_json(response, 200,
                     {{"removed", removed}, {"cache", model_cache_json(operation_api_->model_cache_info())}});
        });
        server_.Get("/v1/operations", [this](const httplib::Request &, httplib::Response &response) {
            Json operations = Json::array();
            for (const imagecpp::OperationInfo &operation : runtime_.operations()) {
                operations.push_back({{"id", operation.id},
                                      {"name", operation.name},
                                      {"description", operation.description},
                                      {"task", static_cast<int>(operation.task)},
                                      {"input_kind", static_cast<int>(operation.input_kind)},
                                      {"output_kind", static_cast<int>(operation.output_kind)}});
            }
            set_json(response, 200, {{"operations", std::move(operations)}});
        });
        server_.Post("/v1/caption", [this](const httplib::Request &request, httplib::Response &response) {
            handle_visual_query(request, response, false);
        });
        server_.Post("/v1/ask", [this](const httplib::Request &request, httplib::Response &response) {
            handle_visual_query(request, response, true);
        });
        operation_api_->register_routes(server_);
        server_.set_error_handler([](const httplib::Request &, httplib::Response &response) {
            if (response.status == 404) {
                set_error(response, 404, "not_found", "HTTP endpoint not found");
            }
        });
        server_.set_exception_handler(
            [](const httplib::Request &, httplib::Response &response, std::exception_ptr exception) {
                try {
                    if (exception) {
                        std::rethrow_exception(exception);
                    }
                } catch (const std::exception &error) {
                    set_error(response, 500, "internal_error", error.what());
                    return;
                } catch (...) {
                }
                set_error(response, 500, "internal_error", "unexpected HTTP server failure");
            });
    }

    void handle_visual_query(const httplib::Request &request, httplib::Response &response, bool ask) {
        if (!vlm_) {
            set_error(response, 503, "model_not_loaded", "no vision-language model is loaded");
            return;
        }
        try {
            QueryInput query = query_input(request, ask);
            const std::string bytes = request_image_bytes(request);
            std::shared_ptr<imagecpp::Image> image;
            try {
                image = std::make_shared<imagecpp::Image>(imagecpp::decode(bytes.data(), bytes.size()));
            } catch (const imagecpp::Error &error) {
                if (error.status() == IMAGECPP_STATUS_OUT_OF_MEMORY) {
                    throw;
                }
                set_error(response, 400, "invalid_image", error.what());
                return;
            }
            if (!query.stream) {
                const std::lock_guard<std::mutex> lock(model_mutex_);
                const imagecpp::TextInfo result = imagecpp::visual_query(*vlm_, *image, query.options).info();
                set_json(response, 200, text_result_json(result));
                return;
            }

            struct StreamState {
                imagecpp::Model *model = nullptr;
                std::mutex *model_mutex = nullptr;
                std::shared_ptr<imagecpp::Image> image;
                QueryInput query;
                bool started = false;
                std::string pending_utf8;
            };
            auto state = std::make_shared<StreamState>();
            state->model = vlm_.get();
            state->model_mutex = &model_mutex_;
            state->image = std::move(image);
            state->query = std::move(query);
            state->query.options.prompt = state->query.prompt.empty() ? nullptr : state->query.prompt.c_str();
            response.set_header("Cache-Control", "no-cache");
            response.set_header("X-Accel-Buffering", "no");
            response.set_chunked_content_provider("text/event-stream; charset=utf-8", [state](size_t,
                                                                                              httplib::DataSink &sink) {
                if (state->started) {
                    return false;
                }
                state->started = true;
                try {
                    const std::lock_guard<std::mutex> lock(*state->model_mutex);
                    imagecpp::TextResult generated = imagecpp::visual_query_stream(
                        *state->model, *state->image, state->query.options, [&](std::string_view chunk) {
                            state->pending_utf8.append(chunk.data(), chunk.size());
                            const size_t prefix_size = valid_utf8_prefix(state->pending_utf8);
                            if (prefix_size == 0) {
                                return sink.is_writable();
                            }
                            const std::string event =
                                sse_event("delta", {{"delta", state->pending_utf8.substr(0, prefix_size)}});
                            state->pending_utf8.erase(0, prefix_size);
                            return sink.write(event.data(), event.size());
                        });
                    if (!state->pending_utf8.empty()) {
                        throw std::runtime_error("VLM stream ended with incomplete UTF-8");
                    }
                    const std::string done = sse_event("done", text_result_json(generated.info()));
                    (void)sink.write(done.data(), done.size());
                } catch (const imagecpp::Error &error) {
                    const std::string event = sse_event(
                        "error", {{"error", {{"code", status_code_name(error.status())}, {"message", error.what()}}}});
                    (void)sink.write(event.data(), event.size());
                } catch (const std::exception &error) {
                    const std::string event =
                        sse_event("error", {{"error", {{"code", "internal_error"}, {"message", error.what()}}}});
                    (void)sink.write(event.data(), event.size());
                }
                sink.done();
                return false;
            });
        } catch (const imagecpp::Error &error) {
            set_error(response, error_status(error), status_code_name(error.status()), error.what());
        } catch (const std::invalid_argument &error) {
            set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            set_error(response, 500, "internal_error", error.what());
        }
    }

    HttpServerConfig config_;
    imagecpp::Runtime runtime_;
    std::mutex model_mutex_;
    std::unique_ptr<imagecpp::Model> vlm_;
    std::unique_ptr<OperationApi> operation_api_;
    httplib::Server server_;
    int bound_port_ = -1;
};

HttpServer::HttpServer(HttpServerConfig config) : implementation_(std::make_unique<Impl>(std::move(config))) {}
HttpServer::~HttpServer() = default;

int HttpServer::bind() { return implementation_->bind(); }
bool HttpServer::listen() { return implementation_->listen(); }
void HttpServer::wait_until_ready() const { implementation_->wait_until_ready(); }
void HttpServer::stop() noexcept { implementation_->stop(); }
int HttpServer::port() const noexcept { return implementation_->port(); }

} // namespace imagecpp::server
