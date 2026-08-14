#ifndef IMAGECPP_APP_PNM_HPP
#define IMAGECPP_APP_PNM_HPP

#include "imagecpp/imagecpp.hpp"

#include <string>

namespace imagecpp::app {

Image read_pnm(const std::string &filename);
void write_pnm(const std::string &filename, const imagecpp_const_image_view &image);

} // namespace imagecpp::app

#endif
