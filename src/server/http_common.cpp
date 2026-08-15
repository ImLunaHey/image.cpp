#include "server/http_common.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace imagecpp::server::detail {

void set_json(httplib::Response &response, int status, const Json &body) {
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

void set_error(httplib::Response &response, int status, const std::string &code, const std::string &message) {
    set_json(response, status, {{"error", {{"code", code}, {"message", message}}}});
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

void set_library_error(httplib::Response &response, const imagecpp::Error &error) {
    set_error(response, error_status(error), status_code_name(error.status()), error.what());
}

void set_invalid_image(httplib::Response &response, const imagecpp::Error &error) {
    if (error.status() == IMAGECPP_STATUS_OUT_OF_MEMORY) {
        set_library_error(response, error);
    } else {
        set_error(response, 400, "invalid_image", error.what());
    }
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

std::string request_file_bytes(const httplib::Request &request, const std::string &name, bool raw_body) {
    if (request.is_multipart_form_data()) {
        if (!request.form.has_file(name)) {
            throw std::invalid_argument("multipart request is missing the " + name + " file field");
        }
        return request.form.get_file(name).content;
    }
    if (!raw_body) {
        throw std::invalid_argument("request must use multipart/form-data");
    }
    if (request.body.empty()) {
        throw std::invalid_argument("request image body is empty");
    }
    return request.body;
}

imagecpp::Image decode_request_image(const httplib::Request &request, const std::string &name, bool raw_body) {
    const std::string bytes = request_file_bytes(request, name, raw_body);
    return imagecpp::decode(bytes.data(), bytes.size());
}

uint32_t parse_uint32(const std::string &value, const char *name, bool allow_zero) {
    uint64_t parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc() || conversion.ptr != value.data() + value.size() ||
        (!allow_zero && parsed == 0) || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<uint32_t>(parsed);
}

int32_t parse_int32(const std::string &value, const char *name, bool allow_zero) {
    const uint32_t parsed = parse_uint32(value, name, allow_zero);
    if (parsed > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        throw std::invalid_argument(std::string(name) + " is too large");
    }
    return static_cast<int32_t>(parsed);
}

int64_t parse_int64(const std::string &value, const char *name) {
    int64_t parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc() || conversion.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return parsed;
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

imagecpp_file_format response_format(const httplib::Request &request) {
    const std::string value = request_value(request, "format").value_or("png");
    if (value == "png") {
        return IMAGECPP_FILE_FORMAT_PNG;
    }
    if (value == "jpeg" || value == "jpg") {
        return IMAGECPP_FILE_FORMAT_JPEG;
    }
    if (value == "webp") {
        return IMAGECPP_FILE_FORMAT_WEBP;
    }
    if (value == "bmp") {
        return IMAGECPP_FILE_FORMAT_BMP;
    }
    if (value == "tga") {
        return IMAGECPP_FILE_FORMAT_TGA;
    }
    throw std::invalid_argument("format must be png, jpeg, webp, bmp, or tga");
}

const char *image_mime_type(imagecpp_file_format format) {
    switch (format) {
    case IMAGECPP_FILE_FORMAT_PNG:
        return "image/png";
    case IMAGECPP_FILE_FORMAT_JPEG:
        return "image/jpeg";
    case IMAGECPP_FILE_FORMAT_WEBP:
        return "image/webp";
    case IMAGECPP_FILE_FORMAT_BMP:
        return "image/bmp";
    case IMAGECPP_FILE_FORMAT_TGA:
        return "image/x-tga";
    case IMAGECPP_FILE_FORMAT_AUTO:
        break;
    }
    return "application/octet-stream";
}

std::string base64_encode(const void *data, size_t size) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::string output;
    output.reserve(((size + 2U) / 3U) * 4U);
    for (size_t offset = 0; offset < size; offset += 3U) {
        const uint32_t first = bytes[offset];
        const uint32_t second = offset + 1U < size ? bytes[offset + 1U] : 0U;
        const uint32_t third = offset + 2U < size ? bytes[offset + 2U] : 0U;
        const uint32_t packed = (first << 16U) | (second << 8U) | third;
        output.push_back(alphabet[(packed >> 18U) & 0x3FU]);
        output.push_back(alphabet[(packed >> 12U) & 0x3FU]);
        output.push_back(offset + 1U < size ? alphabet[(packed >> 6U) & 0x3FU] : '=');
        output.push_back(offset + 2U < size ? alphabet[packed & 0x3FU] : '=');
    }
    return output;
}

Json encoded_image_json(const imagecpp_const_image_view &image, imagecpp_file_format format) {
    const imagecpp::Blob encoded = imagecpp::encode(image, format);
    return {{"format", format == IMAGECPP_FILE_FORMAT_PNG ? "png" : "encoded"},
            {"mime_type", image_mime_type(format)},
            {"width", image.width},
            {"height", image.height},
            {"base64", base64_encode(encoded.data(), encoded.size())}};
}

void set_image(httplib::Response &response, const imagecpp_const_image_view &image, imagecpp_file_format format,
               int quality, bool lossless) {
    imagecpp_encode_options options{};
    imagecpp_encode_options_init(&options);
    options.quality = quality;
    options.lossless = lossless ? 1 : 0;
    const imagecpp::Blob encoded = imagecpp::encode(image, format, &options);
    response.status = 200;
    response.set_content(std::string(static_cast<const char *>(encoded.data()), encoded.size()), image_mime_type(format));
}

void set_image(httplib::Response &response, const imagecpp::Image &image, imagecpp_file_format format, int quality,
               bool lossless) {
    set_image(response, image.view(), format, quality, lossless);
}

} // namespace imagecpp::server::detail
