#include "image/layout.hpp"

#include "core/status.hpp"

#include <limits>

namespace imagecpp::detail {
namespace {

bool multiply_overflows(size_t left, size_t right) noexcept {
    return right != 0 && left > std::numeric_limits<size_t>::max() / right;
}

} // namespace

imagecpp_status compute_layout(uint32_t width, uint32_t height, size_t row_stride, imagecpp_pixel_format format,
                               ImageLayout &output, imagecpp_error *error) noexcept {
    output = {};
    if (width == 0 || height == 0) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image dimensions must be non-zero");
    }

    output.channels = imagecpp_pixel_format_channels(format);
    output.bytes_per_channel = imagecpp_pixel_format_bytes_per_channel(format);
    output.bytes_per_pixel = imagecpp_pixel_format_bytes_per_pixel(format);
    if (output.bytes_per_pixel == 0) {
        return core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "unknown or unsupported pixel format");
    }
    if (multiply_overflows(static_cast<size_t>(width), output.bytes_per_pixel)) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "image row size overflows size_t");
    }
    output.row_bytes = static_cast<size_t>(width) * output.bytes_per_pixel;
    output.row_stride = row_stride == 0 ? output.row_bytes : row_stride;
    if (output.row_stride < output.row_bytes) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "row stride is smaller than the packed row size");
    }
    if (multiply_overflows(output.row_stride, static_cast<size_t>(height - 1))) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "image byte size overflows size_t");
    }
    const size_t preceding_rows = output.row_stride * static_cast<size_t>(height - 1);
    if (preceding_rows > std::numeric_limits<size_t>::max() - output.row_bytes) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "image byte size overflows size_t");
    }
    output.required_bytes = preceding_rows + output.row_bytes;
    return core::succeed(error);
}

imagecpp_status validate_const_view(const imagecpp_const_image_view *view, ImageLayout &output,
                                    imagecpp_error *error) noexcept {
    if (view == nullptr) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image view is null");
    }
    if (view->struct_size < sizeof(imagecpp_const_image_view)) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image view struct_size is too small");
    }
    const imagecpp_status status =
        compute_layout(view->width, view->height, view->row_stride, view->pixel_format, output, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    if (view->data == nullptr) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image data is null");
    }
    if (view->data_size < output.required_bytes) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image data is smaller than its declared layout");
    }
    return core::succeed(error);
}

imagecpp_status validate_mutable_view(const imagecpp_image_view *view, ImageLayout &output,
                                      imagecpp_error *error) noexcept {
    if (view == nullptr) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image view is null");
    }
    if (view->struct_size < sizeof(imagecpp_image_view)) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image view struct_size is too small");
    }
    const imagecpp_const_image_view const_view{
        sizeof(imagecpp_const_image_view),
        view->data,
        view->data_size,
        view->width,
        view->height,
        view->row_stride,
        view->pixel_format,
        view->color_space,
    };
    return validate_const_view(&const_view, output, error);
}

bool ranges_overlap(const void *first, size_t first_size, const void *second, size_t second_size) noexcept {
    if (first == nullptr || second == nullptr || first_size == 0 || second_size == 0) {
        return false;
    }
    const auto first_begin = reinterpret_cast<uintptr_t>(first);
    const auto second_begin = reinterpret_cast<uintptr_t>(second);
    const auto first_end = first_begin + first_size;
    const auto second_end = second_begin + second_size;
    if (first_end < first_begin || second_end < second_begin) {
        return true;
    }
    return first_begin < second_end && second_begin < first_end;
}

} // namespace imagecpp::detail

extern "C" {

size_t imagecpp_pixel_format_channels(imagecpp_pixel_format format) {
    switch (format) {
    case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
    case IMAGECPP_PIXEL_FORMAT_GRAY_F32:
        return 1;
    case IMAGECPP_PIXEL_FORMAT_RGB_U8:
    case IMAGECPP_PIXEL_FORMAT_RGB_F32:
        return 3;
    case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
    case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
    case IMAGECPP_PIXEL_FORMAT_RGBA_F32:
        return 4;
    case IMAGECPP_PIXEL_FORMAT_UNKNOWN:
        return 0;
    }
    return 0;
}

size_t imagecpp_pixel_format_bytes_per_channel(imagecpp_pixel_format format) {
    switch (format) {
    case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
    case IMAGECPP_PIXEL_FORMAT_RGB_U8:
    case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
    case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
        return 1;
    case IMAGECPP_PIXEL_FORMAT_GRAY_F32:
    case IMAGECPP_PIXEL_FORMAT_RGB_F32:
    case IMAGECPP_PIXEL_FORMAT_RGBA_F32:
        return sizeof(float);
    case IMAGECPP_PIXEL_FORMAT_UNKNOWN:
        return 0;
    }
    return 0;
}

size_t imagecpp_pixel_format_bytes_per_pixel(imagecpp_pixel_format format) {
    return imagecpp_pixel_format_channels(format) * imagecpp_pixel_format_bytes_per_channel(format);
}

imagecpp_status imagecpp_validate_const_image_view(const imagecpp_const_image_view *view, imagecpp_error *error) {
    imagecpp::detail::ImageLayout layout;
    return imagecpp::detail::validate_const_view(view, layout, error);
}

imagecpp_status imagecpp_validate_image_view(const imagecpp_image_view *view, imagecpp_error *error) {
    imagecpp::detail::ImageLayout layout;
    return imagecpp::detail::validate_mutable_view(view, layout, error);
}

} // extern "C"
