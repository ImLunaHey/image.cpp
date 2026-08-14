#include "model/model.hpp"

#include "sam3.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace imagecpp::detail {
namespace {

class SamModel;

class SamSession final : public Session {
  public:
    SamSession(std::shared_ptr<sam3_model> model, const sam3_params &params)
        : model_(std::move(model)), state_(sam3_create_state(*model_, params)) {
        if (state_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "SAM provider failed to allocate inference state");
        }
    }

    void set_image(const imagecpp_const_image_view &image) override {
        if (image.width > static_cast<uint32_t>(INT_MAX) || image.height > static_cast<uint32_t>(INT_MAX)) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image dimensions exceed the SAM provider limit");
        }
        if (image.color_space != IMAGECPP_COLOR_SPACE_SRGB && image.color_space != IMAGECPP_COLOR_SPACE_UNKNOWN) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "SAM segmentation requires an sRGB image");
        }

        sam3_image input;
        input.width = static_cast<int>(image.width);
        input.height = static_cast<int>(image.height);
        input.channels = 3;
        input.data.resize(static_cast<size_t>(image.width) * image.height * 3);
        const auto *source = static_cast<const uint8_t *>(image.data);
        for (uint32_t row = 0; row < image.height; ++row) {
            const uint8_t *source_row = source + static_cast<size_t>(row) * image.row_stride;
            uint8_t *destination = input.data.data() + static_cast<size_t>(row) * image.width * 3;
            switch (image.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                for (uint32_t column = 0; column < image.width; ++column) {
                    destination[column * 3] = source_row[column];
                    destination[column * 3 + 1] = source_row[column];
                    destination[column * 3 + 2] = source_row[column];
                }
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8:
                std::copy_n(source_row, static_cast<size_t>(image.width) * 3, destination);
                break;
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
                for (uint32_t column = 0; column < image.width; ++column) {
                    const size_t source_offset = static_cast<size_t>(column) * 4;
                    const size_t destination_offset = static_cast<size_t>(column) * 3;
                    if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGBA_U8) {
                        destination[destination_offset] = source_row[source_offset];
                        destination[destination_offset + 1] = source_row[source_offset + 1];
                        destination[destination_offset + 2] = source_row[source_offset + 2];
                    } else {
                        destination[destination_offset] = source_row[source_offset + 2];
                        destination[destination_offset + 1] = source_row[source_offset + 1];
                        destination[destination_offset + 2] = source_row[source_offset];
                    }
                }
                break;
            default:
                throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                              "SAM segmentation supports GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
            }
        }

        if (!sam3_encode_image(*state_, *model_, input)) {
            ready_ = false;
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "SAM image encoding failed");
        }
        width_ = image.width;
        height_ = image.height;
        ready_ = true;
    }

    std::vector<SegmentOutput> segment(const SegmentRequest &request) override {
        if (!ready_) {
            throw Failure(IMAGECPP_STATUS_NOT_READY, "session has no encoded image");
        }

        sam3_pvs_params parameters;
        parameters.multimask = request.multimask;
        for (const imagecpp_point_prompt &point : request.points) {
            validate_point(point.x, point.y);
            (point.positive != 0 ? parameters.pos_points : parameters.neg_points).push_back({point.x, point.y});
        }
        if (request.use_box) {
            validate_point(request.box.x0, request.box.y0);
            if (request.box.x1 > static_cast<float>(width_) || request.box.y1 > static_cast<float>(height_)) {
                throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "SAM box extends outside the encoded image");
            }
            parameters.box = {request.box.x0, request.box.y0, request.box.x1, request.box.y1};
            parameters.use_box = true;
        }

        sam3_result provider_result = sam3_segment_pvs(*state_, *model_, parameters);
        std::vector<SegmentOutput> outputs;
        outputs.reserve(provider_result.detections.size());
        for (sam3_detection &detection : provider_result.detections) {
            if (detection.mask.width <= 0 || detection.mask.height <= 0 ||
                detection.mask.data.size() !=
                    static_cast<size_t>(detection.mask.width) * static_cast<size_t>(detection.mask.height)) {
                throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "SAM provider returned an invalid mask");
            }
            outputs.push_back({
                static_cast<uint32_t>(detection.mask.width),
                static_cast<uint32_t>(detection.mask.height),
                std::move(detection.mask.data),
                {detection.box.x0, detection.box.y0, detection.box.x1, detection.box.y1},
                detection.score,
                detection.iou_score,
            });
        }
        return outputs;
    }

  private:
    void validate_point(float x, float y) const {
        if (x < 0.0F || y < 0.0F || x >= static_cast<float>(width_) || y >= static_cast<float>(height_)) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "SAM prompt coordinates are outside the encoded image");
        }
    }

    std::shared_ptr<sam3_model> model_;
    sam3_state_ptr state_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool ready_ = false;
};

class SamModel final : public Model {
  public:
    explicit SamModel(const imagecpp_model_options &options) {
#if !defined(GGML_USE_METAL)
        if (options.device == IMAGECPP_DEVICE_GPU) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this SAM provider build has no GPU backend");
        }
#endif
        parameters_.model_path = options.model_path;
        parameters_.n_threads = options.threads == 0
                                    ? static_cast<int>(std::max(1U, std::thread::hardware_concurrency()))
                                    : options.threads;
        parameters_.use_gpu = options.device != IMAGECPP_DEVICE_CPU;
        model_ = sam3_load_model(parameters_);
        if (model_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load SAM model; check its path and format");
        }
    }

    ~SamModel() override {
        if (model_ != nullptr) {
            sam3_free_model(*model_);
        }
    }

    std::unique_ptr<Session> create_session() override { return std::make_unique<SamSession>(model_, parameters_); }

  private:
    sam3_params parameters_;
    std::shared_ptr<sam3_model> model_;
};

} // namespace

std::shared_ptr<Model> load_sam3_model(const imagecpp_model_options &options) {
    return std::make_shared<SamModel>(options);
}

} // namespace imagecpp::detail
