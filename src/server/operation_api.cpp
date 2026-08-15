#include "server/operation_api.hpp"

#include "httplib.h"
#include "server/http_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace imagecpp::server {
namespace {

imagecpp_model_options model_options(const HttpServerConfig &config, const std::string &path) {
    imagecpp_model_options options{};
    imagecpp_model_options_init(&options);
    options.model_path = path.c_str();
    options.threads = config.threads;
    options.device = config.device;
    return options;
}

std::unique_ptr<imagecpp::Image> request_image(const httplib::Request &request, httplib::Response &response) {
    try {
        return std::make_unique<imagecpp::Image>(detail::decode_request_image(request));
    } catch (const imagecpp::Error &error) {
        detail::set_invalid_image(response, error);
        return nullptr;
    }
}

detail::Json box_json(const imagecpp_box &box) { return {box.x0, box.y0, box.x1, box.y1}; }

detail::Json nullable_index(size_t value) {
    return value == IMAGECPP_NO_INDEX ? detail::Json(nullptr) : detail::Json(value);
}

const char *region_level_name(imagecpp_text_region_level level) {
    switch (level) {
    case IMAGECPP_TEXT_REGION_BLOCK:
        return "block";
    case IMAGECPP_TEXT_REGION_PARAGRAPH:
        return "paragraph";
    case IMAGECPP_TEXT_REGION_LINE:
        return "line";
    case IMAGECPP_TEXT_REGION_WORD:
        return "word";
    }
    return "unknown";
}

imagecpp_ocr_page_segmentation ocr_page_segmentation(const std::string &value) {
    if (value == "auto") {
        return IMAGECPP_OCR_PAGE_AUTO;
    }
    if (value == "column") {
        return IMAGECPP_OCR_PAGE_SINGLE_COLUMN;
    }
    if (value == "block") {
        return IMAGECPP_OCR_PAGE_SINGLE_BLOCK;
    }
    if (value == "line") {
        return IMAGECPP_OCR_PAGE_SINGLE_LINE;
    }
    if (value == "word") {
        return IMAGECPP_OCR_PAGE_SINGLE_WORD;
    }
    if (value == "sparse") {
        return IMAGECPP_OCR_PAGE_SPARSE_TEXT;
    }
    if (value == "raw-line") {
        return IMAGECPP_OCR_PAGE_RAW_LINE;
    }
    throw std::invalid_argument("psm must be auto, column, block, line, word, sparse, or raw-line");
}

imagecpp::Image visualized_float_map(const imagecpp_const_image_view &input, bool invert) {
    if (input.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_F32 || input.data == nullptr) {
        throw std::runtime_error("model returned an invalid float image");
    }
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (uint32_t row = 0; row < input.height; ++row) {
        const auto *values = reinterpret_cast<const float *>(static_cast<const uint8_t *>(input.data) +
                                                             static_cast<size_t>(row) * input.row_stride);
        for (uint32_t column = 0; column < input.width; ++column) {
            if (std::isfinite(values[column])) {
                minimum = std::min(minimum, values[column]);
                maximum = std::max(maximum, values[column]);
            }
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        throw std::runtime_error("model returned no finite float image values");
    }
    const imagecpp_image_desc description{sizeof(imagecpp_image_desc), input.width, input.height, 0,
                                          IMAGECPP_PIXEL_FORMAT_GRAY_U8, IMAGECPP_COLOR_SPACE_UNKNOWN};
    imagecpp::Image output(description);
    const imagecpp_image_view output_view = output.view();
    const float range = maximum - minimum;
    for (uint32_t row = 0; row < input.height; ++row) {
        const auto *source = reinterpret_cast<const float *>(static_cast<const uint8_t *>(input.data) +
                                                             static_cast<size_t>(row) * input.row_stride);
        auto *destination =
            static_cast<uint8_t *>(output_view.data) + static_cast<size_t>(row) * output_view.row_stride;
        for (uint32_t column = 0; column < input.width; ++column) {
            float normalized = range > 0.0F && std::isfinite(source[column])
                                   ? (source[column] - minimum) / range
                                   : 0.0F;
            if (invert) {
                normalized = 1.0F - normalized;
            }
            destination[column] = static_cast<uint8_t>(std::clamp(normalized * 255.0F + 0.5F, 0.0F, 255.0F));
        }
    }
    return output;
}

std::optional<std::string> body_string_value(const httplib::Request &request, const std::string &name) {
    if (const auto value = detail::request_value(request, name)) {
        return value;
    }
    const std::string content_type = request.get_header_value("Content-Type");
    if (content_type.find("application/json") == std::string::npos || request.body.empty()) {
        return std::nullopt;
    }
    try {
        const detail::Json body = detail::Json::parse(request.body);
        if (!body.contains(name) || !body.at(name).is_string()) {
            return std::nullopt;
        }
        return body.at(name).get<std::string>();
    } catch (const nlohmann::json::exception &) {
        throw std::invalid_argument("request body is not valid JSON");
    }
}

std::vector<std::string> classification_labels(const httplib::Request &request) {
    std::optional<std::string> value = detail::request_value(request, "labels");
    if (!value) {
        const std::string content_type = request.get_header_value("Content-Type");
        if (content_type.find("application/json") != std::string::npos && !request.body.empty()) {
            try {
                const detail::Json body = detail::Json::parse(request.body);
                if (body.contains("labels")) {
                    return body.at("labels").get<std::vector<std::string>>();
                }
            } catch (const nlohmann::json::exception &) {
                throw std::invalid_argument("labels must be a JSON array of strings");
            }
        }
    }
    if (!value) {
        throw std::invalid_argument("classification requires labels");
    }
    if (!value->empty() && value->front() == '[') {
        try {
            return detail::Json::parse(*value).get<std::vector<std::string>>();
        } catch (const nlohmann::json::exception &) {
            throw std::invalid_argument("labels must be a JSON array of strings or a comma-separated list");
        }
    }
    std::vector<std::string> labels;
    std::stringstream input(*value);
    std::string label;
    while (std::getline(input, label, ',')) {
        const size_t first = label.find_first_not_of(" \t\r\n");
        const size_t last = label.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            labels.push_back(label.substr(first, last - first + 1));
        }
    }
    if (labels.empty()) {
        throw std::invalid_argument("classification requires at least one non-empty label");
    }
    return labels;
}

} // namespace

class OperationApi::Impl final {
  public:
    Impl(imagecpp::Runtime &runtime, const HttpServerConfig &config, std::mutex &model_mutex)
        : runtime_(runtime), config_(config), model_mutex_(model_mutex) {}

    void register_routes(httplib::Server &server) {
        server.Post("/v1/resize", [this](const httplib::Request &request, httplib::Response &response) {
            handle_resize(request, response);
        });
        server.Post("/v1/ocr", [this](const httplib::Request &request, httplib::Response &response) {
            handle_ocr(request, response);
        });
        server.Post("/v1/depth", [this](const httplib::Request &request, httplib::Response &response) {
            handle_depth(request, response);
        });
        server.Post("/v1/embed/image", [this](const httplib::Request &request, httplib::Response &response) {
            handle_embed_image(request, response);
        });
        server.Post("/v1/embed/text", [this](const httplib::Request &request, httplib::Response &response) {
            handle_embed_text(request, response);
        });
        server.Post("/v1/classify", [this](const httplib::Request &request, httplib::Response &response) {
            handle_classify(request, response);
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

    bool require_model(httplib::Response &response, const std::string &path, const char *name) const {
        if (!path.empty()) {
            return true;
        }
        detail::set_error(response, 503, "model_not_configured", std::string("no ") + name + " model is configured");
        return false;
    }

    void handle_ocr(const httplib::Request &request, httplib::Response &response) {
        if (!require_model(response, config_.ocr_model_path, "OCR")) {
            return;
        }
        try {
            std::unique_ptr<imagecpp::Image> image = request_image(request, response);
            if (!image) {
                return;
            }
            imagecpp_ocr_options options{};
            imagecpp_ocr_options_init(&options);
            if (const auto value = detail::request_value(request, "psm")) {
                options.page_segmentation = ocr_page_segmentation(*value);
            }
            if (const auto value = detail::request_value(request, "dpi")) {
                options.source_dpi = detail::parse_uint32(*value, "dpi");
            }
            if (const auto value = detail::request_value(request, "preserve_spaces")) {
                options.preserve_interword_spaces = detail::parse_bool(*value, "preserve_spaces") ? 1 : 0;
            }

            const std::lock_guard<std::mutex> lock(model_mutex_);
            const imagecpp_model_options load_options = model_options(config_, config_.ocr_model_path);
            imagecpp::Model model(runtime_, "image.ocr.tesseract", load_options);
            const imagecpp::OcrResult result = imagecpp::ocr(model, *image, options);
            const imagecpp::OcrInfo info = result.info();
            detail::Json regions = detail::Json::array();
            for (size_t index = 0; index < result.size(); ++index) {
                const imagecpp::TextRegionInfo region = result.at(index);
                detail::Json baseline = nullptr;
                if (region.has_baseline) {
                    baseline = {region.baseline.x0, region.baseline.y0, region.baseline.x1, region.baseline.y1};
                }
                regions.push_back({{"level", region_level_name(region.level)},
                                   {"text", region.text},
                                   {"box", box_json(region.box)},
                                   {"confidence", region.confidence},
                                   {"block_index", nullable_index(region.block_index)},
                                   {"paragraph_index", nullable_index(region.paragraph_index)},
                                   {"line_index", nullable_index(region.line_index)},
                                   {"word_index", nullable_index(region.word_index)},
                                   {"block_type", static_cast<int>(region.block_type)},
                                   {"baseline", std::move(baseline)},
                                   {"orientation", static_cast<int>(region.orientation)},
                                   {"writing_direction", static_cast<int>(region.writing_direction)},
                                   {"textline_order", static_cast<int>(region.textline_order)},
                                   {"deskew_angle_degrees", region.deskew_angle_degrees}});
            }
            detail::set_json(response, 200,
                             {{"text", info.text},
                              {"language", info.language},
                              {"mean_confidence", info.mean_confidence},
                              {"regions", std::move(regions)}});
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    void handle_depth(const httplib::Request &request, httplib::Response &response) {
        if (!require_model(response, config_.depth_model_path, "depth")) {
            return;
        }
        try {
            std::unique_ptr<imagecpp::Image> image = request_image(request, response);
            if (!image) {
                return;
            }
            imagecpp_depth_options options{};
            imagecpp_depth_options_init(&options);
            if (const auto value = detail::request_value(request, "pose")) {
                options.include_pose = detail::parse_bool(*value, "pose") ? 1 : 0;
            }
            bool invert = true;
            if (const auto value = detail::request_value(request, "invert")) {
                invert = detail::parse_bool(*value, "invert");
            }

            const std::lock_guard<std::mutex> lock(model_mutex_);
            imagecpp_model_options load_options = model_options(config_, config_.depth_model_path);
            load_options.device = IMAGECPP_DEVICE_AUTO;
            imagecpp::Model model(runtime_, "image.depth.depth-anything", load_options);
            const imagecpp::DepthResult result = imagecpp::depth(model, *image, options);
            const imagecpp_depth_info info = result.info();
            imagecpp::Image depth_image = visualized_float_map(info.depth, invert);
            if (detail::request_value(request, "response").value_or("json") == "image") {
                detail::set_image(response, depth_image, detail::response_format(request));
                return;
            }
            const imagecpp::Image &const_depth_image = depth_image;
            detail::Json body = {{"is_metric", info.is_metric != 0},
                                 {"has_pose", info.has_pose != 0},
                                 {"depth", detail::encoded_image_json(const_depth_image.view())}};
            if (info.confidence.data != nullptr) {
                imagecpp::Image confidence = visualized_float_map(info.confidence, false);
                const imagecpp::Image &const_confidence = confidence;
                body["confidence"] = detail::encoded_image_json(const_confidence.view());
            }
            if (info.sky.data != nullptr) {
                imagecpp::Image sky = visualized_float_map(info.sky, false);
                const imagecpp::Image &const_sky = sky;
                body["sky"] = detail::encoded_image_json(const_sky.view());
            }
            if (info.has_pose != 0) {
                body["extrinsics"] = std::vector<float>(std::begin(info.extrinsics), std::end(info.extrinsics));
                body["intrinsics"] = std::vector<float>(std::begin(info.intrinsics), std::end(info.intrinsics));
            }
            detail::set_json(response, 200, body);
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    void handle_embed_image(const httplib::Request &request, httplib::Response &response) {
        if (!require_model(response, config_.clip_model_path, "CLIP")) {
            return;
        }
        try {
            std::unique_ptr<imagecpp::Image> image = request_image(request, response);
            if (!image) {
                return;
            }
            const std::lock_guard<std::mutex> lock(model_mutex_);
            const imagecpp_model_options load_options = model_options(config_, config_.clip_model_path);
            imagecpp::Model model(runtime_, "image.embed.clip", load_options);
            const imagecpp::EmbeddingResult result = imagecpp::embed_image(model, *image);
            detail::set_json(response, 200,
                             {{"dimensions", result.size()},
                              {"embedding", std::vector<float>(result.data(), result.data() + result.size())}});
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    void handle_embed_text(const httplib::Request &request, httplib::Response &response) {
        if (!require_model(response, config_.clip_model_path, "CLIP")) {
            return;
        }
        try {
            const std::optional<std::string> text = body_string_value(request, "text");
            if (!text || text->empty()) {
                throw std::invalid_argument("text embedding requires non-empty text");
            }
            const std::lock_guard<std::mutex> lock(model_mutex_);
            const imagecpp_model_options load_options = model_options(config_, config_.clip_model_path);
            imagecpp::Model model(runtime_, "image.embed.clip", load_options);
            const imagecpp::EmbeddingResult result = imagecpp::embed_text(model, *text);
            detail::set_json(response, 200,
                             {{"dimensions", result.size()},
                              {"embedding", std::vector<float>(result.data(), result.data() + result.size())}});
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    void handle_classify(const httplib::Request &request, httplib::Response &response) {
        if (!require_model(response, config_.clip_model_path, "CLIP")) {
            return;
        }
        try {
            std::unique_ptr<imagecpp::Image> image = request_image(request, response);
            if (!image) {
                return;
            }
            const std::vector<std::string> labels = classification_labels(request);
            const std::lock_guard<std::mutex> lock(model_mutex_);
            const imagecpp_model_options load_options = model_options(config_, config_.clip_model_path);
            imagecpp::Model model(runtime_, "image.classify.clip", load_options);
            const imagecpp::ClassificationResult result = imagecpp::classify(model, *image, labels);
            detail::Json classifications = detail::Json::array();
            for (size_t index = 0; index < result.size(); ++index) {
                const imagecpp::ClassificationInfo item = result.at(index);
                classifications.push_back(
                    {{"label_index", item.label_index}, {"label", item.label}, {"score", item.score}});
            }
            detail::set_json(response, 200, {{"classifications", std::move(classifications)}});
        } catch (const imagecpp::Error &error) {
            detail::set_library_error(response, error);
        } catch (const std::invalid_argument &error) {
            detail::set_error(response, 400, "invalid_request", error.what());
        } catch (const std::exception &error) {
            detail::set_error(response, 500, "internal_error", error.what());
        }
    }

    imagecpp::Runtime &runtime_;
    const HttpServerConfig &config_;
    std::mutex &model_mutex_;
};

OperationApi::OperationApi(imagecpp::Runtime &runtime, const HttpServerConfig &config, std::mutex &model_mutex)
    : implementation_(std::make_unique<Impl>(runtime, config, model_mutex)) {}

OperationApi::~OperationApi() = default;

void OperationApi::register_routes(httplib::Server &server) { implementation_->register_routes(server); }

} // namespace imagecpp::server
