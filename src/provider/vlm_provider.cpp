#include "model/model.hpp"

#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imagecpp::detail {
namespace {

void quiet_log(enum ggml_log_level, const char *, void *) {}

void initialize_backend() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(quiet_log, nullptr);
        mtmd_log_set(quiet_log, nullptr);
        llama_backend_init();
    });
}

int resolved_threads(int32_t requested) {
    if (requested != 0) {
        return requested;
    }
    return static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
}

std::vector<uint8_t> rgb_pixels(const imagecpp_const_image_view &image) {
    if (image.color_space != IMAGECPP_COLOR_SPACE_SRGB && image.color_space != IMAGECPP_COLOR_SPACE_UNKNOWN) {
        throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "visual language models require sRGB images");
    }
    const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
    if (image.height != 0 && pixel_count / image.height != image.width) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "visual query image dimensions overflow this platform");
    }
    if (pixel_count > std::numeric_limits<size_t>::max() / 3) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "visual query image size overflows this platform");
    }
    std::vector<uint8_t> output(pixel_count * 3);
    const auto *source = static_cast<const uint8_t *>(image.data);
    for (uint32_t row = 0; row < image.height; ++row) {
        const uint8_t *source_row = source + static_cast<size_t>(row) * image.row_stride;
        uint8_t *target_row = output.data() + static_cast<size_t>(row) * image.width * 3;
        for (uint32_t column = 0; column < image.width; ++column) {
            const size_t target = static_cast<size_t>(column) * 3;
            switch (image.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                target_row[target] = source_row[column];
                target_row[target + 1] = source_row[column];
                target_row[target + 2] = source_row[column];
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8: {
                const size_t input = static_cast<size_t>(column) * 3;
                std::memcpy(target_row + target, source_row + input, 3);
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8: {
                const size_t input = static_cast<size_t>(column) * 4;
                const bool bgra = image.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8;
                target_row[target] = source_row[input + (bgra ? 2 : 0)];
                target_row[target + 1] = source_row[input + 1];
                target_row[target + 2] = source_row[input + (bgra ? 0 : 2)];
                break;
            }
            default:
                throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                              "visual queries support GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
            }
        }
    }
    return output;
}

std::string format_prompt(const llama_model *model, const std::string &prompt) {
    const char *chat_template = llama_model_chat_template(model, nullptr);
    if (chat_template == nullptr || chat_template[0] == '\0') {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "VLM language model does not contain a supported chat template");
    }
    const std::string content = std::string(mtmd_default_marker()) + "\n" + prompt;
    const llama_chat_message message{"user", content.c_str()};
    int32_t required = llama_chat_apply_template(chat_template, &message, 1, true, nullptr, 0);
    if (required <= 0) {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to apply the VLM chat template");
    }
    std::vector<char> buffer(static_cast<size_t>(required) + 1);
    const int32_t written = llama_chat_apply_template(chat_template, &message, 1, true, buffer.data(), required + 1);
    if (written < 0 || written > required) {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to format the VLM prompt");
    }
    return std::string(buffer.data(), static_cast<size_t>(written));
}

std::string token_piece(const llama_vocab *vocab, llama_token token) {
    std::vector<char> buffer(64);
    int32_t written = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, false);
    if (written < 0) {
        buffer.resize(static_cast<size_t>(-written));
        written = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, false);
    }
    if (written < 0) {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to decode generated VLM token");
    }
    return std::string(buffer.data(), static_cast<size_t>(written));
}

struct ModelDeleter {
    void operator()(llama_model *value) const { llama_model_free(value); }
};

struct ContextDeleter {
    void operator()(llama_context *value) const { llama_free(value); }
};

struct MtmdDeleter {
    void operator()(mtmd_context *value) const { mtmd_free(value); }
};

struct BitmapDeleter {
    void operator()(mtmd_bitmap *value) const { mtmd_bitmap_free(value); }
};

struct ChunksDeleter {
    void operator()(mtmd_input_chunks *value) const { mtmd_input_chunks_free(value); }
};

struct SamplerDeleter {
    void operator()(llama_sampler *value) const { llama_sampler_free(value); }
};

class Batch final {
  public:
    Batch() : value_(llama_batch_init(1, 0, 1)) {}
    ~Batch() { llama_batch_free(value_); }

    Batch(const Batch &) = delete;
    Batch &operator=(const Batch &) = delete;

    llama_batch next(llama_token token, llama_pos position) {
        value_.n_tokens = 1;
        value_.token[0] = token;
        value_.pos[0] = position;
        value_.n_seq_id[0] = 1;
        value_.seq_id[0][0] = 0;
        value_.logits[0] = 1;
        return value_;
    }

  private:
    llama_batch value_{};
};

class VlmModel final : public Model {
  public:
    explicit VlmModel(const imagecpp_vlm_model_options &options)
        : threads_(resolved_threads(options.threads)), context_size_(options.context_size) {
        initialize_backend();
        if (options.device == IMAGECPP_DEVICE_GPU && !llama_supports_gpu_offload()) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "this VLM build has no GPU backend");
        }

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = options.device == IMAGECPP_DEVICE_CPU ? 0 : -1;
        model_.reset(llama_model_load_from_file(options.model_path, model_params));
        if (!model_) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load VLM language model");
        }
        if (!llama_model_has_decoder(model_.get())) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "VLM language model has no decoder");
        }

        llama_context_params context_params = llama_context_default_params();
        context_params.n_ctx = context_size_;
        context_params.n_batch = std::min<uint32_t>(context_size_, 512);
        context_params.n_ubatch = context_params.n_batch;
        context_params.n_threads = threads_;
        context_params.n_threads_batch = threads_;
        context_.reset(llama_init_from_model(model_.get(), context_params));
        if (!context_) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to create VLM language context");
        }

        mtmd_context_params projection_params = mtmd_context_params_default();
        projection_params.use_gpu = options.device != IMAGECPP_DEVICE_CPU;
        projection_params.print_timings = false;
        projection_params.n_threads = threads_;
        projection_params.warmup = false;
        projection_.reset(mtmd_init_from_file(options.projection_model_path, model_.get(), projection_params));
        if (!projection_) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load VLM projection model");
        }
        if (!mtmd_support_vision(projection_.get())) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "VLM projection model does not support images");
        }
        (void)format_prompt(model_.get(), "Describe this image.");
    }

    TextOutput visual_query(const imagecpp_const_image_view &image, const VisualQueryRequest &request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        llama_memory_clear(llama_get_memory(context_.get()), true);

        std::vector<uint8_t> pixels = rgb_pixels(image);
        std::unique_ptr<mtmd_bitmap, BitmapDeleter> bitmap(mtmd_bitmap_init(image.width, image.height, pixels.data()));
        if (!bitmap) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_MEMORY, "failed to allocate VLM image input");
        }

        const std::string prompt = format_prompt(model_.get(), request.prompt);
        const mtmd_input_text input{prompt.data(), prompt.size(), true, true};
        std::unique_ptr<mtmd_input_chunks, ChunksDeleter> chunks(mtmd_input_chunks_init());
        if (!chunks) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_MEMORY, "failed to allocate VLM prompt input");
        }
        const mtmd_bitmap *bitmaps[] = {bitmap.get()};
        const int32_t tokenize_status = mtmd_tokenize(projection_.get(), chunks.get(), &input, bitmaps, 1);
        if (tokenize_status != 0) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to tokenize VLM image prompt");
        }

        TextOutput output;
        output.prompt_tokens = mtmd_helper_get_n_tokens(chunks.get());
        const llama_pos prompt_positions = mtmd_helper_get_n_pos(chunks.get());
        if (prompt_positions < 0 || static_cast<uint64_t>(prompt_positions) + request.max_tokens > context_size_) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE,
                          "VLM image prompt and requested output exceed the configured context size");
        }

        llama_pos n_past = 0;
        const int32_t eval_status = mtmd_helper_eval_chunks(projection_.get(), context_.get(), chunks.get(), 0, 0,
                                                            std::min<int32_t>(512, context_size_), true, &n_past);
        if (eval_status != 0) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to evaluate VLM image prompt");
        }

        std::unique_ptr<llama_sampler, SamplerDeleter> sampler(
            llama_sampler_chain_init(llama_sampler_chain_default_params()));
        if (!sampler) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_MEMORY, "failed to allocate VLM sampler");
        }
        if (request.temperature <= 0.0F) {
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());
        } else {
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(request.top_k));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(request.top_p, 1));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(request.temperature));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(request.seed));
        }

        const llama_vocab *vocab = llama_model_get_vocab(model_.get());
        Batch batch;
        output.finish_reason = IMAGECPP_TEXT_FINISH_LENGTH;
        for (uint32_t index = 0; index < request.max_tokens; ++index) {
            const llama_token token = llama_sampler_sample(sampler.get(), context_.get(), -1);
            llama_sampler_accept(sampler.get(), token);
            if (llama_vocab_is_eog(vocab, token)) {
                output.finish_reason = IMAGECPP_TEXT_FINISH_END_OF_GENERATION;
                break;
            }
            output.text += token_piece(vocab, token);
            ++output.generated_tokens;
            if (index + 1 == request.max_tokens) {
                break;
            }
            if (llama_decode(context_.get(), batch.next(token, n_past)) != 0) {
                throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to evaluate generated VLM token");
            }
            ++n_past;
        }
        return output;
    }

  private:
    int threads_ = 1;
    uint32_t context_size_ = 4096;
    std::unique_ptr<llama_model, ModelDeleter> model_;
    std::unique_ptr<llama_context, ContextDeleter> context_;
    std::unique_ptr<mtmd_context, MtmdDeleter> projection_;
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<Model> load_vlm_model(const imagecpp_vlm_model_options &options) {
    return std::make_shared<VlmModel>(options);
}

} // namespace imagecpp::detail
