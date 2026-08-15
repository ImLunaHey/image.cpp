#include "imagecpp/imagecpp.h"

#include "core/status.hpp"
#include "workflow/cutout_internal.hpp"

#include <cstdint>
#include <memory>
#include <new>
#include <vector>

struct imagecpp_cutout_result {
    imagecpp::workflow::CutoutData cutout;
    size_t selected_mask_index = 0;
    float score = 0.0F;
    float iou_score = 0.0F;
};

namespace {

using SegmentResult = std::unique_ptr<imagecpp_segment_result, decltype(&imagecpp_segment_result_destroy)>;

} // namespace

extern "C" {

void imagecpp_cutout_options_init(imagecpp_cutout_options *options) {
    if (options != nullptr) {
        *options = {};
        options->struct_size = sizeof(imagecpp_cutout_options);
        imagecpp_segment_options_init(&options->segment);
        options->crop_to_mask = 1;
        options->upscale_factor = 1;
    }
}

imagecpp_status imagecpp_cutout(imagecpp_session *segment_session, const imagecpp_model *upscaler_model,
                                const imagecpp_const_image_view *image, const imagecpp_cutout_options *options,
                                imagecpp_cutout_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output cutout pointer is null");
    }
    *output = nullptr;
    if (options == nullptr || options->struct_size < sizeof(imagecpp_cutout_options)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "cutout options are null or too small");
    }
    if (options->crop_to_mask != 0 && options->crop_to_mask != 1) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "crop-to-mask must be zero or one");
    }
    if (options->upscale_factor == 0) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "cutout upscale factor cannot be zero");
    }
    if (options->upscale_factor > 1 && upscaler_model == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "an upscaler model is required when the cutout scale is greater than one");
    }
    imagecpp_status status = imagecpp_validate_const_image_view(image, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    status = imagecpp::workflow::validate_cutout_source(*image, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        status = imagecpp_session_set_image(segment_session, image, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        imagecpp_segment_result *segment_raw = nullptr;
        status = imagecpp_segment(segment_session, &options->segment, &segment_raw, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        SegmentResult segments(segment_raw, &imagecpp_segment_result_destroy);
        const size_t segment_count = imagecpp_segment_result_count(segments.get());
        if (segment_count == 0) {
            return imagecpp::core::fail(error, IMAGECPP_STATUS_MODEL_ERROR, "cutout segmentation returned no masks");
        }

        imagecpp_segment_info selected{};
        selected.struct_size = sizeof(selected);
        size_t selected_index = 0;
        status = imagecpp_segment_result_info(segments.get(), 0, &selected, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        for (size_t index = 1; index < segment_count; ++index) {
            imagecpp_segment_info candidate{};
            candidate.struct_size = sizeof(candidate);
            status = imagecpp_segment_result_info(segments.get(), index, &candidate, error);
            if (status != IMAGECPP_STATUS_OK) {
                return status;
            }
            if (candidate.iou_score > selected.iou_score) {
                selected = candidate;
                selected_index = index;
            }
        }
        std::vector<uint8_t> full_mask;
        status = imagecpp::workflow::copy_gray_mask(selected.mask, image->width, image->height, full_mask, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }

        auto result = std::make_unique<imagecpp_cutout_result>();
        status =
            imagecpp::workflow::compose_cutout(*image, std::move(full_mask), options->crop_to_mask, options->padding,
                                               upscaler_model, options->upscale_factor, result->cutout, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        result->selected_mask_index = selected_index;
        result->score = selected.score;
        result->iou_score = selected.iou_score;
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::bad_alloc &) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "cutout workflow allocation failed");
    } catch (const std::exception &exception) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, exception.what());
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected cutout workflow failure");
    }
}

imagecpp_status imagecpp_cutout_result_info(const imagecpp_cutout_result *result, imagecpp_cutout_info *output,
                                            imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_cutout_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "cutout result or output info is null or too small");
    }
    *output = {
        sizeof(imagecpp_cutout_info),
        {sizeof(imagecpp_const_image_view), result->cutout.image.data(), result->cutout.image.size(),
         result->cutout.width, result->cutout.height, static_cast<size_t>(result->cutout.width) * 4,
         IMAGECPP_PIXEL_FORMAT_RGBA_U8, result->cutout.color_space},
        {sizeof(imagecpp_const_image_view), result->cutout.mask.data(), result->cutout.mask.size(),
         result->cutout.width, result->cutout.height, result->cutout.width, IMAGECPP_PIXEL_FORMAT_GRAY_U8,
         IMAGECPP_COLOR_SPACE_UNKNOWN},
        result->cutout.source_box,
        result->selected_mask_index,
        result->score,
        result->iou_score,
    };
    return imagecpp::core::succeed(error);
}

void imagecpp_cutout_result_destroy(imagecpp_cutout_result *result) { delete result; }

} // extern "C"
