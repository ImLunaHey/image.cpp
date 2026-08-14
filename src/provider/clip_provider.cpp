#include "model/model.hpp"

#include "clip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

namespace imagecpp::detail {
namespace {

struct ClipContextDeleter {
    void operator()(clip_ctx *context) const noexcept {
        if (context != nullptr) {
            clip_free(context);
        }
    }
};

struct ClipImageDeleter {
    void operator()(clip_image_f32 *image) const noexcept {
        if (image != nullptr) {
            clip_image_f32_free(image);
        }
    }
};

float linear_to_srgb(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

uint8_t byte_from_float(float value, bool linear) {
    if (!std::isfinite(value)) {
        value = 0.0F;
    }
    if (linear) {
        value = linear_to_srgb(value);
    }
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

float read_float(const uint8_t *data) {
    float value = 0.0F;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::string normalize_text(const std::string &text) {
    std::string result;
    result.reserve(text.size());
    bool pending_space = false;
    for (unsigned char character : text) {
        if (std::isspace(character) != 0) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
            pending_space = false;
        }
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

std::vector<uint8_t> convert_to_rgb(const imagecpp_const_image_view &image) {
    if (image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("CLIP image dimensions exceed the provider limit");
    }
    const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
    if (pixel_count > std::numeric_limits<size_t>::max() / 3) {
        throw std::bad_alloc();
    }
    std::vector<uint8_t> output(pixel_count * 3);
    const auto *bytes = static_cast<const uint8_t *>(image.data);
    const bool linear = image.color_space == IMAGECPP_COLOR_SPACE_LINEAR_SRGB;

    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t *row = bytes + static_cast<size_t>(y) * image.row_stride;
        for (uint32_t x = 0; x < image.width; ++x) {
            uint8_t *destination = output.data() + (static_cast<size_t>(y) * image.width + x) * 3;
            switch (image.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8: {
                const float value = static_cast<float>(row[x]) / 255.0F;
                const uint8_t converted = linear ? byte_from_float(value, true) : row[x];
                destination[0] = converted;
                destination[1] = converted;
                destination[2] = converted;
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_RGB_U8:
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8: {
                const size_t channels = image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGB_U8 ? 3 : 4;
                for (size_t channel = 0; channel < 3; ++channel) {
                    const uint8_t value = row[static_cast<size_t>(x) * channels + channel];
                    destination[channel] = linear ? byte_from_float(static_cast<float>(value) / 255.0F, true) : value;
                }
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8: {
                const uint8_t *source = row + static_cast<size_t>(x) * 4;
                destination[0] = linear ? byte_from_float(static_cast<float>(source[2]) / 255.0F, true) : source[2];
                destination[1] = linear ? byte_from_float(static_cast<float>(source[1]) / 255.0F, true) : source[1];
                destination[2] = linear ? byte_from_float(static_cast<float>(source[0]) / 255.0F, true) : source[0];
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_GRAY_F32:
            case IMAGECPP_PIXEL_FORMAT_RGB_F32:
            case IMAGECPP_PIXEL_FORMAT_RGBA_F32: {
                const size_t channels = image.pixel_format == IMAGECPP_PIXEL_FORMAT_GRAY_F32
                                            ? 1
                                            : (image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGB_F32 ? 3 : 4);
                for (size_t channel = 0; channel < 3; ++channel) {
                    const size_t source_channel = channels == 1 ? 0 : channel;
                    const size_t offset = (static_cast<size_t>(x) * channels + source_channel) * sizeof(float);
                    destination[channel] = byte_from_float(read_float(row + offset), linear);
                }
                break;
            }
            default:
                throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "CLIP cannot convert this pixel format to RGB");
            }
        }
    }
    return output;
}

class ClipModel final : public Model {
  public:
    explicit ClipModel(const imagecpp_model_options &options) {
        if (options.device == IMAGECPP_DEVICE_GPU) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "CLIP GPU execution is not available yet; use auto or CPU");
        }
        const unsigned detected_threads = std::thread::hardware_concurrency();
        threads_ =
            options.threads > 0 ? options.threads : static_cast<int>(detected_threads == 0 ? 4 : detected_threads);
        context_.reset(clip_model_load(options.model_path, 0));
        if (context_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load the CLIP GGUF model");
        }
        const clip_text_hparams *text = clip_get_text_hparams(context_.get());
        const clip_vision_hparams *vision = clip_get_vision_hparams(context_.get());
        if (text == nullptr || vision == nullptr || text->projection_dim <= 0 ||
            text->projection_dim != vision->projection_dim) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "CLIP model must contain compatible text and vision towers");
        }
        dimension_ = text->projection_dim;
        context_length_ = text->num_positions;
    }

    std::vector<float> embed_image(const imagecpp_const_image_view &image) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return embed_image_unlocked(image);
    }

    std::vector<float> embed_text(const std::string &text) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return embed_text_unlocked(text);
    }

    std::vector<ClassificationOutput> classify(const imagecpp_const_image_view &image,
                                               const std::vector<std::string> &labels) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::vector<float> image_embedding = embed_image_unlocked(image);
        std::vector<float> logits(labels.size());
        for (size_t index = 0; index < labels.size(); ++index) {
            const std::vector<float> text_embedding = embed_text_unlocked("a photo of a " + labels[index]);
            logits[index] = 100.0F * std::inner_product(image_embedding.begin(), image_embedding.end(),
                                                        text_embedding.begin(), 0.0F);
        }

        const float maximum = *std::max_element(logits.begin(), logits.end());
        double total = 0.0;
        for (float &logit : logits) {
            logit = std::exp(logit - maximum);
            total += logit;
        }
        std::vector<ClassificationOutput> result;
        result.reserve(labels.size());
        for (size_t index = 0; index < labels.size(); ++index) {
            result.push_back({index, labels[index], static_cast<float>(logits[index] / total)});
        }
        std::stable_sort(result.begin(), result.end(),
                         [](const ClassificationOutput &left, const ClassificationOutput &right) {
                             return left.score > right.score;
                         });
        return result;
    }

  private:
    std::vector<float> embed_image_unlocked(const imagecpp_const_image_view &image) {
        std::vector<uint8_t> rgb = convert_to_rgb(image);
        clip_image_u8 input{static_cast<int>(image.width), static_cast<int>(image.height), rgb.data(), rgb.size()};
        std::unique_ptr<clip_image_f32, ClipImageDeleter> processed(clip_image_f32_make());
        if (processed == nullptr || !clip_image_preprocess(context_.get(), &input, processed.get())) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "CLIP image preprocessing failed");
        }
        std::vector<float> result(static_cast<size_t>(dimension_));
        if (!clip_image_encode(context_.get(), threads_, processed.get(), result.data(), true)) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "CLIP vision encoder failed");
        }
        return result;
    }

    std::vector<float> embed_text_unlocked(const std::string &text) {
        const std::string normalized = normalize_text(text);
        if (normalized.empty()) {
            throw std::invalid_argument("CLIP text is empty after whitespace normalization");
        }
        clip_tokens tokens{};
        if (!clip_tokenize(context_.get(), normalized.c_str(), &tokens)) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "CLIP tokenizer failed");
        }
        std::unique_ptr<clip_vocab_id[]> owned_tokens(tokens.data);
        if (tokens.size == 0 || tokens.size > static_cast<size_t>(context_length_)) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "CLIP text exceeds the model context length");
        }
        std::vector<float> result(static_cast<size_t>(dimension_));
        if (!clip_text_encode(context_.get(), threads_, &tokens, result.data(), true)) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "CLIP text encoder failed");
        }
        return result;
    }

    std::unique_ptr<clip_ctx, ClipContextDeleter> context_;
    std::mutex mutex_;
    int threads_ = 4;
    int dimension_ = 0;
    int context_length_ = 0;
};

} // namespace

std::shared_ptr<Model> load_clip_model(const imagecpp_model_options &options) {
    return std::make_shared<ClipModel>(options);
}

} // namespace imagecpp::detail
