#include "imagecpp/imagecpp.h"

#include <math.h>
#include <stddef.h>

#ifndef IMAGECPP_TEST_CLIP_MODEL_PATH
#error IMAGECPP_TEST_CLIP_MODEL_PATH is required
#endif
#ifndef IMAGECPP_TEST_IMAGE_PATH
#error IMAGECPP_TEST_IMAGE_PATH is required
#endif

static int normalized(const imagecpp_embedding_result *result, size_t expected_size) {
    const size_t size = imagecpp_embedding_result_size(result);
    const float *values = imagecpp_embedding_result_data(result);
    if (size != expected_size || values == NULL) {
        return 0;
    }
    double norm_squared = 0.0;
    for (size_t index = 0; index < size; ++index) {
        if (!isfinite(values[index])) {
            return 0;
        }
        norm_squared += (double)values[index] * values[index];
    }
    return norm_squared > 0.999 && norm_squared < 1.001;
}

int main(void) {
    imagecpp_error error = {0};
    imagecpp_runtime *runtime = NULL;
    imagecpp_model *model = NULL;
    imagecpp_image *image = NULL;
    imagecpp_embedding_result *image_embedding = NULL;
    imagecpp_embedding_result *text_embedding = NULL;
    imagecpp_classification_result *classification = NULL;
    int result = 1;

    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        goto cleanup;
    }
    imagecpp_model_options options;
    imagecpp_model_options_init(&options);
    options.model_path = IMAGECPP_TEST_CLIP_MODEL_PATH;
    options.threads = 4;
    options.device = IMAGECPP_DEVICE_CPU;
    if (imagecpp_model_load(runtime, "image.embed.clip", &options, &model, &error) != IMAGECPP_STATUS_OK) {
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
    if (imagecpp_embed_image(model, &view, &image_embedding, &error) != IMAGECPP_STATUS_OK ||
        !normalized(image_embedding, 512)) {
        goto cleanup;
    }
    if (imagecpp_embed_text(model, "a photo of a colorful square", &text_embedding, &error) != IMAGECPP_STATUS_OK ||
        !normalized(text_embedding, 512)) {
        goto cleanup;
    }

    const char *labels[] = {"cat", "dog", "car"};
    if (imagecpp_classify(model, &view, labels, 3, &classification, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_classification_result_count(classification) != 3) {
        goto cleanup;
    }
    float previous = 2.0F;
    float total = 0.0F;
    for (size_t index = 0; index < 3; ++index) {
        imagecpp_classification_info info = {0};
        info.struct_size = sizeof(info);
        if (imagecpp_classification_result_info(classification, index, &info, &error) != IMAGECPP_STATUS_OK ||
            info.label == NULL || info.label_index >= 3 || !isfinite(info.score) || info.score > previous) {
            goto cleanup;
        }
        previous = info.score;
        total += info.score;
    }
    if (total < 0.999F || total > 1.001F) {
        goto cleanup;
    }
    result = 0;

cleanup:
    imagecpp_classification_result_destroy(classification);
    imagecpp_embedding_result_destroy(text_embedding);
    imagecpp_embedding_result_destroy(image_embedding);
    imagecpp_image_destroy(image);
    imagecpp_model_destroy(model);
    imagecpp_runtime_destroy(runtime);
    return result;
}
