#include "imagecpp/imagecpp.h"

#include "core/status.hpp"

#include <new>
#include <string>
#include <vector>

namespace {

struct Operation {
    std::string id;
    std::string name;
    std::string description;
    imagecpp_task task;
    imagecpp_artifact_kind input_kind;
    imagecpp_artifact_kind output_kind;
};

} // namespace

struct imagecpp_runtime {
    std::vector<Operation> operations;
};

extern "C" {

imagecpp_status imagecpp_runtime_create(imagecpp_runtime **output, imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output runtime pointer is null");
    }
    *output = nullptr;
    try {
        auto *runtime = new imagecpp_runtime;
        runtime->operations.push_back({
            "image.resize",
            "Resize image",
            "Resize an image with nearest-neighbor or bilinear sampling",
            IMAGECPP_TASK_IMAGE_UTILITY,
            IMAGECPP_ARTIFACT_IMAGE,
            IMAGECPP_ARTIFACT_IMAGE,
        });
#if defined(IMAGECPP_WITH_SAM3)
        runtime->operations.push_back({
            "image.segment.sam",
            "Segment with SAM",
            "Point- and box-prompted segmentation with SAM 2, SAM 3, or EdgeTAM",
            IMAGECPP_TASK_SEGMENT,
            IMAGECPP_ARTIFACT_IMAGE,
            IMAGECPP_ARTIFACT_MASK,
        });
#endif
#if defined(IMAGECPP_WITH_DEPTH_ANYTHING)
        runtime->operations.push_back({
            "image.depth.depth-anything",
            "Estimate depth with Depth Anything",
            "Dense relative or metric depth estimation with optional confidence and camera pose",
            IMAGECPP_TASK_DEPTH,
            IMAGECPP_ARTIFACT_IMAGE,
            IMAGECPP_ARTIFACT_DEPTH_MAP,
        });
#endif
#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
        runtime->operations.push_back({
            "image.generate.diffusion",
            "Generate with diffusion",
            "Text-to-image generation with Stable Diffusion, Flux, and compatible diffusion families",
            IMAGECPP_TASK_GENERATE,
            IMAGECPP_ARTIFACT_METADATA,
            IMAGECPP_ARTIFACT_IMAGE,
        });
        runtime->operations.push_back({
            "image.edit.diffusion",
            "Edit with diffusion",
            "Image-to-image editing and mask-guided inpainting with compatible diffusion models",
            IMAGECPP_TASK_EDIT,
            IMAGECPP_ARTIFACT_IMAGE,
            IMAGECPP_ARTIFACT_IMAGE,
        });
        runtime->operations.push_back({
            "image.upscale.esrgan",
            "Upscale with ESRGAN",
            "Model-backed image upscaling with ESRGAN-family weights",
            IMAGECPP_TASK_UPSCALE,
            IMAGECPP_ARTIFACT_IMAGE,
            IMAGECPP_ARTIFACT_IMAGE,
        });
#endif
        *output = runtime;
        return imagecpp::core::succeed(error);
    } catch (const std::bad_alloc &) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "failed to allocate runtime");
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected failure while creating runtime");
    }
}

void imagecpp_runtime_destroy(imagecpp_runtime *runtime) { delete runtime; }

size_t imagecpp_runtime_operation_count(const imagecpp_runtime *runtime) {
    return runtime == nullptr ? 0 : runtime->operations.size();
}

imagecpp_status imagecpp_runtime_operation_info(const imagecpp_runtime *runtime, size_t index,
                                                imagecpp_operation_info *output, imagecpp_error *error) {
    if (runtime == nullptr || output == nullptr || output->struct_size < sizeof(imagecpp_operation_info)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "runtime or operation output is null or too small");
    }
    if (index >= runtime->operations.size()) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "operation index is out of range");
    }
    const Operation &operation = runtime->operations[index];
    *output = {
        sizeof(imagecpp_operation_info), operation.id.c_str(), operation.name.c_str(),
        operation.description.c_str(),   operation.task,       operation.input_kind,
        operation.output_kind,
    };
    return imagecpp::core::succeed(error);
}

} // extern "C"
