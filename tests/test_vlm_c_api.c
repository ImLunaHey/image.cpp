#include "imagecpp/imagecpp.h"

#include <string.h>

int main(void) {
    imagecpp_error error = {0};
    imagecpp_runtime *runtime = NULL;
    imagecpp_model *model = NULL;
    imagecpp_image *image = NULL;
    imagecpp_text_result *result = NULL;
    int exit_code = 1;

    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_vlm_model_options model_options;
    imagecpp_vlm_model_options_init(&model_options);
    model_options.model_path = IMAGECPP_TEST_VLM_MODEL_PATH;
    model_options.projection_model_path = IMAGECPP_TEST_VLM_PROJECTION_MODEL_PATH;
    model_options.device = IMAGECPP_DEVICE_CPU;
    if (imagecpp_vlm_model_load(runtime, &model_options, &model, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    if (imagecpp_image_load(IMAGECPP_TEST_IMAGE_PATH, NULL, &image, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_const_image_view view = {0};
    view.struct_size = sizeof(view);
    if (imagecpp_image_get_const_view(image, &view, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_visual_query_options query;
    imagecpp_visual_query_options_init(&query);
    query.prompt = "What animal is in the image? Answer with one word.";
    query.max_tokens = 8;
    query.temperature = 0.0F;
    if (imagecpp_visual_query(model, &view, &query, &result, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_text_info info = {0};
    info.struct_size = sizeof(info);
    if (imagecpp_text_result_info(result, &info, &error) != IMAGECPP_STATUS_OK || info.text == NULL ||
        info.text[0] == '\0' || info.generated_tokens == 0 || info.prompt_tokens == 0) {
        goto cleanup;
    }
    if (strstr(info.text, "cat") == NULL && strstr(info.text, "Cat") == NULL) {
        goto cleanup;
    }
    exit_code = 0;

cleanup:
    imagecpp_text_result_destroy(result);
    imagecpp_image_destroy(image);
    imagecpp_model_destroy(model);
    imagecpp_runtime_destroy(runtime);
    return exit_code;
}
