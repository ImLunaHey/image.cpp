#include "workflow/cutout_internal.hpp"

#include "core/status.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace imagecpp::workflow {
namespace {

using ImageResult = std::unique_ptr<imagecpp_image_result, decltype(&imagecpp_image_result_destroy)>;

bool checked_area(uint32_t width, uint32_t height, size_t channels, size_t &result) {
    if (height != 0 && width > std::numeric_limits<size_t>::max() / height) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(width) * height;
    if (channels != 0 && pixels > std::numeric_limits<size_t>::max() / channels) {
        return false;
    }
    result = pixels * channels;
    return true;
}

size_t bytes_per_pixel(imagecpp_pixel_format format) {
    switch (format) {
    case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
        return 1;
    case IMAGECPP_PIXEL_FORMAT_RGB_U8:
        return 3;
    case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
    case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
        return 4;
    default:
        return 0;
    }
}

struct Crop {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool mask_bounds(const std::vector<uint8_t> &mask, uint32_t width, uint32_t height, uint32_t padding, Crop &crop) {
    uint32_t min_x = width;
    uint32_t min_y = height;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    bool found = false;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (mask[static_cast<size_t>(y) * width + x] < 128) {
                continue;
            }
            found = true;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    if (!found) {
        return false;
    }

    crop.x = min_x > padding ? min_x - padding : 0;
    crop.y = min_y > padding ? min_y - padding : 0;
    const uint64_t padded_max_x = static_cast<uint64_t>(max_x) + padding + 1;
    const uint64_t padded_max_y = static_cast<uint64_t>(max_y) + padding + 1;
    const uint32_t end_x = static_cast<uint32_t>(std::min<uint64_t>(width, padded_max_x));
    const uint32_t end_y = static_cast<uint32_t>(std::min<uint64_t>(height, padded_max_y));
    crop.width = end_x - crop.x;
    crop.height = end_y - crop.y;
    return true;
}

std::vector<uint8_t> crop_mask(const std::vector<uint8_t> &mask, uint32_t source_width, const Crop &crop) {
    std::vector<uint8_t> output(static_cast<size_t>(crop.width) * crop.height);
    for (uint32_t row = 0; row < crop.height; ++row) {
        const uint8_t *source = mask.data() + static_cast<size_t>(crop.y + row) * source_width + crop.x;
        std::memcpy(output.data() + static_cast<size_t>(row) * crop.width, source, crop.width);
    }
    return output;
}

imagecpp_const_image_view cropped_view(const imagecpp_const_image_view &image, const Crop &crop) {
    const size_t pixel_size = bytes_per_pixel(image.pixel_format);
    const size_t offset = static_cast<size_t>(crop.y) * image.row_stride + static_cast<size_t>(crop.x) * pixel_size;
    return {
        sizeof(imagecpp_const_image_view),
        static_cast<const uint8_t *>(image.data) + offset,
        image.data_size - offset,
        crop.width,
        crop.height,
        image.row_stride,
        image.pixel_format,
        image.color_space,
    };
}

imagecpp_status resized_mask(const std::vector<uint8_t> &source, uint32_t source_width, uint32_t source_height,
                             uint32_t output_width, uint32_t output_height, std::vector<uint8_t> &output,
                             imagecpp_error *error) {
    size_t output_size = 0;
    if (!checked_area(output_width, output_height, 1, output_size)) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "workflow mask output is too large");
    }
    output.resize(output_size);
    const imagecpp_const_image_view source_view{
        sizeof(imagecpp_const_image_view),
        source.data(),
        source.size(),
        source_width,
        source_height,
        source_width,
        IMAGECPP_PIXEL_FORMAT_GRAY_U8,
        IMAGECPP_COLOR_SPACE_UNKNOWN,
    };
    const imagecpp_image_view output_view{
        sizeof(imagecpp_image_view),
        output.data(),
        output.size(),
        output_width,
        output_height,
        output_width,
        IMAGECPP_PIXEL_FORMAT_GRAY_U8,
        IMAGECPP_COLOR_SPACE_UNKNOWN,
    };
    return imagecpp_resize(&source_view, &output_view, IMAGECPP_RESIZE_BILINEAR, error);
}

imagecpp_status apply_mask(const imagecpp_const_image_view &source, const std::vector<uint8_t> &mask,
                           std::vector<uint8_t> &output, imagecpp_error *error) {
    const size_t source_pixel_size = bytes_per_pixel(source.pixel_format);
    if (source_pixel_size == 0) {
        return core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                          "cutout workflows support GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
    }
    size_t output_size = 0;
    if (!checked_area(source.width, source.height, 4, output_size) ||
        mask.size() != static_cast<size_t>(source.width) * source.height) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "cutout workflow output is too large");
    }
    output.resize(output_size);
    const auto *source_bytes = static_cast<const uint8_t *>(source.data);
    for (uint32_t row = 0; row < source.height; ++row) {
        const uint8_t *source_row = source_bytes + static_cast<size_t>(row) * source.row_stride;
        uint8_t *output_row = output.data() + static_cast<size_t>(row) * source.width * 4;
        const uint8_t *mask_row = mask.data() + static_cast<size_t>(row) * source.width;
        for (uint32_t column = 0; column < source.width; ++column) {
            const uint8_t *pixel = source_row + static_cast<size_t>(column) * source_pixel_size;
            uint8_t *destination = output_row + static_cast<size_t>(column) * 4;
            uint8_t original_alpha = 255;
            switch (source.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                destination[0] = destination[1] = destination[2] = pixel[0];
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8:
                std::memcpy(destination, pixel, 3);
                break;
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
                std::memcpy(destination, pixel, 3);
                original_alpha = pixel[3];
                break;
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
                destination[0] = pixel[2];
                destination[1] = pixel[1];
                destination[2] = pixel[0];
                original_alpha = pixel[3];
                break;
            default:
                break;
            }
            destination[3] =
                static_cast<uint8_t>((static_cast<unsigned>(original_alpha) * mask_row[column] + 127U) / 255U);
        }
    }
    return IMAGECPP_STATUS_OK;
}

} // namespace

imagecpp_status validate_cutout_source(const imagecpp_const_image_view &image, imagecpp_error *error) {
    if (bytes_per_pixel(image.pixel_format) == 0) {
        return core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                          "cutout workflows support GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
    }
    return IMAGECPP_STATUS_OK;
}

imagecpp_status copy_gray_mask(const imagecpp_const_image_view &view, uint32_t expected_width, uint32_t expected_height,
                               std::vector<uint8_t> &mask, imagecpp_error *error) {
    if (view.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_U8 || view.width != expected_width ||
        view.height != expected_height) {
        return core::fail(error, IMAGECPP_STATUS_MODEL_ERROR, "workflow requires a full-resolution GRAY_U8 mask");
    }
    size_t byte_count = 0;
    if (!checked_area(view.width, view.height, 1, byte_count)) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "workflow mask is too large");
    }
    mask.resize(byte_count);
    const auto *source = static_cast<const uint8_t *>(view.data);
    for (uint32_t row = 0; row < view.height; ++row) {
        std::memcpy(mask.data() + static_cast<size_t>(row) * view.width,
                    source + static_cast<size_t>(row) * view.row_stride, view.width);
    }
    return IMAGECPP_STATUS_OK;
}

imagecpp_status compose_cutout(const imagecpp_const_image_view &image, std::vector<uint8_t> full_mask, int crop_to_mask,
                               uint32_t padding, const imagecpp_model *upscaler_model, uint32_t upscale_factor,
                               CutoutData &output, imagecpp_error *error) {
    imagecpp_status status = validate_cutout_source(image, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    size_t mask_size = 0;
    if (!checked_area(image.width, image.height, 1, mask_size) || full_mask.size() != mask_size) {
        return core::fail(error, IMAGECPP_STATUS_MODEL_ERROR, "cutout mask dimensions do not match the source image");
    }

    Crop crop{0, 0, image.width, image.height};
    if (crop_to_mask != 0 && !mask_bounds(full_mask, image.width, image.height, padding, crop)) {
        return core::fail(error, IMAGECPP_STATUS_MODEL_ERROR, "cutout mask has no foreground pixels");
    }
    std::vector<uint8_t> working_mask = crop_mask(full_mask, image.width, crop);
    imagecpp_const_image_view working_image = cropped_view(image, crop);

    ImageResult upscaled(nullptr, &imagecpp_image_result_destroy);
    if (upscale_factor > 1) {
        imagecpp_image_result *upscaled_raw = nullptr;
        status = imagecpp_upscale(upscaler_model, &working_image, upscale_factor, &upscaled_raw, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        upscaled.reset(upscaled_raw);
        imagecpp_const_image_view upscaled_view{};
        upscaled_view.struct_size = sizeof(upscaled_view);
        status = imagecpp_image_result_view(upscaled.get(), 0, &upscaled_view, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        std::vector<uint8_t> scaled_mask;
        status = resized_mask(working_mask, crop.width, crop.height, upscaled_view.width, upscaled_view.height,
                              scaled_mask, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        working_mask = std::move(scaled_mask);
        working_image = upscaled_view;
    }

    output.width = working_image.width;
    output.height = working_image.height;
    output.color_space = working_image.color_space;
    output.mask = std::move(working_mask);
    output.source_box = {static_cast<float>(crop.x), static_cast<float>(crop.y),
                         static_cast<float>(crop.x + crop.width), static_cast<float>(crop.y + crop.height)};
    return apply_mask(working_image, output.mask, output.image, error);
}

} // namespace imagecpp::workflow
