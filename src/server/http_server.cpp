#include "server/http_server.hpp"

#include "httplib.h"
#include "imagecpp/imagecpp.hpp"
#include "json.hpp"

#include <exception>
#include <stdexcept>
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
        server_.set_payload_max_length(config_.max_upload_bytes);
        server_.set_default_headers({{"X-Content-Type-Options", "nosniff"}});
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
        server_.Get("/", [](const httplib::Request &, httplib::Response &response) {
            set_json(response, 200,
                     {{"name", "image.cpp"},
                      {"version", imagecpp_version_string()},
                      {"endpoints", {"/healthz", "/v1/operations", "/v1/caption", "/v1/ask"}}});
        });
        server_.Get("/healthz", [](const httplib::Request &, httplib::Response &response) {
            set_json(response, 200,
                     {{"status", "ok"}, {"version", imagecpp_version_string()}, {"vlm_loaded", false}});
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
        server_.Post("/v1/caption", [](const httplib::Request &, httplib::Response &response) {
            set_error(response, 503, "model_not_loaded", "no vision-language model is loaded");
        });
        server_.Post("/v1/ask", [](const httplib::Request &, httplib::Response &response) {
            set_error(response, 503, "model_not_loaded", "no vision-language model is loaded");
        });
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

    HttpServerConfig config_;
    imagecpp::Runtime runtime_;
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
