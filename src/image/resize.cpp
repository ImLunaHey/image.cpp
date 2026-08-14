#include "imagecpp/imagecpp.h"

#include "core/status.hpp"
#include "image/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace imagecpp::detail {
namespace {

struct AxisSample {
    uint32_t low;
    uint32_t high;
    float fraction;
};

AxisSample sample_axis(uint32_t destination_index, uint32_t source_size, uint32_t destination_size) {
    const float source_coordinate = (static_cast<float>(destination_index) + 0.5F) * static_cast<float>(source_size) /
                                        static_cast<float>(destination_size) -
                                    0.5F;
    const float clamped = std::clamp(source_coordinate, 0.0F, static_cast<float>(source_size - 1));
    const uint32_t low = static_cast<uint32_t>(std::floor(clamped));
    const uint32_t high = std::min(low + 1, source_size - 1);
    return {low, high, clamped - static_cast<float>(low)};
}

float read_channel(const uint8_t *pixel, size_t channel, size_t bytes_per_channel) {
    if (bytes_per_channel == 1) {
        return static_cast<float>(pixel[channel]);
    }
    float value = 0.0F;
    std::memcpy(&value, pixel + channel * sizeof(float), sizeof(value));
    return value;
}

void write_channel(uint8_t *pixel, size_t channel, size_t bytes_per_channel, float value) {
    if (bytes_per_channel == 1) {
        const long rounded = std::lround(std::clamp(value, 0.0F, 255.0F));
        pixel[channel] = static_cast<uint8_t>(rounded);
        return;
    }
    std::memcpy(pixel + channel * sizeof(float), &value, sizeof(value));
}

void resize_nearest(const imagecpp_const_image_view &source, const ImageLayout &source_layout,
                    const imagecpp_image_view &destination, const ImageLayout &destination_layout) {
    const auto *source_bytes = static_cast<const uint8_t *>(source.data);
    auto *destination_bytes = static_cast<uint8_t *>(destination.data);
    for (uint32_t y = 0; y < destination.height; ++y) {
        const uint32_t source_y = std::min(
            static_cast<uint32_t>((static_cast<uint64_t>(y) * source.height) / destination.height), source.height - 1);
        const uint8_t *source_row = source_bytes + static_cast<size_t>(source_y) * source_layout.row_stride;
        uint8_t *destination_row = destination_bytes + static_cast<size_t>(y) * destination_layout.row_stride;
        for (uint32_t x = 0; x < destination.width; ++x) {
            const uint32_t source_x = std::min(
                static_cast<uint32_t>((static_cast<uint64_t>(x) * source.width) / destination.width), source.width - 1);
            std::memcpy(destination_row + static_cast<size_t>(x) * destination_layout.bytes_per_pixel,
                        source_row + static_cast<size_t>(source_x) * source_layout.bytes_per_pixel,
                        source_layout.bytes_per_pixel);
        }
    }
}

void resize_bilinear(const imagecpp_const_image_view &source, const ImageLayout &source_layout,
                     const imagecpp_image_view &destination, const ImageLayout &destination_layout) {
    const auto *source_bytes = static_cast<const uint8_t *>(source.data);
    auto *destination_bytes = static_cast<uint8_t *>(destination.data);
    for (uint32_t y = 0; y < destination.height; ++y) {
        const AxisSample sy = sample_axis(y, source.height, destination.height);
        const uint8_t *row0 = source_bytes + static_cast<size_t>(sy.low) * source_layout.row_stride;
        const uint8_t *row1 = source_bytes + static_cast<size_t>(sy.high) * source_layout.row_stride;
        uint8_t *destination_row = destination_bytes + static_cast<size_t>(y) * destination_layout.row_stride;
        for (uint32_t x = 0; x < destination.width; ++x) {
            const AxisSample sx = sample_axis(x, source.width, destination.width);
            const uint8_t *p00 = row0 + static_cast<size_t>(sx.low) * source_layout.bytes_per_pixel;
            const uint8_t *p10 = row0 + static_cast<size_t>(sx.high) * source_layout.bytes_per_pixel;
            const uint8_t *p01 = row1 + static_cast<size_t>(sx.low) * source_layout.bytes_per_pixel;
            const uint8_t *p11 = row1 + static_cast<size_t>(sx.high) * source_layout.bytes_per_pixel;
            uint8_t *output = destination_row + static_cast<size_t>(x) * destination_layout.bytes_per_pixel;
            for (size_t channel = 0; channel < source_layout.channels; ++channel) {
                const float top = read_channel(p00, channel, source_layout.bytes_per_channel) * (1.0F - sx.fraction) +
                                  read_channel(p10, channel, source_layout.bytes_per_channel) * sx.fraction;
                const float bottom =
                    read_channel(p01, channel, source_layout.bytes_per_channel) * (1.0F - sx.fraction) +
                    read_channel(p11, channel, source_layout.bytes_per_channel) * sx.fraction;
                const float value = top * (1.0F - sy.fraction) + bottom * sy.fraction;
                write_channel(output, channel, destination_layout.bytes_per_channel, value);
            }
        }
    }
}

} // namespace
} // namespace imagecpp::detail

extern "C" {

imagecpp_status imagecpp_resize(const imagecpp_const_image_view *source, const imagecpp_image_view *destination,
                                imagecpp_resize_filter filter, imagecpp_error *error) {
    imagecpp::detail::ImageLayout source_layout;
    imagecpp_status status = imagecpp::detail::validate_const_view(source, source_layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    imagecpp::detail::ImageLayout destination_layout;
    status = imagecpp::detail::validate_mutable_view(destination, destination_layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    if (source->pixel_format != destination->pixel_format) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "source and destination pixel formats must match");
    }
    if (source->color_space != destination->color_space) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "source and destination color spaces must match");
    }
    if (imagecpp::detail::ranges_overlap(source->data, source_layout.required_bytes, destination->data,
                                         destination_layout.required_bytes)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "source and destination storage must not overlap");
    }

    switch (filter) {
    case IMAGECPP_RESIZE_NEAREST:
        imagecpp::detail::resize_nearest(*source, source_layout, *destination, destination_layout);
        return imagecpp::core::succeed(error);
    case IMAGECPP_RESIZE_BILINEAR:
        imagecpp::detail::resize_bilinear(*source, source_layout, *destination, destination_layout);
        return imagecpp::core::succeed(error);
    }
    return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "unknown resize filter");
}

} // extern "C"
