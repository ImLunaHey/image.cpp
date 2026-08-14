#include "model/model.hpp"

#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace imagecpp::detail {
namespace {

int resolved_threads(int32_t requested) {
    if (requested != 0) {
        return requested;
    }
    return static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
}

da::Image convert_image(const imagecpp_const_image_view &source) {
    if (source.color_space != IMAGECPP_COLOR_SPACE_SRGB && source.color_space != IMAGECPP_COLOR_SPACE_UNKNOWN) {
        throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "Depth Anything requires an sRGB image");
    }
    if (source.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        source.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image dimensions exceed the Depth Anything limit");
    }
    if (source.height != 0 && source.width > std::numeric_limits<size_t>::max() / source.height / 3U) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image byte size overflows this platform");
    }

    da::Image output;
    output.w = static_cast<int>(source.width);
    output.h = static_cast<int>(source.height);
    output.rgb.resize(static_cast<size_t>(source.width) * source.height * 3U);
    const auto *input = static_cast<const uint8_t *>(source.data);
    for (uint32_t row = 0; row < source.height; ++row) {
        const uint8_t *input_row = input + static_cast<size_t>(row) * source.row_stride;
        uint8_t *output_row = output.rgb.data() + static_cast<size_t>(row) * source.width * 3U;
        for (uint32_t column = 0; column < source.width; ++column) {
            const size_t output_offset = static_cast<size_t>(column) * 3U;
            switch (source.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                output_row[output_offset] = input_row[column];
                output_row[output_offset + 1] = input_row[column];
                output_row[output_offset + 2] = input_row[column];
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8: {
                const size_t input_offset = static_cast<size_t>(column) * 3U;
                std::copy_n(input_row + input_offset, 3, output_row + output_offset);
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8: {
                const size_t input_offset = static_cast<size_t>(column) * 4U;
                const bool bgra = source.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8;
                output_row[output_offset] = input_row[input_offset + (bgra ? 2U : 0U)];
                output_row[output_offset + 1] = input_row[input_offset + 1U];
                output_row[output_offset + 2] = input_row[input_offset + (bgra ? 0U : 2U)];
                break;
            }
            default:
                throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                              "Depth Anything supports GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
            }
        }
    }
    return output;
}

bool metric_model(const da::Config &config) {
    if (config.head_max_depth > 0.0F) {
        return true;
    }
    std::string name = config.checkpoint_name;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return name.find("metric") != std::string::npos || name.find("nested") != std::string::npos ||
           name.find("mono") != std::string::npos;
}

class DepthAnythingModel final : public Model {
  public:
    explicit DepthAnythingModel(const imagecpp_model_options &options) {
        if (options.device != IMAGECPP_DEVICE_AUTO) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                          "Depth Anything currently selects the best compiled device automatically");
        }
        engine_ = da::Engine::load(options.model_path, resolved_threads(options.threads));
        if (engine_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load Depth Anything model");
        }
    }

    DepthOutput depth(const imagecpp_const_image_view &image, bool include_pose) override {
        std::lock_guard<std::mutex> lock(mutex_);
        da::Image input = convert_image(image);
        std::vector<float> depth_values;
        std::vector<float> secondary;
        std::array<float, 12> extrinsics{};
        std::array<float, 9> intrinsics{};
        int height = 0;
        int width = 0;
        bool succeeded = false;
        bool has_pose = false;

        if (engine_->is_da2()) {
            succeeded = engine_->depth_relative(input, depth_values, height, width);
        } else if (engine_->is_mono()) {
            succeeded = engine_->depth_mono(input, depth_values, secondary, height, width);
        } else if (include_pose) {
            succeeded =
                engine_->depth_pose_native(input, depth_values, secondary, extrinsics, intrinsics, height, width);
            has_pose = succeeded;
        } else {
            succeeded = engine_->depth_native_image(input, depth_values, secondary, height, width);
        }
        if (!succeeded || width <= 0 || height <= 0 || depth_values.size() != static_cast<size_t>(width) * height) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "Depth Anything inference failed");
        }
        if (!secondary.empty() && secondary.size() != depth_values.size()) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "Depth Anything returned an invalid auxiliary map");
        }

        DepthOutput output;
        output.width = static_cast<uint32_t>(width);
        output.height = static_cast<uint32_t>(height);
        output.depth = std::move(depth_values);
        if (engine_->is_mono()) {
            output.sky = std::move(secondary);
        } else {
            output.confidence = std::move(secondary);
        }
        output.is_metric = metric_model(engine_->config());
        output.has_pose = has_pose;
        std::copy(extrinsics.begin(), extrinsics.end(), output.extrinsics);
        std::copy(intrinsics.begin(), intrinsics.end(), output.intrinsics);
        return output;
    }

  private:
    std::unique_ptr<da::Engine> engine_;
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<Model> load_depth_anything_model(const imagecpp_model_options &options) {
    return std::make_shared<DepthAnythingModel>(options);
}

} // namespace imagecpp::detail
