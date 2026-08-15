#ifndef IMAGECPP_SERVER_HTTP_SERVER_HPP
#define IMAGECPP_SERVER_HTTP_SERVER_HPP

#include "imagecpp/imagecpp.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace imagecpp::server {

struct HttpServerConfig {
    std::string host = "127.0.0.1";
    int port = 8080;
    size_t max_upload_bytes = 32U * 1024U * 1024U;
    uint64_t max_output_pixels = 64U * 1024U * 1024U;
    std::string vlm_model_path;
    std::string vlm_projection_model_path;
    int32_t threads = 0;
    imagecpp_device device = IMAGECPP_DEVICE_AUTO;
    uint32_t context_size = 4096;
};

class HttpServer final {
  public:
    explicit HttpServer(HttpServerConfig config);
    ~HttpServer();

    HttpServer(const HttpServer &) = delete;
    HttpServer &operator=(const HttpServer &) = delete;

    int bind();
    bool listen();
    void wait_until_ready() const;
    void stop() noexcept;
    int port() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace imagecpp::server

#endif
