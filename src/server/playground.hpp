#ifndef IMAGECPP_SERVER_PLAYGROUND_HPP
#define IMAGECPP_SERVER_PLAYGROUND_HPP

#include <string_view>

namespace imagecpp::server {

std::string_view playground_html() noexcept;
std::string_view playground_css() noexcept;
std::string_view playground_javascript() noexcept;

} // namespace imagecpp::server

#endif
