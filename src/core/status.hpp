#ifndef IMAGECPP_CORE_STATUS_HPP
#define IMAGECPP_CORE_STATUS_HPP

#include "imagecpp/imagecpp.h"

#include <string_view>

namespace imagecpp::core {

imagecpp_status fail(imagecpp_error *error, imagecpp_status status, std::string_view message) noexcept;
imagecpp_status succeed(imagecpp_error *error) noexcept;

} // namespace imagecpp::core

#endif
