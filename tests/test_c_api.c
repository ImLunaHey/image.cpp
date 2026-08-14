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
    imagecpp_image_destroy(image);
    return imagecpp_version() == 0 ? 2 : 0;
}
