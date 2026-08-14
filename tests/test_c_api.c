#include "imagecpp/imagecpp.h"

int main(void) {
    imagecpp_error error = {0};
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
