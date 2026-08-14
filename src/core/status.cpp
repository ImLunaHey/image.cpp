#include "core/status.hpp"

#include <algorithm>
#include <cstring>

namespace imagecpp::core {

imagecpp_status fail(imagecpp_error *error, imagecpp_status status, std::string_view message) noexcept {
    if (error != nullptr) {
        error->code = status;
        const size_t length = std::min(message.size(), sizeof(error->message) - 1);
        std::memcpy(error->message, message.data(), length);
        error->message[length] = '\0';
    }
    return status;
}

imagecpp_status succeed(imagecpp_error *error) noexcept {
    imagecpp_error_clear(error);
    return IMAGECPP_STATUS_OK;
}

} // namespace imagecpp::core

extern "C" {

const char *imagecpp_status_string(imagecpp_status status) {
    switch (status) {
    case IMAGECPP_STATUS_OK:
        return "ok";
    case IMAGECPP_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case IMAGECPP_STATUS_OUT_OF_RANGE:
        return "out of range";
    case IMAGECPP_STATUS_UNSUPPORTED:
        return "unsupported";
    case IMAGECPP_STATUS_OUT_OF_MEMORY:
        return "out of memory";
    case IMAGECPP_STATUS_INTERNAL:
        return "internal error";
    }
    return "unknown status";
}

void imagecpp_error_clear(imagecpp_error *error) {
    if (error != nullptr) {
        error->code = IMAGECPP_STATUS_OK;
        error->message[0] = '\0';
    }
}

} // extern "C"
