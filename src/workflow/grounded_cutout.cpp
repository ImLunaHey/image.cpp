#include "imagecpp/imagecpp.h"

#include "core/status.hpp"
#include "workflow/cutout_internal.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <utility>
#include <vector>

struct imagecpp_grounded_cutout_result {
    imagecpp::workflow::CutoutData cutout;
    size_t matched_detection_count = 0;
    size_t selected_detection_count = 0;
    float best_score = 0.0F;
    float best_iou_score = 0.0F;
};

namespace {

using DetectionResult = std::unique_ptr<imagecpp_detection_result, decltype(&imagecpp_detection_result_destroy)>;

imagecpp_status validate_options(const imagecpp_grounded_cutout_options *options, const imagecpp_model *upscaler_model,
                                 imagecpp_error *error) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_grounded_cutout_options)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "grounded cutout options are null or too small");
    }
    if (options->selection != IMAGECPP_GROUNDED_CUTOUT_BEST && options->selection != IMAGECPP_GROUNDED_CUTOUT_ALL) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "unknown grounded cutout selection mode");
    }
    if (options->crop_to_mask != 0 && options->crop_to_mask != 1) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "crop-to-mask must be zero or one");
    }
    if (options->upscale_factor == 0) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "grounded cutout upscale factor cannot be zero");
    }
    if (options->upscale_factor > 1 && upscaler_model == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "an upscaler model is required when the grounded cutout scale is greater than one");
    }
    return IMAGECPP_STATUS_OK;
}

} // namespace

extern "C" {

void imagecpp_grounded_cutout_options_init(imagecpp_grounded_cutout_options *options) {
    if (options != nullptr) {
        *options = {};
        options->struct_size = sizeof(imagecpp_grounded_cutout_options);
        imagecpp_detect_options_init(&options->detect);
        options->selection = IMAGECPP_GROUNDED_CUTOUT_BEST;
        options->crop_to_mask = 1;
        options->upscale_factor = 1;
    }
}

imagecpp_status imagecpp_grounded_cutout(imagecpp_session *detect_session, const imagecpp_model *upscaler_model,
                                         const imagecpp_const_image_view *image,
                                         const imagecpp_grounded_cutout_options *options,
                                         imagecpp_grounded_cutout_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output grounded cutout pointer is null");
    }
    *output = nullptr;
    imagecpp_status status = validate_options(options, upscaler_model, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    status = imagecpp_validate_const_image_view(image, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    status = imagecpp::workflow::validate_cutout_source(*image, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }

    try {
        status = imagecpp_session_set_image(detect_session, image, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        imagecpp_detection_result *detections_raw = nullptr;
        status = imagecpp_detect(detect_session, &options->detect, &detections_raw, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        DetectionResult detections(detections_raw, &imagecpp_detection_result_destroy);
        const size_t matched_count = imagecpp_detection_result_count(detections.get());
        if (matched_count == 0) {
            return imagecpp::core::fail(error, IMAGECPP_STATUS_MODEL_ERROR,
                                        "grounded cutout detection returned no matches");
        }
        const size_t selected_count =
            options->selection == IMAGECPP_GROUNDED_CUTOUT_ALL ? matched_count : static_cast<size_t>(1);

        std::vector<uint8_t> combined_mask;
        imagecpp_detection_info best{};
        best.struct_size = sizeof(best);
        for (size_t index = 0; index < selected_count; ++index) {
            imagecpp_detection_info detection{};
            detection.struct_size = sizeof(detection);
            status = imagecpp_detection_result_info(detections.get(), index, &detection, error);
            if (status != IMAGECPP_STATUS_OK) {
                return status;
            }
            if (index == 0) {
                best = detection;
            }
            std::vector<uint8_t> mask;
            status = imagecpp::workflow::copy_gray_mask(detection.mask, image->width, image->height, mask, error);
            if (status != IMAGECPP_STATUS_OK) {
                return status;
            }
            if (combined_mask.empty()) {
                combined_mask = std::move(mask);
            } else {
                std::transform(combined_mask.begin(), combined_mask.end(), mask.begin(), combined_mask.begin(),
                               [](uint8_t left, uint8_t right) { return std::max(left, right); });
            }
        }

        auto result = std::make_unique<imagecpp_grounded_cutout_result>();
        status = imagecpp::workflow::compose_cutout(*image, std::move(combined_mask), options->crop_to_mask,
                                                    options->padding, upscaler_model, options->upscale_factor,
                                                    result->cutout, error);
        if (status != IMAGECPP_STATUS_OK) {
            return status;
        }
        result->matched_detection_count = matched_count;
        result->selected_detection_count = selected_count;
        result->best_score = best.score;
        result->best_iou_score = best.iou_score;
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::bad_alloc &) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "grounded cutout workflow allocation failed");
    } catch (const std::exception &exception) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, exception.what());
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected grounded cutout workflow failure");
    }
}

imagecpp_status imagecpp_grounded_cutout_result_info(const imagecpp_grounded_cutout_result *result,
                                                     imagecpp_grounded_cutout_info *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_grounded_cutout_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "grounded cutout result or output info is null or too small");
    }
    *output = {
        sizeof(imagecpp_grounded_cutout_info),
        {sizeof(imagecpp_const_image_view), result->cutout.image.data(), result->cutout.image.size(),
         result->cutout.width, result->cutout.height, static_cast<size_t>(result->cutout.width) * 4,
         IMAGECPP_PIXEL_FORMAT_RGBA_U8, result->cutout.color_space},
        {sizeof(imagecpp_const_image_view), result->cutout.mask.data(), result->cutout.mask.size(),
         result->cutout.width, result->cutout.height, result->cutout.width, IMAGECPP_PIXEL_FORMAT_GRAY_U8,
         IMAGECPP_COLOR_SPACE_UNKNOWN},
        result->cutout.source_box,
        result->matched_detection_count,
        result->selected_detection_count,
        result->best_score,
        result->best_iou_score,
    };
    return imagecpp::core::succeed(error);
}

void imagecpp_grounded_cutout_result_destroy(imagecpp_grounded_cutout_result *result) { delete result; }

} // extern "C"
