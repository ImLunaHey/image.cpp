#include <imagecpp/imagecpp.h>

#include <stdio.h>

int main(void) {
    imagecpp_runtime *runtime = NULL;
    imagecpp_error error = {0};
    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    const size_t operation_count = imagecpp_runtime_operation_count(runtime);
    imagecpp_cutout_options cutout_options;
    imagecpp_cutout_options_init(&cutout_options);
    imagecpp_detect_options detect_options;
    imagecpp_detect_options_init(&detect_options);
    imagecpp_grounded_cutout_options grounded_options;
    imagecpp_grounded_cutout_options_init(&grounded_options);
    imagecpp_ocr_options ocr_options;
    imagecpp_ocr_options_init(&ocr_options);
    imagecpp_runtime_destroy(runtime);
    return operation_count == 0 || cutout_options.struct_size != sizeof(cutout_options) ||
                   detect_options.struct_size != sizeof(detect_options) ||
                   grounded_options.struct_size != sizeof(grounded_options) ||
                   ocr_options.struct_size != sizeof(ocr_options)
               ? 2
               : 0;
}
