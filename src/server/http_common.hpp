#ifndef IMAGECPP_SERVER_HTTP_COMMON_HPP
#define IMAGECPP_SERVER_HTTP_COMMON_HPP

#include "httplib.h"
#include "imagecpp/imagecpp.hpp"
#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace imagecpp::server::detail {

using Json = nlohmann::json;

void set_json(httplib::Response &response, int status, const Json &body);
void set_error(httplib::Response &response, int status, const std::string &code, const std::string &message);
void set_library_error(httplib::Response &response, const imagecpp::Error &error);
void set_invalid_image(httplib::Response &response, const imagecpp::Error &error);

const char *status_code_name(imagecpp_status status);
int error_status(const imagecpp::Error &error);

std::optional<std::string> request_value(const httplib::Request &request, const std::string &name);
std::string request_file_bytes(const httplib::Request &request, const std::string &name, bool raw_body = false);
imagecpp::Image decode_request_image(const httplib::Request &request, const std::string &name = "image",
                                     bool raw_body = true);

uint32_t parse_uint32(const std::string &value, const char *name, bool allow_zero = false);
int32_t parse_int32(const std::string &value, const char *name, bool allow_zero = false);
int64_t parse_int64(const std::string &value, const char *name);
float parse_float(const std::string &value, const char *name);
bool parse_bool(const std::string &value, const char *name);

imagecpp_file_format response_format(const httplib::Request &request);
void set_image(httplib::Response &response, const imagecpp_const_image_view &image,
               imagecpp_file_format format = IMAGECPP_FILE_FORMAT_PNG, int quality = 90, bool lossless = false);
void set_image(httplib::Response &response, const imagecpp::Image &image,
               imagecpp_file_format format = IMAGECPP_FILE_FORMAT_PNG, int quality = 90, bool lossless = false);
const char *image_mime_type(imagecpp_file_format format);
std::string base64_encode(const void *data, size_t size);
Json encoded_image_json(const imagecpp_const_image_view &image, imagecpp_file_format format = IMAGECPP_FILE_FORMAT_PNG);

} // namespace imagecpp::server::detail

#endif
