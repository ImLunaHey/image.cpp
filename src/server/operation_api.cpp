#include "server/operation_api.hpp"

#include "httplib.h"
#include "server/http_common.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace imagecpp::server {

class OperationApi::Impl final {
  public:
    Impl(imagecpp::Runtime &runtime, const HttpServerConfig &config, std::mutex &model_mutex)
        : config_(config) {
        (void)runtime;
        (void)model_mutex;
    }

    void register_routes(httplib::Server &server) {
        server.Post("/v1/resize", [this](const httplib::Request &request, httplib::Response &response) {
            handle_resize(request, response);
        });
    }

  private:
    void handle_resize(const httplib::Request &request, httplib::Response &response) const {
        try {
            const auto width_value = detail::request_value(request, "width");
            const auto height_value = detail::request_value(request, "height");
            if (!width_value || !height_value) {
                throw std::invalid_argument("resize requires width and height");
            }
            const uint32_t width = detail::parse_uint32(*width_value, "width");
            const uint32_t height = detail::parse_uint32(*height_value, "height");
            if (static_cast<uint64_t>(width) * height > config_.max_output_pixels) {
                throw std::invalid_argument("requested image exceeds the configured output pixel limit");
            }

            std::unique_ptr<imagecpp::Image> source;
            try {
                source = std::make_unique<imagecpp::Image>(detail::decode_request_image(request));
            } catch (const imagecpp::Error &error) {
                detail::set_invalid_image(response, error);
                return;
            }
            const imagecpp::Image &source_image = *source;
            const imagecpp_const_image_view source_view = source_image.view();
            imagecpp_image_desc description{};
            description.struct_size = sizeof(description);
            description.width = width;
            description.height = height;
            description.pixel_format = source_view.pixel_format;
            description.color_space = source_view.color_space;
            imagecpp::Image destination(description);

            imagecpp_resize_filter filter = IMAGECPP_RESIZE_BILINEAR;
            if (const auto value = detail::request_value(request, "filter")) {
                if (*value == "nearest") {
                    filter = IMAGECPP_RESIZE_NEAREST;
                } else if (*value != "bilinear") {
                    throw std::invalid_argument("filter must be nearest or bilinear");
                }
            }
            imagecpp::resize(source_view, destination.view(), filter);

            const imagecpp_file_format format = detail::response_format(request);
            int quality = 90;
            if (const auto value = detail::request_value(request, "quality")) {
                const uint32_t parsed = detail::parse_uint32(*value, "quality");
                if (parsed > 100) {
                    throw std::invalid_argument("quality must be between 1 and 100");
                }
                quality = static_cast<int>(parsed);
            }
            bool lossless = false;
            if (const auto value = detail::request_value(request, "lossless")) {
                lossless = detail::parse_bool(*value, "lossless");
            }
            detail::set_image(response, destination, format, quality, lossless);
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    const HttpServerConfig &config_;
};

OperationApi::OperationApi(imagecpp::Runtime &runtime, const HttpServerConfig &config, std::mutex &model_mutex)
    : implementation_(std::make_unique<Impl>(runtime, config, model_mutex)) {}

OperationApi::~OperationApi() = default;

void OperationApi::register_routes(httplib::Server &server) { implementation_->register_routes(server); }

} // namespace imagecpp::server
