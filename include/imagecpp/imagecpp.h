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
    IMAGECPP_STATUS_INTERNAL = 5,
    IMAGECPP_STATUS_IO_ERROR = 6,
    IMAGECPP_STATUS_MODEL_ERROR = 7,
    IMAGECPP_STATUS_NOT_READY = 8
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

typedef enum imagecpp_device {
    IMAGECPP_DEVICE_AUTO = 0,
    IMAGECPP_DEVICE_CPU = 1,
    IMAGECPP_DEVICE_GPU = 2
} imagecpp_device;

typedef enum imagecpp_sample_method {
    IMAGECPP_SAMPLE_METHOD_AUTO = 0,
    IMAGECPP_SAMPLE_METHOD_EULER = 1,
    IMAGECPP_SAMPLE_METHOD_EULER_A = 2,
    IMAGECPP_SAMPLE_METHOD_DPM_PLUS_PLUS_2M = 3,
    IMAGECPP_SAMPLE_METHOD_LCM = 4,
    IMAGECPP_SAMPLE_METHOD_DDIM = 5
} imagecpp_sample_method;

typedef enum imagecpp_scheduler {
    IMAGECPP_SCHEDULER_AUTO = 0,
    IMAGECPP_SCHEDULER_DISCRETE = 1,
    IMAGECPP_SCHEDULER_KARRAS = 2,
    IMAGECPP_SCHEDULER_EXPONENTIAL = 3,
    IMAGECPP_SCHEDULER_AYS = 4,
    IMAGECPP_SCHEDULER_SGM_UNIFORM = 5,
    IMAGECPP_SCHEDULER_SIMPLE = 6
} imagecpp_scheduler;

typedef enum imagecpp_file_format {
    IMAGECPP_FILE_FORMAT_AUTO = 0,
    IMAGECPP_FILE_FORMAT_PNG = 1,
    IMAGECPP_FILE_FORMAT_JPEG = 2,
    IMAGECPP_FILE_FORMAT_WEBP = 3,
    IMAGECPP_FILE_FORMAT_BMP = 4,
    IMAGECPP_FILE_FORMAT_TGA = 5
} imagecpp_file_format;

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

typedef struct imagecpp_decode_options {
    size_t struct_size;
    imagecpp_pixel_format pixel_format;
} imagecpp_decode_options;

typedef struct imagecpp_encode_options {
    size_t struct_size;
    int quality;
    int lossless;
} imagecpp_encode_options;

typedef struct imagecpp_model_options {
    size_t struct_size;
    const char *model_path;
    int32_t threads;
    imagecpp_device device;
} imagecpp_model_options;

typedef struct imagecpp_diffusion_model_options {
    size_t struct_size;
    const char *model_path;
    const char *diffusion_model_path;
    const char *vae_path;
    const char *clip_l_path;
    const char *clip_g_path;
    const char *t5xxl_path;
    const char *llm_path;
    int32_t threads;
    imagecpp_device device;
    int flash_attention;
    int keep_text_encoder_on_cpu;
    int keep_vae_on_cpu;
} imagecpp_diffusion_model_options;

typedef struct imagecpp_upscaler_model_options {
    size_t struct_size;
    const char *model_path;
    int32_t threads;
    imagecpp_device device;
    int32_t tile_size;
} imagecpp_upscaler_model_options;

typedef struct imagecpp_generate_options {
    size_t struct_size;
    const char *prompt;
    const char *negative_prompt;
    uint32_t width;
    uint32_t height;
    int32_t steps;
    float guidance;
    int64_t seed;
    int32_t batch_count;
    float strength;
    imagecpp_sample_method sample_method;
    imagecpp_scheduler scheduler;
    const imagecpp_const_image_view *init_image;
    const imagecpp_const_image_view *mask;
} imagecpp_generate_options;

typedef struct imagecpp_depth_options {
    size_t struct_size;
    int include_pose;
} imagecpp_depth_options;

typedef struct imagecpp_depth_info {
    size_t struct_size;
    imagecpp_const_image_view depth;
    imagecpp_const_image_view confidence;
    imagecpp_const_image_view sky;
    int is_metric;
    int has_pose;
    float extrinsics[12];
    float intrinsics[9];
} imagecpp_depth_info;

typedef struct imagecpp_point_prompt {
    float x;
    float y;
    int positive;
} imagecpp_point_prompt;

typedef struct imagecpp_box {
    float x0;
    float y0;
    float x1;
    float y1;
} imagecpp_box;

typedef struct imagecpp_segment_options {
    size_t struct_size;
    const imagecpp_point_prompt *points;
    size_t point_count;
    imagecpp_box box;
    int use_box;
    int multimask;
} imagecpp_segment_options;

typedef struct imagecpp_segment_info {
    size_t struct_size;
    imagecpp_const_image_view mask;
    imagecpp_box box;
    float score;
    float iou_score;
} imagecpp_segment_info;

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
typedef struct imagecpp_blob imagecpp_blob;
typedef struct imagecpp_model imagecpp_model;
typedef struct imagecpp_runtime imagecpp_runtime;
typedef struct imagecpp_session imagecpp_session;
typedef struct imagecpp_segment_result imagecpp_segment_result;
typedef struct imagecpp_image_result imagecpp_image_result;
typedef struct imagecpp_depth_result imagecpp_depth_result;

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

IMAGECPP_API void imagecpp_decode_options_init(imagecpp_decode_options *options);
IMAGECPP_API void imagecpp_encode_options_init(imagecpp_encode_options *options);
IMAGECPP_API imagecpp_status imagecpp_image_decode(const void *data, size_t data_size, imagecpp_file_format format,
                                                   const imagecpp_decode_options *options, imagecpp_image **output,
                                                   imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_image_load(const char *filename, const imagecpp_decode_options *options,
                                                 imagecpp_image **output, imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_image_encode(const imagecpp_const_image_view *image, imagecpp_file_format format,
                                                   const imagecpp_encode_options *options, imagecpp_blob **output,
                                                   imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_image_save(const char *filename, const imagecpp_const_image_view *image,
                                                 imagecpp_file_format format, const imagecpp_encode_options *options,
                                                 imagecpp_error *error);
IMAGECPP_API const void *imagecpp_blob_data(const imagecpp_blob *blob);
IMAGECPP_API size_t imagecpp_blob_size(const imagecpp_blob *blob);
IMAGECPP_API void imagecpp_blob_destroy(imagecpp_blob *blob);

IMAGECPP_API void imagecpp_model_options_init(imagecpp_model_options *options);
IMAGECPP_API void imagecpp_diffusion_model_options_init(imagecpp_diffusion_model_options *options);
IMAGECPP_API void imagecpp_upscaler_model_options_init(imagecpp_upscaler_model_options *options);
IMAGECPP_API imagecpp_status imagecpp_model_load(const imagecpp_runtime *runtime, const char *operation_id,
                                                 const imagecpp_model_options *options, imagecpp_model **output,
                                                 imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_diffusion_model_load(const imagecpp_runtime *runtime,
                                                           const imagecpp_diffusion_model_options *options,
                                                           imagecpp_model **output, imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_upscaler_model_load(const imagecpp_runtime *runtime,
                                                          const imagecpp_upscaler_model_options *options,
                                                          imagecpp_model **output, imagecpp_error *error);
IMAGECPP_API void imagecpp_model_destroy(imagecpp_model *model);
IMAGECPP_API imagecpp_status imagecpp_session_create(const imagecpp_model *model, imagecpp_session **output,
                                                     imagecpp_error *error);
IMAGECPP_API void imagecpp_session_destroy(imagecpp_session *session);
IMAGECPP_API imagecpp_status imagecpp_session_set_image(imagecpp_session *session,
                                                        const imagecpp_const_image_view *image, imagecpp_error *error);

IMAGECPP_API void imagecpp_segment_options_init(imagecpp_segment_options *options);
IMAGECPP_API imagecpp_status imagecpp_segment(imagecpp_session *session, const imagecpp_segment_options *options,
                                              imagecpp_segment_result **output, imagecpp_error *error);
IMAGECPP_API size_t imagecpp_segment_result_count(const imagecpp_segment_result *result);
IMAGECPP_API imagecpp_status imagecpp_segment_result_info(const imagecpp_segment_result *result, size_t index,
                                                          imagecpp_segment_info *output, imagecpp_error *error);
IMAGECPP_API void imagecpp_segment_result_destroy(imagecpp_segment_result *result);

IMAGECPP_API void imagecpp_generate_options_init(imagecpp_generate_options *options);
IMAGECPP_API imagecpp_status imagecpp_generate(const imagecpp_model *model, const imagecpp_generate_options *options,
                                               imagecpp_image_result **output, imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_upscale(const imagecpp_model *model, const imagecpp_const_image_view *image,
                                              uint32_t factor, imagecpp_image_result **output, imagecpp_error *error);
IMAGECPP_API size_t imagecpp_image_result_count(const imagecpp_image_result *result);
IMAGECPP_API imagecpp_status imagecpp_image_result_view(const imagecpp_image_result *result, size_t index,
                                                        imagecpp_const_image_view *output, imagecpp_error *error);
IMAGECPP_API void imagecpp_image_result_destroy(imagecpp_image_result *result);

IMAGECPP_API void imagecpp_depth_options_init(imagecpp_depth_options *options);
IMAGECPP_API imagecpp_status imagecpp_depth(const imagecpp_model *model, const imagecpp_const_image_view *image,
                                            const imagecpp_depth_options *options, imagecpp_depth_result **output,
                                            imagecpp_error *error);
IMAGECPP_API imagecpp_status imagecpp_depth_result_info(const imagecpp_depth_result *result,
                                                        imagecpp_depth_info *output, imagecpp_error *error);
IMAGECPP_API void imagecpp_depth_result_destroy(imagecpp_depth_result *result);

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
