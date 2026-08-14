#include "imagecpp/imagecpp.h"

extern "C" {

uint32_t imagecpp_version(void) {
    return (IMAGECPP_VERSION_MAJOR << 24U) | (IMAGECPP_VERSION_MINOR << 16U) | IMAGECPP_VERSION_PATCH;
}

const char *imagecpp_version_string(void) { return "0.1.0-dev"; }

} // extern "C"
