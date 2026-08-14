#include "model/model.hpp"

#include "core/status.hpp"
#include "image/layout.hpp"

#include <cmath>
#include <cstring>
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

#if defined(IMAGECPP_WITH_SAM3)
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

} // namespace
} // namespace imagecpp::detail

extern "C" {

void imagecpp_model_options_init(imagecpp_model_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_model_options), nullptr, 0, IMAGECPP_DEVICE_AUTO};
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
        if (std::string_view(operation_id) == "image.segment.sam") {
#if defined(IMAGECPP_WITH_SAM3)
            const imagecpp_model_options &settings = imagecpp::detail::validate_model_options(options);
            implementation = imagecpp::detail::load_sam3_model(settings);
#else
            (void)options;
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED,
                                        "SAM segmentation support is not compiled in");
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

} // extern "C"
