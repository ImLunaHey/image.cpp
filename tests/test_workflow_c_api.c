#include "imagecpp/imagecpp.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifndef IMAGECPP_TEST_SAM_MODEL_PATH
#error "IMAGECPP_TEST_SAM_MODEL_PATH must be defined"
#endif

#ifndef IMAGECPP_TEST_UPSCALER_MODEL_PATH
#error "IMAGECPP_TEST_UPSCALER_MODEL_PATH must be defined"
#endif

#ifndef IMAGECPP_TEST_IMAGE_PATH
#error "IMAGECPP_TEST_IMAGE_PATH must be defined"
#endif

int main(void) {
    int exit_code = 1;
    imagecpp_error error = {0};
    imagecpp_runtime *runtime = NULL;
    imagecpp_model *segment_model = NULL;
    imagecpp_model *upscaler_model = NULL;
    imagecpp_session *session = NULL;
    imagecpp_image *image = NULL;
    imagecpp_cutout_result *result = NULL;

    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_model_options segment_model_options;
    imagecpp_model_options_init(&segment_model_options);
    segment_model_options.model_path = IMAGECPP_TEST_SAM_MODEL_PATH;
    if (imagecpp_model_load(runtime, "image.segment.sam", &segment_model_options, &segment_model, &error) !=
        IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_upscaler_model_options upscaler_options;
    imagecpp_upscaler_model_options_init(&upscaler_options);
    upscaler_options.model_path = IMAGECPP_TEST_UPSCALER_MODEL_PATH;
    if (imagecpp_upscaler_model_load(runtime, &upscaler_options, &upscaler_model, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    if (imagecpp_session_create(segment_model, &session, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_image_load(IMAGECPP_TEST_IMAGE_PATH, NULL, &image, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_const_image_view image_view = {0};
    image_view.struct_size = sizeof(image_view);
    if (imagecpp_image_get_const_view(image, &image_view, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }

    const imagecpp_point_prompt point = {1.0F, 1.0F, 1};
    imagecpp_cutout_options options;
    imagecpp_cutout_options_init(&options);
    options.segment.points = &point;
    options.segment.point_count = 1;
    options.crop_to_mask = 0;
    options.upscale_factor = 4;
    if (imagecpp_cutout(session, upscaler_model, &image_view, &options, &result, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }

    imagecpp_cutout_info info = {0};
    info.struct_size = sizeof(info);
    if (imagecpp_cutout_result_info(result, &info, &error) != IMAGECPP_STATUS_OK || info.image.width != 8 ||
        info.image.height != 8 || info.image.pixel_format != IMAGECPP_PIXEL_FORMAT_RGBA_U8 || info.mask.width != 8 ||
        info.mask.height != 8 || info.mask.pixel_format != IMAGECPP_PIXEL_FORMAT_GRAY_U8 ||
        info.source_box.x0 != 0.0F || info.source_box.y0 != 0.0F || info.source_box.x1 != 2.0F ||
        info.source_box.y1 != 2.0F) {
        exit_code = 2;
        goto cleanup;
    }

    for (uint32_t y = 0; y < info.image.height; ++y) {
        const uint8_t *rgba = (const uint8_t *)info.image.data + (size_t)y * info.image.row_stride;
        const uint8_t *mask = (const uint8_t *)info.mask.data + (size_t)y * info.mask.row_stride;
        for (uint32_t x = 0; x < info.image.width; ++x) {
            if (rgba[(size_t)x * 4 + 3] != mask[x]) {
                exit_code = 3;
                goto cleanup;
            }
        }
    }
    exit_code = 0;

cleanup:
    imagecpp_cutout_result_destroy(result);
    imagecpp_image_destroy(image);
    imagecpp_session_destroy(session);
    imagecpp_model_destroy(upscaler_model);
    imagecpp_model_destroy(segment_model);
    imagecpp_runtime_destroy(runtime);
    return exit_code;
}
