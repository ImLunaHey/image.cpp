#include "imagecpp/imagecpp.h"

#include <tesseract/baseapi.h>

namespace imagecpp::detail {

static_assert(TESSERACT_MAJOR_VERSION == 5, "image.cpp requires Tesseract 5");

} // namespace imagecpp::detail
