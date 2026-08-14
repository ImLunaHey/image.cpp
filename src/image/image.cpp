#include "imagecpp/imagecpp.h"

#include "core/status.hpp"
#include "image/layout.hpp"

#include <new>
#include <vector>

struct imagecpp_image {
    imagecpp_image_desc desc{};
    std::vector<uint8_t> bytes;
};

extern "C" {

imagecpp_status imagecpp_image_create(const imagecpp_image_desc *desc, imagecpp_image **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output image pointer is null");
    }
    *output = nullptr;
    if (desc == nullptr || desc->struct_size < sizeof(imagecpp_image_desc)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image description is null or too small");
    }

    imagecpp::detail::ImageLayout layout;
    const imagecpp_status layout_status = imagecpp::detail::compute_layout(desc->width, desc->height, desc->row_stride,
                                                                           desc->pixel_format, layout, error);
    if (layout_status != IMAGECPP_STATUS_OK) {
        return layout_status;
    }

    try {
        auto *image = new imagecpp_image;
        image->desc = *desc;
        image->desc.struct_size = sizeof(imagecpp_image_desc);
        image->desc.row_stride = layout.row_stride;
        image->bytes.resize(layout.required_bytes);
        *output = image;
        return imagecpp::core::succeed(error);
    } catch (const std::bad_alloc &) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "failed to allocate image storage");
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected failure while creating image");
    }
}

void imagecpp_image_destroy(imagecpp_image *image) { delete image; }

imagecpp_status imagecpp_image_get_view(imagecpp_image *image, imagecpp_image_view *output, imagecpp_error *error) {
    if (image == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_image_view)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "image or output view is null or too small");
    }
    *output = {
        sizeof(imagecpp_image_view), image->bytes.data(),    image->bytes.size(),      image->desc.width,
        image->desc.height,          image->desc.row_stride, image->desc.pixel_format, image->desc.color_space,
    };
    return imagecpp::core::succeed(error);
}

imagecpp_status imagecpp_image_get_const_view(const imagecpp_image *image, imagecpp_const_image_view *output,
                                              imagecpp_error *error) {
    if (image == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_const_image_view)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "image or output view is null or too small");
    }
    *output = {
        sizeof(imagecpp_const_image_view),
        image->bytes.data(),
        image->bytes.size(),
        image->desc.width,
        image->desc.height,
        image->desc.row_stride,
        image->desc.pixel_format,
        image->desc.color_space,
    };
    return imagecpp::core::succeed(error);
}

} // extern "C"
