#include "model/model.hpp"

#include "core/status.hpp"
#include "image/layout.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

struct imagecpp_model {
    std::shared_ptr<imagecpp::detail::Model> implementation;
};

struct imagecpp_session {
    std::shared_ptr<imagecpp::detail::Model> model;
    std::unique_ptr<imagecpp::detail::Session> implementation;
};

struct imagecpp_segment_result {
    std::vector<imagecpp::detail::SegmentOutput> outputs;
};

struct imagecpp_detection_result {
    std::vector<imagecpp::detail::DetectionOutput> outputs;
};

struct imagecpp_image_result {
    std::vector<imagecpp::detail::ImageOutput> outputs;
};

struct imagecpp_depth_result {
    imagecpp::detail::DepthOutput output;
};

struct imagecpp_embedding_result {
    std::vector<float> values;
};

struct imagecpp_classification_result {
    std::vector<imagecpp::detail::ClassificationOutput> outputs;
};

struct imagecpp_ocr_result {
    imagecpp::detail::OcrOutput output;
};

struct imagecpp_text_result {
    imagecpp::detail::TextOutput output;
};

namespace imagecpp::detail {
namespace {

imagecpp_status translate_exception(imagecpp_error *error, const std::exception &exception) noexcept {
    if (const auto *failure = dynamic_cast<const Failure *>(&exception); failure != nullptr) {
        return core::fail(error, failure->status(), failure->what());
    }
    if (dynamic_cast<const std::bad_alloc *>(&exception) != nullptr) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "model operation allocation failed");
    }
    if (dynamic_cast<const std::invalid_argument *>(&exception) != nullptr) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, exception.what());
    }
    return core::fail(error, IMAGECPP_STATUS_INTERNAL, exception.what());
}

#if defined(IMAGECPP_WITH_SAM3) || defined(IMAGECPP_WITH_DEPTH_ANYTHING) || defined(IMAGECPP_WITH_CLIP) ||             \
    defined(IMAGECPP_WITH_TESSERACT)
const imagecpp_model_options &validate_model_options(const imagecpp_model_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_model_options)) {
        throw std::invalid_argument("model options are null or too small");
    }
    if (options->model_path == nullptr || options->model_path[0] == '\0') {
        throw std::invalid_argument("model path is empty");
    }
    if (options->threads < 0) {
        throw std::invalid_argument("model thread count cannot be negative");
    }
    if (options->device != IMAGECPP_DEVICE_AUTO && options->device != IMAGECPP_DEVICE_CPU &&
        options->device != IMAGECPP_DEVICE_GPU) {
        throw std::invalid_argument("unknown model device");
    }
    return *options;
}
#endif

#if defined(IMAGECPP_WITH_VLM)
const imagecpp_vlm_model_options &validate_vlm_model_options(const imagecpp_vlm_model_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_vlm_model_options)) {
        throw std::invalid_argument("VLM model options are null or too small");
    }
    if (options->model_path == nullptr || options->model_path[0] == '\0') {
        throw std::invalid_argument("VLM language model path is empty");
    }
    if (options->projection_model_path == nullptr || options->projection_model_path[0] == '\0') {
        throw std::invalid_argument("VLM projection model path is empty");
    }
    if (options->threads < 0) {
        throw std::invalid_argument("VLM thread count cannot be negative");
    }
    if (options->device != IMAGECPP_DEVICE_AUTO && options->device != IMAGECPP_DEVICE_CPU &&
        options->device != IMAGECPP_DEVICE_GPU) {
        throw std::invalid_argument("unknown VLM device");
    }
    if (options->context_size < 512) {
        throw std::invalid_argument("VLM context size must be at least 512 tokens");
    }
    return *options;
}
#endif

VisualQueryRequest visual_query_request(const imagecpp_visual_query_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_visual_query_options)) {
        throw std::invalid_argument("visual query options are null or too small");
    }
    if (options->max_tokens == 0) {
        throw std::invalid_argument("visual query max tokens must be positive");
    }
    if (!std::isfinite(options->temperature) || options->temperature < 0.0F) {
        throw std::invalid_argument("visual query temperature must be finite and non-negative");
    }
    if (!std::isfinite(options->top_p) || options->top_p <= 0.0F || options->top_p > 1.0F) {
        throw std::invalid_argument("visual query top-p must be greater than zero and at most one");
    }
    if (options->top_k < 0) {
        throw std::invalid_argument("visual query top-k cannot be negative");
    }
    VisualQueryRequest request;
    request.prompt = options->prompt == nullptr || options->prompt[0] == '\0'
                         ? "Describe this image in one concise paragraph."
                         : options->prompt;
    request.max_tokens = options->max_tokens;
    request.temperature = options->temperature;
    request.top_p = options->top_p;
    request.top_k = options->top_k;
    request.seed = options->seed;
    return request;
}

OcrRequest ocr_request(const imagecpp_ocr_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_ocr_options)) {
        throw std::invalid_argument("OCR options are null or too small");
    }
    if (options->page_segmentation < IMAGECPP_OCR_PAGE_AUTO ||
        options->page_segmentation > IMAGECPP_OCR_PAGE_RAW_LINE) {
        throw std::invalid_argument("unknown OCR page segmentation mode");
    }
    if (options->source_dpi > 2400) {
        throw std::invalid_argument("OCR source DPI cannot exceed 2400");
    }
    return {options->page_segmentation, options->source_dpi, options->preserve_interword_spaces != 0};
}

#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
const imagecpp_diffusion_model_options &
validate_diffusion_model_options(const imagecpp_diffusion_model_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_diffusion_model_options)) {
        throw std::invalid_argument("diffusion model options are null or too small");
    }
    const bool has_checkpoint = options->model_path != nullptr && options->model_path[0] != '\0';
    const bool has_diffusion = options->diffusion_model_path != nullptr && options->diffusion_model_path[0] != '\0';
    if (!has_checkpoint && !has_diffusion) {
        throw std::invalid_argument("diffusion model requires a checkpoint or diffusion-model path");
    }
    if (options->threads < 0) {
        throw std::invalid_argument("diffusion model thread count cannot be negative");
    }
    if (options->device != IMAGECPP_DEVICE_AUTO && options->device != IMAGECPP_DEVICE_CPU &&
        options->device != IMAGECPP_DEVICE_GPU) {
        throw std::invalid_argument("unknown diffusion model device");
    }
    return *options;
}

const imagecpp_upscaler_model_options &validate_upscaler_model_options(const imagecpp_upscaler_model_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_upscaler_model_options)) {
        throw std::invalid_argument("upscaler model options are null or too small");
    }
    if (options->model_path == nullptr || options->model_path[0] == '\0') {
        throw std::invalid_argument("upscaler model path is empty");
    }
    if (options->threads < 0 || options->tile_size < 0) {
        throw std::invalid_argument("upscaler thread count and tile size cannot be negative");
    }
    if (options->device != IMAGECPP_DEVICE_AUTO && options->device != IMAGECPP_DEVICE_CPU &&
        options->device != IMAGECPP_DEVICE_GPU) {
        throw std::invalid_argument("unknown upscaler model device");
    }
    return *options;
}
#endif

SegmentRequest segment_request(const imagecpp_segment_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_segment_options)) {
        throw std::invalid_argument("segment options are null or too small");
    }
    if (options->point_count != 0 && options->points == nullptr) {
        throw std::invalid_argument("segment point array is null");
    }
    if (options->point_count == 0 && options->use_box == 0) {
        throw std::invalid_argument("segmentation requires at least one point or a box");
    }

    SegmentRequest request;
    if (options->point_count != 0) {
        request.points.assign(options->points, options->points + options->point_count);
    }
    for (const imagecpp_point_prompt &point : request.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            throw std::invalid_argument("segment point coordinates must be finite");
        }
    }
    request.box = options->box;
    request.use_box = options->use_box != 0;
    request.multimask = options->multimask != 0;
    if (request.use_box &&
        (!std::isfinite(request.box.x0) || !std::isfinite(request.box.y0) || !std::isfinite(request.box.x1) ||
         !std::isfinite(request.box.y1) || request.box.x1 <= request.box.x0 || request.box.y1 <= request.box.y0)) {
        throw std::invalid_argument("segment box must be finite and have positive area");
    }
    return request;
}

bool valid_box(const imagecpp_box &box) {
    return std::isfinite(box.x0) && std::isfinite(box.y0) && std::isfinite(box.x1) && std::isfinite(box.y1) &&
           box.x1 > box.x0 && box.y1 > box.y0;
}

DetectRequest detect_request(const imagecpp_detect_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_detect_options)) {
        throw std::invalid_argument("detection options are null or too small");
    }
    if (options->prompt == nullptr || options->prompt[0] == '\0' ||
        std::string_view(options->prompt).find_first_not_of(" \t\r\n\f\v") == std::string_view::npos) {
        throw std::invalid_argument("detection prompt is empty");
    }
    if ((options->positive_exemplar_count != 0 && options->positive_exemplars == nullptr) ||
        (options->negative_exemplar_count != 0 && options->negative_exemplars == nullptr)) {
        throw std::invalid_argument("detection exemplar box array is null");
    }
    if (!std::isfinite(options->score_threshold) || options->score_threshold < 0.0F ||
        options->score_threshold > 1.0F || !std::isfinite(options->nms_threshold) || options->nms_threshold < 0.0F ||
        options->nms_threshold > 1.0F) {
        throw std::invalid_argument("detection score and NMS thresholds must be between zero and one");
    }

    DetectRequest request;
    request.prompt = options->prompt;
    if (options->positive_exemplar_count != 0) {
        request.positive_exemplars.assign(options->positive_exemplars,
                                          options->positive_exemplars + options->positive_exemplar_count);
    }
    if (options->negative_exemplar_count != 0) {
        request.negative_exemplars.assign(options->negative_exemplars,
                                          options->negative_exemplars + options->negative_exemplar_count);
    }
    for (const imagecpp_box &box : request.positive_exemplars) {
        if (!valid_box(box)) {
            throw std::invalid_argument("positive exemplar boxes must be finite and have positive area");
        }
    }
    for (const imagecpp_box &box : request.negative_exemplars) {
        if (!valid_box(box)) {
            throw std::invalid_argument("negative exemplar boxes must be finite and have positive area");
        }
    }
    request.score_threshold = options->score_threshold;
    request.nms_threshold = options->nms_threshold;
    return request;
}

GenerateRequest generate_request(const imagecpp_generate_options *options) {
    if (options == nullptr || options->struct_size < sizeof(imagecpp_generate_options)) {
        throw std::invalid_argument("generation options are null or too small");
    }
    if (options->prompt == nullptr || options->prompt[0] == '\0') {
        throw std::invalid_argument("generation prompt is empty");
    }
    if (options->width == 0 || options->height == 0) {
        throw std::invalid_argument("generation dimensions must be positive");
    }
    if (options->width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        options->height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        throw std::invalid_argument("generation dimensions exceed the provider limit");
    }
    if (options->steps <= 0 || options->batch_count <= 0) {
        throw std::invalid_argument("generation steps and batch count must be positive");
    }
    if (!std::isfinite(options->guidance) || options->guidance < 0.0F) {
        throw std::invalid_argument("generation guidance must be finite and non-negative");
    }
    if (!std::isfinite(options->strength) || options->strength < 0.0F || options->strength > 1.0F) {
        throw std::invalid_argument("generation strength must be between zero and one");
    }
    if (options->sample_method < IMAGECPP_SAMPLE_METHOD_AUTO || options->sample_method > IMAGECPP_SAMPLE_METHOD_DDIM) {
        throw std::invalid_argument("unknown generation sample method");
    }
    if (options->scheduler < IMAGECPP_SCHEDULER_AUTO || options->scheduler > IMAGECPP_SCHEDULER_SIMPLE) {
        throw std::invalid_argument("unknown generation scheduler");
    }
    if (options->mask != nullptr && options->init_image == nullptr) {
        throw std::invalid_argument("an inpainting mask requires an initial image");
    }

    return {
        options->prompt,    options->negative_prompt == nullptr ? "" : options->negative_prompt,
        options->width,     options->height,
        options->steps,     options->guidance,
        options->seed,      options->batch_count,
        options->strength,  options->sample_method,
        options->scheduler, options->init_image,
        options->mask,
    };
}

} // namespace

std::unique_ptr<Session> Model::create_session() {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not provide reusable image sessions");
}

std::vector<DetectionOutput> Session::detect(const DetectRequest &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this session does not support text-prompted detection");
}

std::vector<ImageOutput> Model::generate(const GenerateRequest &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support image generation");
}

ImageOutput Model::upscale(const imagecpp_const_image_view &, uint32_t) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support image upscaling");
}

DepthOutput Model::depth(const imagecpp_const_image_view &, bool) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support depth estimation");
}

std::vector<float> Model::embed_image(const imagecpp_const_image_view &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support image embeddings");
}

std::vector<float> Model::embed_text(const std::string &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support text embeddings");
}

std::vector<ClassificationOutput> Model::classify(const imagecpp_const_image_view &, const std::vector<std::string> &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support image classification");
}

OcrOutput Model::ocr(const imagecpp_const_image_view &, const OcrRequest &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support OCR");
}

TextOutput Model::visual_query(const imagecpp_const_image_view &, const VisualQueryRequest &) {
    throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this model does not support visual text generation");
}
} // namespace imagecpp::detail

extern "C" {

void imagecpp_model_options_init(imagecpp_model_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_model_options), nullptr, 0, IMAGECPP_DEVICE_AUTO};
    }
}

void imagecpp_diffusion_model_options_init(imagecpp_diffusion_model_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_diffusion_model_options),
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    0,
                    IMAGECPP_DEVICE_AUTO,
                    1,
                    0,
                    0};
    }
}

void imagecpp_upscaler_model_options_init(imagecpp_upscaler_model_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_upscaler_model_options), nullptr, 0, IMAGECPP_DEVICE_AUTO, 0};
    }
}

void imagecpp_vlm_model_options_init(imagecpp_vlm_model_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_vlm_model_options), nullptr, nullptr, 0, IMAGECPP_DEVICE_AUTO, 4096};
    }
}

imagecpp_status imagecpp_model_load(const imagecpp_runtime *runtime, const char *operation_id,
                                    const imagecpp_model_options *options, imagecpp_model **output,
                                    imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output model pointer is null");
    }
    *output = nullptr;
    if (runtime == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "runtime is null");
    }
    if (operation_id == nullptr || operation_id[0] == '\0') {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "operation id is empty");
    }
    try {
        std::shared_ptr<imagecpp::detail::Model> implementation;
        if (std::string_view(operation_id) == "image.segment.sam" ||
            std::string_view(operation_id) == "image.detect.sam3") {
#if defined(IMAGECPP_WITH_SAM3)
            const imagecpp_model_options &settings = imagecpp::detail::validate_model_options(options);
            implementation =
                imagecpp::detail::load_sam3_model(settings, std::string_view(operation_id) == "image.detect.sam3");
#else
            (void)options;
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                                        "SAM segmentation support is not compiled in");
#endif
        } else if (std::string_view(operation_id) == "image.depth.depth-anything") {
#if defined(IMAGECPP_WITH_DEPTH_ANYTHING)
            const imagecpp_model_options &settings = imagecpp::detail::validate_model_options(options);
            implementation = imagecpp::detail::load_depth_anything_model(settings);
#else
            (void)options;
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                                        "Depth Anything support is not compiled in");
#endif
        } else if (std::string_view(operation_id) == "image.embed.clip" ||
                   std::string_view(operation_id) == "image.classify.clip") {
#if defined(IMAGECPP_WITH_CLIP)
            const imagecpp_model_options &settings = imagecpp::detail::validate_model_options(options);
            implementation = imagecpp::detail::load_clip_model(settings);
#else
            (void)options;
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "CLIP support is not compiled in");
#endif
        } else if (std::string_view(operation_id) == "image.ocr.tesseract") {
#if defined(IMAGECPP_WITH_TESSERACT)
            const imagecpp_model_options &settings = imagecpp::detail::validate_model_options(options);
            implementation = imagecpp::detail::load_tesseract_model(settings);
#else
            (void)options;
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "Tesseract OCR support is not compiled in");
#endif
        } else {
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "operation has no model loader");
        }
        auto model = std::make_unique<imagecpp_model>();
        model->implementation = std::move(implementation);
        *output = model.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected model load failure");
    }
}

imagecpp_status imagecpp_diffusion_model_load(const imagecpp_runtime *runtime,
                                              const imagecpp_diffusion_model_options *options, imagecpp_model **output,
                                              imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output model pointer is null");
    }
    *output = nullptr;
    if (runtime == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "runtime is null");
    }
#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
    try {
        const imagecpp_diffusion_model_options &settings = imagecpp::detail::validate_diffusion_model_options(options);
        auto model = std::make_unique<imagecpp_model>();
        model->implementation = imagecpp::detail::load_diffusion_model(settings);
        *output = model.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected diffusion model load failure");
    }
#else
    (void)options;
    return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "diffusion support is not compiled in");
#endif
}

imagecpp_status imagecpp_upscaler_model_load(const imagecpp_runtime *runtime,
                                             const imagecpp_upscaler_model_options *options, imagecpp_model **output,
                                             imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output model pointer is null");
    }
    *output = nullptr;
    if (runtime == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "runtime is null");
    }
#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
    try {
        const imagecpp_upscaler_model_options &settings = imagecpp::detail::validate_upscaler_model_options(options);
        auto model = std::make_unique<imagecpp_model>();
        model->implementation = imagecpp::detail::load_upscaler_model(settings);
        *output = model.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected upscaler model load failure");
    }
#else
    (void)options;
    return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                                "model-backed upscaling support is not compiled in");
#endif
}

imagecpp_status imagecpp_vlm_model_load(const imagecpp_runtime *runtime, const imagecpp_vlm_model_options *options,
                                        imagecpp_model **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output VLM model pointer is null");
    }
    *output = nullptr;
    if (runtime == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "runtime is null");
    }
#if defined(IMAGECPP_WITH_VLM)
    try {
        const imagecpp_vlm_model_options &settings = imagecpp::detail::validate_vlm_model_options(options);
        auto model = std::make_unique<imagecpp_model>();
        model->implementation = imagecpp::detail::load_vlm_model(settings);
        *output = model.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected VLM model load failure");
    }
#else
    (void)options;
    return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "VLM support is not compiled in");
#endif
}

void imagecpp_model_destroy(imagecpp_model *model) { delete model; }

imagecpp_status imagecpp_session_create(const imagecpp_model *model, imagecpp_session **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output session pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    try {
        auto session = std::make_unique<imagecpp_session>();
        session->model = model->implementation;
        session->implementation = model->implementation->create_session();
        if (session->implementation == nullptr) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "provider failed to create a session");
        }
        *output = session.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected session creation failure");
    }
}

void imagecpp_session_destroy(imagecpp_session *session) { delete session; }

imagecpp_status imagecpp_session_set_image(imagecpp_session *session, const imagecpp_const_image_view *image,
                                           imagecpp_error *error) {
    if (session == nullptr || session->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "session is null");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        session->implementation->set_image(*image);
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image encoding failure");
    }
}

void imagecpp_segment_options_init(imagecpp_segment_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_segment_options), nullptr, 0, {}, 0, 0};
    }
}

imagecpp_status imagecpp_segment(imagecpp_session *session, const imagecpp_segment_options *options,
                                 imagecpp_segment_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "output segmentation result pointer is null");
    }
    *output = nullptr;
    if (session == nullptr || session->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "session is null");
    }
    try {
        const imagecpp::detail::SegmentRequest request = imagecpp::detail::segment_request(options);
        auto result = std::make_unique<imagecpp_segment_result>();
        result->outputs = session->implementation->segment(request);
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected segmentation failure");
    }
}

size_t imagecpp_segment_result_count(const imagecpp_segment_result *result) {
    return result == nullptr ? 0 : result->outputs.size();
}

imagecpp_status imagecpp_segment_result_info(const imagecpp_segment_result *result, size_t index,
                                             imagecpp_segment_info *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_segment_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "segmentation result or output is null or too small");
    }
    if (index >= result->outputs.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "segmentation result index is out of range");
    }
    const imagecpp::detail::SegmentOutput &item = result->outputs[index];
    *output = {
        sizeof(imagecpp_segment_info),
        {
            sizeof(imagecpp_const_image_view),
            item.mask.data(),
            item.mask.size(),
            item.width,
            item.height,
            item.width,
            IMAGECPP_PIXEL_FORMAT_GRAY_U8,
            IMAGECPP_COLOR_SPACE_UNKNOWN,
        },
        item.box,
        item.score,
        item.iou_score,
    };
    return imagecpp::core::succeed(error);
}

void imagecpp_segment_result_destroy(imagecpp_segment_result *result) { delete result; }

void imagecpp_detect_options_init(imagecpp_detect_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_detect_options), nullptr, nullptr, 0, nullptr, 0, 0.5F, 0.1F};
    }
}

imagecpp_status imagecpp_detect(imagecpp_session *session, const imagecpp_detect_options *options,
                                imagecpp_detection_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output detection pointer is null");
    }
    *output = nullptr;
    if (session == nullptr || session->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "session is null");
    }
    try {
        const imagecpp::detail::DetectRequest request = imagecpp::detail::detect_request(options);
        auto result = std::make_unique<imagecpp_detection_result>();
        result->outputs = session->implementation->detect(request);
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected detection failure");
    }
}

size_t imagecpp_detection_result_count(const imagecpp_detection_result *result) {
    return result == nullptr ? 0 : result->outputs.size();
}

imagecpp_status imagecpp_detection_result_info(const imagecpp_detection_result *result, size_t index,
                                               imagecpp_detection_info *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_detection_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "detection result or output info is null or too small");
    }
    if (index >= result->outputs.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "detection result index is out of range");
    }
    const imagecpp::detail::DetectionOutput &item = result->outputs[index];
    *output = {
        sizeof(imagecpp_detection_info),
        item.label.c_str(),
        item.box,
        {sizeof(imagecpp_const_image_view), item.mask.data(), item.mask.size(), item.width, item.height, item.width,
         IMAGECPP_PIXEL_FORMAT_GRAY_U8, IMAGECPP_COLOR_SPACE_UNKNOWN},
        item.score,
        item.iou_score,
    };
    return imagecpp::core::succeed(error);
}

void imagecpp_detection_result_destroy(imagecpp_detection_result *result) { delete result; }

void imagecpp_generate_options_init(imagecpp_generate_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_generate_options),
                    "",
                    "",
                    512,
                    512,
                    20,
                    7.0F,
                    -1,
                    1,
                    0.75F,
                    IMAGECPP_SAMPLE_METHOD_AUTO,
                    IMAGECPP_SCHEDULER_AUTO,
                    nullptr,
                    nullptr};
    }
}

imagecpp_status imagecpp_generate(const imagecpp_model *model, const imagecpp_generate_options *options,
                                  imagecpp_image_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "output generation result pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    try {
        const imagecpp::detail::GenerateRequest request = imagecpp::detail::generate_request(options);
        imagecpp::detail::ImageLayout initial_layout;
        if (request.init_image != nullptr) {
            const imagecpp_status status =
                imagecpp::detail::validate_const_view(request.init_image, initial_layout, error);
            if (status != IMAGECPP_STATUS_OK) {
                return status;
            }
            if (request.init_image->width != request.width || request.init_image->height != request.height) {
                return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                            "initial image dimensions must match generation dimensions");
            }
        }
        if (request.mask != nullptr) {
            imagecpp::detail::ImageLayout mask_layout;
            const imagecpp_status status = imagecpp::detail::validate_const_view(request.mask, mask_layout, error);
            if (status != IMAGECPP_STATUS_OK) {
                return status;
            }
            if (request.mask->width != request.width || request.mask->height != request.height) {
                return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                            "mask dimensions must match generation dimensions");
            }
        }
        auto result = std::make_unique<imagecpp_image_result>();
        result->outputs = model->implementation->generate(request);
        if (result->outputs.empty()) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "generation returned no images");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image generation failure");
    }
}

imagecpp_status imagecpp_upscale(const imagecpp_model *model, const imagecpp_const_image_view *image, uint32_t factor,
                                 imagecpp_image_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output upscale result pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    if (factor < 2) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "upscale factor must be at least two");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        auto result = std::make_unique<imagecpp_image_result>();
        result->outputs.push_back(model->implementation->upscale(*image, factor));
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image upscale failure");
    }
}

size_t imagecpp_image_result_count(const imagecpp_image_result *result) {
    return result == nullptr ? 0 : result->outputs.size();
}

imagecpp_status imagecpp_image_result_view(const imagecpp_image_result *result, size_t index,
                                           imagecpp_const_image_view *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_const_image_view)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "image result or output view is null or too small");
    }
    if (index >= result->outputs.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "image result index is out of range");
    }
    const imagecpp::detail::ImageOutput &item = result->outputs[index];
    const size_t bytes_per_pixel = imagecpp_pixel_format_bytes_per_pixel(item.pixel_format);
    *output = {sizeof(imagecpp_const_image_view),
               item.data.data(),
               item.data.size(),
               item.width,
               item.height,
               static_cast<size_t>(item.width) * bytes_per_pixel,
               item.pixel_format,
               item.color_space};
    return imagecpp::core::succeed(error);
}

void imagecpp_image_result_destroy(imagecpp_image_result *result) { delete result; }

void imagecpp_depth_options_init(imagecpp_depth_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_depth_options), 0};
    }
}

imagecpp_status imagecpp_depth(const imagecpp_model *model, const imagecpp_const_image_view *image,
                               const imagecpp_depth_options *options, imagecpp_depth_result **output,
                               imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output depth result pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    if (options == nullptr || options->struct_size < sizeof(imagecpp_depth_options)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "depth options are null or too small");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        auto result = std::make_unique<imagecpp_depth_result>();
        result->output = model->implementation->depth(*image, options->include_pose != 0);
        if (result->output.width == 0 || result->output.height == 0 || result->output.depth.empty()) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "depth estimation returned no data");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected depth estimation failure");
    }
}

imagecpp_status imagecpp_depth_result_info(const imagecpp_depth_result *result, imagecpp_depth_info *output,
                                           imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_depth_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "depth result or output info is null or too small");
    }
    const imagecpp::detail::DepthOutput &item = result->output;
    const auto view = [&](const std::vector<float> &values) {
        if (values.empty()) {
            return imagecpp_const_image_view{
                sizeof(imagecpp_const_image_view), nullptr, 0, 0, 0, 0, IMAGECPP_PIXEL_FORMAT_UNKNOWN,
                IMAGECPP_COLOR_SPACE_UNKNOWN};
        }
        return imagecpp_const_image_view{
            sizeof(imagecpp_const_image_view),
            values.data(),
            values.size() * sizeof(float),
            item.width,
            item.height,
            static_cast<size_t>(item.width) * sizeof(float),
            IMAGECPP_PIXEL_FORMAT_GRAY_F32,
            IMAGECPP_COLOR_SPACE_UNKNOWN,
        };
    };
    *output = {sizeof(imagecpp_depth_info),
               view(item.depth),
               view(item.confidence),
               view(item.sky),
               item.is_metric ? 1 : 0,
               item.has_pose ? 1 : 0,
               {},
               {}};
    std::memcpy(output->extrinsics, item.extrinsics, sizeof(item.extrinsics));
    std::memcpy(output->intrinsics, item.intrinsics, sizeof(item.intrinsics));
    return imagecpp::core::succeed(error);
}

void imagecpp_depth_result_destroy(imagecpp_depth_result *result) { delete result; }

void imagecpp_ocr_options_init(imagecpp_ocr_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_ocr_options), IMAGECPP_OCR_PAGE_AUTO, 300, 0};
    }
}

imagecpp_status imagecpp_ocr(const imagecpp_model *model, const imagecpp_const_image_view *image,
                             const imagecpp_ocr_options *options, imagecpp_ocr_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output OCR result pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        const imagecpp::detail::OcrRequest request = imagecpp::detail::ocr_request(options);
        auto result = std::make_unique<imagecpp_ocr_result>();
        result->output = model->implementation->ocr(*image, request);
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected OCR failure");
    }
}

imagecpp_status imagecpp_ocr_result_info(const imagecpp_ocr_result *result, imagecpp_ocr_info *output,
                                         imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_ocr_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "OCR result or output info is null or too small");
    }
    *output = {sizeof(imagecpp_ocr_info), result->output.text.c_str(), result->output.language.c_str(),
               result->output.mean_confidence, result->output.regions.size()};
    return imagecpp::core::succeed(error);
}

size_t imagecpp_ocr_result_region_count(const imagecpp_ocr_result *result) {
    return result == nullptr ? 0 : result->output.regions.size();
}

imagecpp_status imagecpp_ocr_result_region_info(const imagecpp_ocr_result *result, size_t index,
                                                imagecpp_text_region_info *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_text_region_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "OCR result or output region info is null or too small");
    }
    if (index >= result->output.regions.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "OCR region index is out of range");
    }
    const imagecpp::detail::TextRegionOutput &region = result->output.regions[index];
    *output = {sizeof(imagecpp_text_region_info),
               region.level,
               region.text.c_str(),
               region.box,
               region.confidence,
               region.block_index,
               region.paragraph_index,
               region.line_index,
               region.word_index,
               region.block_type,
               region.baseline,
               region.has_baseline ? 1 : 0,
               region.orientation,
               region.writing_direction,
               region.textline_order,
               region.deskew_angle_degrees};
    return imagecpp::core::succeed(error);
}

void imagecpp_ocr_result_destroy(imagecpp_ocr_result *result) { delete result; }

void imagecpp_visual_query_options_init(imagecpp_visual_query_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_visual_query_options), nullptr, 128, 0.1F, 0.9F, 40, 0};
    }
}

imagecpp_status imagecpp_visual_query(const imagecpp_model *model, const imagecpp_const_image_view *image,
                                      const imagecpp_visual_query_options *options, imagecpp_text_result **output,
                                      imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output text result pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        const imagecpp::detail::VisualQueryRequest request = imagecpp::detail::visual_query_request(options);
        auto result = std::make_unique<imagecpp_text_result>();
        result->output = model->implementation->visual_query(*image, request);
        if (result->output.text.empty()) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "visual query returned no text");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected visual query failure");
    }
}

imagecpp_status imagecpp_text_result_info(const imagecpp_text_result *result, imagecpp_text_info *output,
                                          imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_text_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "text result or output info is null or too small");
    }
    *output = {sizeof(imagecpp_text_info), result->output.text.c_str(), result->output.prompt_tokens,
               result->output.generated_tokens, result->output.finish_reason};
    return imagecpp::core::succeed(error);
}

void imagecpp_text_result_destroy(imagecpp_text_result *result) { delete result; }

imagecpp_status imagecpp_embed_image(const imagecpp_model *model, const imagecpp_const_image_view *image,
                                     imagecpp_embedding_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output embedding pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        auto result = std::make_unique<imagecpp_embedding_result>();
        result->values = model->implementation->embed_image(*image);
        if (result->values.empty()) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "image embedding is empty");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image embedding failure");
    }
}

imagecpp_status imagecpp_embed_text(const imagecpp_model *model, const char *text, imagecpp_embedding_result **output,
                                    imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output embedding pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    if (text == nullptr || text[0] == '\0') {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "embedding text is empty");
    }
    try {
        auto result = std::make_unique<imagecpp_embedding_result>();
        result->values = model->implementation->embed_text(text);
        if (result->values.empty()) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR, "text embedding is empty");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected text embedding failure");
    }
}

size_t imagecpp_embedding_result_size(const imagecpp_embedding_result *result) {
    return result == nullptr ? 0 : result->values.size();
}

const float *imagecpp_embedding_result_data(const imagecpp_embedding_result *result) {
    return result == nullptr || result->values.empty() ? nullptr : result->values.data();
}

void imagecpp_embedding_result_destroy(imagecpp_embedding_result *result) { delete result; }

imagecpp_status imagecpp_classify(const imagecpp_model *model, const imagecpp_const_image_view *image,
                                  const char *const *labels, size_t label_count,
                                  imagecpp_classification_result **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output classification pointer is null");
    }
    *output = nullptr;
    if (model == nullptr || model->implementation == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "model is null");
    }
    if (labels == nullptr || label_count == 0) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "classification labels are empty");
    }
    imagecpp::detail::ImageLayout layout;
    const imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        std::vector<std::string> copied_labels;
        copied_labels.reserve(label_count);
        for (size_t index = 0; index < label_count; ++index) {
            if (labels[index] == nullptr || labels[index][0] == '\0') {
                throw std::invalid_argument("classification labels cannot be empty");
            }
            copied_labels.emplace_back(labels[index]);
        }
        auto result = std::make_unique<imagecpp_classification_result>();
        result->outputs = model->implementation->classify(*image, copied_labels);
        if (result->outputs.size() != label_count) {
            throw imagecpp::detail::Failure(IMAGECPP_STATUS_MODEL_ERROR,
                                            "classification returned the wrong number of scores");
        }
        *output = result.release();
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::translate_exception(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected classification failure");
    }
}

size_t imagecpp_classification_result_count(const imagecpp_classification_result *result) {
    return result == nullptr ? 0 : result->outputs.size();
}

imagecpp_status imagecpp_classification_result_info(const imagecpp_classification_result *result, size_t index,
                                                    imagecpp_classification_info *output, imagecpp_error *error) {
    if (result == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_classification_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "classification result or output is null or too small");
    }
    if (index >= result->outputs.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "classification result index is out of range");
    }
    const imagecpp::detail::ClassificationOutput &item = result->outputs[index];
    *output = {sizeof(imagecpp_classification_info), item.label_index, item.label.c_str(), item.score};
    return imagecpp::core::succeed(error);
}

void imagecpp_classification_result_destroy(imagecpp_classification_result *result) { delete result; }

} // extern "C"
