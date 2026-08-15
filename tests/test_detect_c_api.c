#include "imagecpp/imagecpp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef IMAGECPP_TEST_SAM3_MODEL_PATH
#error "IMAGECPP_TEST_SAM3_MODEL_PATH must be defined"
#endif

#ifndef IMAGECPP_TEST_IMAGE_PATH
#error "IMAGECPP_TEST_IMAGE_PATH must be defined"
#endif

int main(void) {
    int exit_code = 1;
    imagecpp_error error = {0};
    imagecpp_runtime *runtime = NULL;
    imagecpp_model *model = NULL;
    imagecpp_session *session = NULL;
    imagecpp_image *image = NULL;
    imagecpp_detection_result *result = NULL;
    imagecpp_grounded_cutout_result *cutout_result = NULL;

    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_model_options model_options;
    imagecpp_model_options_init(&model_options);
    model_options.model_path = IMAGECPP_TEST_SAM3_MODEL_PATH;
    if (imagecpp_model_load(runtime, "image.detect.sam3", &model_options, &model, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_session_create(model, &session, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_image_load(IMAGECPP_TEST_IMAGE_PATH, NULL, &image, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_const_image_view image_view = {0};
    image_view.struct_size = sizeof(image_view);
    if (imagecpp_image_get_const_view(image, &image_view, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_session_set_image(session, &image_view, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }

    imagecpp_detect_options options;
    imagecpp_detect_options_init(&options);
    options.prompt = "cat";
    options.score_threshold = 0.3F;
    if (imagecpp_detect(session, &options, &result, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_detection_result_count(result) == 0) {
        goto cleanup;
    }

    imagecpp_detection_info info = {0};
    info.struct_size = sizeof(info);
    if (imagecpp_detection_result_info(result, 0, &info, &error) != IMAGECPP_STATUS_OK || info.label == NULL ||
        strcmp(info.label, "cat") != 0 || info.score < 0.3F || info.box.x1 <= info.box.x0 ||
        info.box.y1 <= info.box.y0 || info.mask.width != image_view.width || info.mask.height != image_view.height ||
        info.mask.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_U8) {
        exit_code = 2;
        goto cleanup;
    }

    size_t foreground = 0;
    const uint8_t *mask = (const uint8_t *)info.mask.data;
    for (uint32_t row = 0; row < info.mask.height; ++row) {
        const uint8_t *mask_row = mask + (size_t)row * info.mask.row_stride;
        for (uint32_t column = 0; column < info.mask.width; ++column) {
            foreground += mask_row[column] != 0;
        }
    }
    if (foreground == 0 || foreground == (size_t)info.mask.width * info.mask.height) {
        exit_code = 3;
        goto cleanup;
    }

    imagecpp_detection_result_destroy(result);
    result = NULL;
    options.prompt = "dog";
    options.score_threshold = 0.9F;
    if (imagecpp_detect(session, &options, &result, &error) != IMAGECPP_STATUS_OK) {
        exit_code = 4;
        goto cleanup;
    }

    imagecpp_detection_result_destroy(result);
    result = NULL;
    imagecpp_grounded_cutout_options cutout_options;
    imagecpp_grounded_cutout_options_init(&cutout_options);
    cutout_options.detect.prompt = "cat";
    cutout_options.detect.score_threshold = 0.3F;
    cutout_options.padding = 8;
    if (imagecpp_grounded_cutout(session, NULL, &image_view, &cutout_options, &cutout_result, &error) !=
        IMAGECPP_STATUS_OK) {
        exit_code = 5;
        goto cleanup;
    }
    imagecpp_grounded_cutout_info cutout_info = {0};
    cutout_info.struct_size = sizeof(cutout_info);
    if (imagecpp_grounded_cutout_result_info(cutout_result, &cutout_info, &error) != IMAGECPP_STATUS_OK ||
        cutout_info.matched_detection_count == 0 || cutout_info.selected_detection_count != 1 ||
        cutout_info.best_score < 0.3F || cutout_info.image.pixel_format != IMAGECPP_PIXEL_FORMAT_RGBA_U8 ||
        cutout_info.mask.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_U8 ||
        cutout_info.image.width != cutout_info.mask.width || cutout_info.image.height != cutout_info.mask.height ||
        cutout_info.source_box.x1 <= cutout_info.source_box.x0 ||
        cutout_info.source_box.y1 <= cutout_info.source_box.y0 || cutout_info.source_box.x0 < 0.0F ||
        cutout_info.source_box.y0 < 0.0F || cutout_info.source_box.x1 > (float)image_view.width ||
        cutout_info.source_box.y1 > (float)image_view.height) {
        exit_code = 6;
        goto cleanup;
    }
    const uint8_t *cutout_pixels = (const uint8_t *)cutout_info.image.data;
    const uint8_t *cutout_mask = (const uint8_t *)cutout_info.mask.data;
    for (uint32_t row = 0; row < cutout_info.image.height; ++row) {
        const uint8_t *pixel_row = cutout_pixels + (size_t)row * cutout_info.image.row_stride;
        const uint8_t *mask_row = cutout_mask + (size_t)row * cutout_info.mask.row_stride;
        for (uint32_t column = 0; column < cutout_info.image.width; ++column) {
            if (pixel_row[(size_t)column * 4 + 3] != mask_row[column]) {
                exit_code = 7;
                goto cleanup;
            }
        }
    }
    exit_code = 0;

cleanup:
    imagecpp_grounded_cutout_result_destroy(cutout_result);
    imagecpp_detection_result_destroy(result);
    imagecpp_image_destroy(image);
    imagecpp_session_destroy(session);
    imagecpp_model_destroy(model);
    imagecpp_runtime_destroy(runtime);
    return exit_code;
}
