#ifndef IMAGECPP_IMAGE_LAYOUT_HPP
#define IMAGECPP_IMAGE_LAYOUT_HPP

#include "imagecpp/imagecpp.h"

#include <cstddef>
#include <cstdint>

namespace imagecpp::detail {

struct ImageLayout {
    size_t channels = 0;
    size_t bytes_per_channel = 0;
    size_t bytes_per_pixel = 0;
    size_t row_bytes = 0;
    size_t row_stride = 0;
    size_t required_bytes = 0;
};

imagecpp_status compute_layout(uint32_t width, uint32_t height, size_t row_stride, imagecpp_pixel_format format,
                               ImageLayout &output, imagecpp_error *error) noexcept;

imagecpp_status validate_const_view(const imagecpp_const_image_view *view, ImageLayout &output,
                                    imagecpp_error *error) noexcept;

imagecpp_status validate_mutable_view(const imagecpp_image_view *view, ImageLayout &output,
                                      imagecpp_error *error) noexcept;

bool ranges_overlap(const void *first, size_t first_size, const void *second, size_t second_size) noexcept;

} // namespace imagecpp::detail

#endif
