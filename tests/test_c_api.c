#include "imagecpp/imagecpp.h"

int main(void) {
    imagecpp_error error = {0};
    imagecpp_diffusion_model_options diffusion_options;
    imagecpp_diffusion_model_options_init(&diffusion_options);
    if (diffusion_options.struct_size != sizeof(diffusion_options) || diffusion_options.flash_attention != 1) {
        return 6;
    }
    imagecpp_upscaler_model_options upscaler_options;
    imagecpp_upscaler_model_options_init(&upscaler_options);
    if (upscaler_options.struct_size != sizeof(upscaler_options)) {
        return 7;
    }
    imagecpp_generate_options generate_options;
    imagecpp_generate_options_init(&generate_options);
    if (generate_options.struct_size != sizeof(generate_options) || generate_options.width != 512 ||
        generate_options.height != 512 || generate_options.batch_count != 1) {
        return 8;
    }
    imagecpp_depth_options depth_options;
    imagecpp_depth_options_init(&depth_options);
    if (depth_options.struct_size != sizeof(depth_options) || depth_options.include_pose != 0) {
        return 9;
    }
    imagecpp_vlm_model_options vlm_model_options;
    imagecpp_vlm_model_options_init(&vlm_model_options);
    if (vlm_model_options.struct_size != sizeof(vlm_model_options) || vlm_model_options.context_size != 4096) {
        return 13;
    }
    imagecpp_visual_query_options visual_options;
    imagecpp_visual_query_options_init(&visual_options);
    if (visual_options.struct_size != sizeof(visual_options) || visual_options.max_tokens != 128 ||
        visual_options.top_k != 40 || IMAGECPP_TEXT_FINISH_CANCELLED != 2) {
        return 14;
    }
    imagecpp_ocr_options ocr_options;
    imagecpp_ocr_options_init(&ocr_options);
    if (ocr_options.struct_size != sizeof(ocr_options) || ocr_options.page_segmentation != IMAGECPP_OCR_PAGE_AUTO ||
        ocr_options.source_dpi != 300 || ocr_options.preserve_interword_spaces != 0) {
        return 12;
    }
    imagecpp_cutout_options cutout_options;
    imagecpp_cutout_options_init(&cutout_options);
    if (cutout_options.struct_size != sizeof(cutout_options) ||
        cutout_options.segment.struct_size != sizeof(cutout_options.segment) || cutout_options.crop_to_mask != 1 ||
        cutout_options.upscale_factor != 1) {
        return 10;
    }
    imagecpp_detect_options detect_options;
    imagecpp_detect_options_init(&detect_options);
    if (detect_options.struct_size != sizeof(detect_options) || detect_options.score_threshold != 0.5F ||
        detect_options.nms_threshold != 0.1F) {
        return 11;
    }
    imagecpp_image_desc desc = {
        sizeof(imagecpp_image_desc), 1, 1, 0, IMAGECPP_PIXEL_FORMAT_RGB_U8, IMAGECPP_COLOR_SPACE_SRGB,
    };
    imagecpp_image *image = NULL;
    if (imagecpp_image_create(&desc, &image, &error) != IMAGECPP_STATUS_OK) {
        return 1;
    }
    imagecpp_image_view view = {0};
    view.struct_size = sizeof(imagecpp_image_view);
    if (imagecpp_image_get_view(image, &view, &error) != IMAGECPP_STATUS_OK) {
        imagecpp_image_destroy(image);
        return 2;
    }
    ((unsigned char *)view.data)[0] = 11;
    ((unsigned char *)view.data)[1] = 22;
    ((unsigned char *)view.data)[2] = 33;
    imagecpp_const_image_view const_view = {
        sizeof(imagecpp_const_image_view),
        view.data,
        view.data_size,
        view.width,
        view.height,
        view.row_stride,
        view.pixel_format,
        view.color_space,
    };
    imagecpp_blob *blob = NULL;
    if (imagecpp_image_encode(&const_view, IMAGECPP_FILE_FORMAT_PNG, NULL, &blob, &error) != IMAGECPP_STATUS_OK ||
        imagecpp_blob_data(blob) == NULL || imagecpp_blob_size(blob) < 8) {
        imagecpp_image_destroy(image);
        imagecpp_blob_destroy(blob);
        return 3;
    }
    imagecpp_image *decoded = NULL;
    if (imagecpp_image_decode(imagecpp_blob_data(blob), imagecpp_blob_size(blob), IMAGECPP_FILE_FORMAT_AUTO, NULL,
                              &decoded, &error) != IMAGECPP_STATUS_OK) {
        imagecpp_image_destroy(image);
        imagecpp_blob_destroy(blob);
        return 4;
    }
    imagecpp_image_destroy(decoded);
    imagecpp_blob_destroy(blob);
    imagecpp_image_destroy(image);
    return imagecpp_version() == 0 ? 5 : 0;
}
