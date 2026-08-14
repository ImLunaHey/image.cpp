#ifndef IMAGECPP_IMAGECPP_H
#define IMAGECPP_IMAGECPP_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(IMAGECPP_SHARED)
#if defined(IMAGECPP_BUILDING_LIBRARY)
#define IMAGECPP_API __declspec(dllexport)
#else
#define IMAGECPP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(IMAGECPP_SHARED)
#define IMAGECPP_API __attribute__((visibility("default")))
#else
#define IMAGECPP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGECPP_VERSION_MAJOR 0
#define IMAGECPP_VERSION_MINOR 1
#define IMAGECPP_VERSION_PATCH 0
#define IMAGECPP_ERROR_MESSAGE_CAPACITY 256

typedef enum imagecpp_status {
    IMAGECPP_STATUS_OK = 0,
    IMAGECPP_STATUS_INVALID_ARGUMENT = 1,
    IMAGECPP_STATUS_OUT_OF_RANGE = 2,
    IMAGECPP_STATUS_UNSUPPORTED = 3,
    IMAGECPP_STATUS_OUT_OF_MEMORY = 4,
    IMAGECPP_STATUS_INTERNAL = 5
} imagecpp_status;

typedef struct imagecpp_error {
    imagecpp_status code;
    char message[IMAGECPP_ERROR_MESSAGE_CAPACITY];
} imagecpp_error;

typedef enum imagecpp_pixel_format {
    IMAGECPP_PIXEL_FORMAT_UNKNOWN = 0,
    IMAGECPP_PIXEL_FORMAT_GRAY_U8 = 1,
    IMAGECPP_PIXEL_FORMAT_RGB_U8 = 2,
    IMAGECPP_PIXEL_FORMAT_RGBA_U8 = 3,
    IMAGECPP_PIXEL_FORMAT_BGRA_U8 = 4,
    IMAGECPP_PIXEL_FORMAT_GRAY_F32 = 5,
    IMAGECPP_PIXEL_FORMAT_RGB_F32 = 6,
    IMAGECPP_PIXEL_FORMAT_RGBA_F32 = 7
} imagecpp_pixel_format;

typedef enum imagecpp_color_space {
    IMAGECPP_COLOR_SPACE_UNKNOWN = 0,
    IMAGECPP_COLOR_SPACE_SRGB = 1,
    IMAGECPP_COLOR_SPACE_LINEAR_SRGB = 2
} imagecpp_color_space;

typedef enum imagecpp_resize_filter {
    IMAGECPP_RESIZE_NEAREST = 0,
    IMAGECPP_RESIZE_BILINEAR = 1
} imagecpp_resize_filter;

typedef enum imagecpp_artifact_kind {
    IMAGECPP_ARTIFACT_IMAGE = 0,
    IMAGECPP_ARTIFACT_MASK = 1,
    IMAGECPP_ARTIFACT_BOXES = 2,
    IMAGECPP_ARTIFACT_KEYPOINTS = 3,
    IMAGECPP_ARTIFACT_DEPTH_MAP = 4,
    IMAGECPP_ARTIFACT_TEXT_REGIONS = 5,
    IMAGECPP_ARTIFACT_EMBEDDING = 6,
    IMAGECPP_ARTIFACT_METADATA = 7
} imagecpp_artifact_kind;

typedef enum imagecpp_task {
    IMAGECPP_TASK_IMAGE_UTILITY = 0,
    IMAGECPP_TASK_EMBED = 1,
    IMAGECPP_TASK_CLASSIFY = 2,
    IMAGECPP_TASK_DETECT = 3,
    IMAGECPP_TASK_SEGMENT = 4,
    IMAGECPP_TASK_MATTING = 5,
    IMAGECPP_TASK_DEPTH = 6,
    IMAGECPP_TASK_POSE = 7,
    IMAGECPP_TASK_OCR = 8,
    IMAGECPP_TASK_CAPTION = 9,
    IMAGECPP_TASK_GENERATE = 10,
    IMAGECPP_TASK_EDIT = 11,
    IMAGECPP_TASK_UPSCALE = 12,
    IMAGECPP_TASK_RESTORE = 13
} imagecpp_task;

typedef struct imagecpp_const_image_view {
    size_t struct_size;
    const void *data;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    size_t row_stride;
    imagecpp_pixel_format pixel_format;
    imagecpp_color_space color_space;
} imagecpp_const_image_view;

typedef struct imagecpp_image_view {
    size_t struct_size;
    void *data;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    size_t row_stride;
    imagecpp_pixel_format pixel_format;
    imagecpp_color_space color_space;
} imagecpp_image_view;

typedef struct imagecpp_image_desc {
    size_t struct_size;
    uint32_t width;
    uint32_t height;
    size_t row_stride;
    imagecpp_pixel_format pixel_format;
    imagecpp_color_space color_space;
} imagecpp_image_desc;

typedef struct imagecpp_operation_info {
    size_t struct_size;
    const char *id;
    const char *name;
    const char *description;
    imagecpp_task task;
    imagecpp_artifact_kind input_kind;
    imagecpp_artifact_kind output_kind;
} imagecpp_operation_info;

typedef struct imagecpp_image imagecpp_image;
typedef struct imagecpp_runtime imagecpp_runtime;

IMAGECPP_API uint32_t imagecpp_version(void);
IMAGECPP_API const char *imagecpp_version_string(void);
IMAGECPP_API const char *imagecpp_status_string(imagecpp_status status);
IMAGECPP_API void imagecpp_error_clear(imagecpp_error *error);

IMAGECPP_API size_t imagecpp_pixel_format_channels(imagecpp_pixel_format format);
IMAGECPP_API size_t imagecpp_pixel_format_bytes_per_channel(imagecpp_pixel_format format);
IMAGECPP_API size_t imagecpp_pixel_format_bytes_per_pixel(imagecpp_pixel_format format);

IMAGECPP_API imagecpp_status imagecpp_validate_const_image_view(const imagecpp_const_image_view *view,
                                                                imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_validate_image_view(const imagecpp_image_view *view, imagecpp_error *error);

IMAGECPP_API imagecpp_status imagecpp_image_create(const imagecpp_image_desc *desc, imagecpp_image **output,
                                                   imagecpp_error *error);
IMAGECPP_API void imagecpp_image_destroy(imagecpp_image *image);
IMAGECPP_API imagecpp_status imagecpp_image_get_view(imagecpp_image *image, imagecpp_image_view *output,
                                                     imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_image_get_const_view(const imagecpp_image *image,
                                                           imagecpp_const_image_view *output, imagecpp_error *error);

IMAGECPP_API imagecpp_status imagecpp_resize(const imagecpp_const_image_view *source,
                                             const imagecpp_image_view *destination, imagecpp_resize_filter filter,
                                             imagecpp_error *error);

IMAGECPP_API imagecpp_status imagecpp_runtime_create(imagecpp_runtime **output, imagecpp_error *error);
IMAGECPP_API void imagecpp_runtime_destroy(imagecpp_runtime *runtime);
IMAGECPP_API size_t imagecpp_runtime_operation_count(const imagecpp_runtime *runtime);
IMAGECPP_API imagecpp_status imagecpp_runtime_operation_info(const imagecpp_runtime *runtime, size_t index,
                                                             imagecpp_operation_info *output, imagecpp_error *error);

#ifdef __cplusplus
}
#endif

#endif
