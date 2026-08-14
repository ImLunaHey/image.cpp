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
    imagecpp_runtime_destroy(runtime);
    return operation_count == 0 ? 2 : 0;
}
