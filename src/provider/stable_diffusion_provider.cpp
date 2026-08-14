#include "model/model.hpp"

#include "stable-diffusion.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace imagecpp::detail {
namespace {

const char *path_or_empty(const char *path) { return path == nullptr ? "" : path; }

int resolved_threads(int32_t requested) {
    if (requested != 0) {
        return requested;
    }
    const unsigned int available = std::thread::hardware_concurrency();
    return static_cast<int>(std::max(1U, available));
}

void validate_provider_device(imagecpp_device device, const char *provider) {
#if defined(IMAGECPP_SD_METAL)
    if (device == IMAGECPP_DEVICE_CPU) {
        throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                      std::string(provider) +
                          " is GPU-backed in this build; configure without Metal for CPU inference");
    }
#else
    if (device == IMAGECPP_DEVICE_GPU) {
        throw Failure(IMAGECPP_STATUS_UNSUPPORTED, std::string(provider) + " has no GPU backend in this build");
    }
#endif
}

struct ProviderImage {
    std::vector<uint8_t> bytes;
    sd_image_t image{};
};

size_t image_byte_size(uint32_t width, uint32_t height, uint32_t channels) {
    if (height != 0 && width > std::numeric_limits<size_t>::max() / height) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image byte size overflows this platform");
    }
    const size_t pixels = static_cast<size_t>(width) * height;
    if (channels != 0 && pixels > std::numeric_limits<size_t>::max() / channels) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image byte size overflows this platform");
    }
    return pixels * channels;
}

ProviderImage convert_image(const imagecpp_const_image_view &source, uint32_t channels) {
    if (channels != 1 && channels != 3) {
        throw Failure(IMAGECPP_STATUS_INTERNAL, "unsupported provider channel conversion");
    }
    if (source.color_space != IMAGECPP_COLOR_SPACE_SRGB && source.color_space != IMAGECPP_COLOR_SPACE_UNKNOWN) {
        throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "stable diffusion providers require sRGB images");
    }
    if (source.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        source.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "image dimensions exceed the provider limit");
    }

    ProviderImage output;
    output.bytes.resize(image_byte_size(source.width, source.height, channels));
    const auto *input = static_cast<const uint8_t *>(source.data);
    for (uint32_t row = 0; row < source.height; ++row) {
        const uint8_t *input_row = input + static_cast<size_t>(row) * source.row_stride;
        uint8_t *output_row = output.bytes.data() + static_cast<size_t>(row) * source.width * channels;
        for (uint32_t column = 0; column < source.width; ++column) {
            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;
            switch (source.pixel_format) {
            case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
                red = green = blue = input_row[column];
                break;
            case IMAGECPP_PIXEL_FORMAT_RGB_U8: {
                const size_t offset = static_cast<size_t>(column) * 3;
                red = input_row[offset];
                green = input_row[offset + 1];
                blue = input_row[offset + 2];
                break;
            }
            case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
            case IMAGECPP_PIXEL_FORMAT_BGRA_U8: {
                const size_t offset = static_cast<size_t>(column) * 4;
                const bool bgra = source.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8;
                red = input_row[offset + (bgra ? 2 : 0)];
                green = input_row[offset + 1];
                blue = input_row[offset + (bgra ? 0 : 2)];
                break;
            }
            default:
                throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                              "stable diffusion providers support GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
            }

            if (channels == 1) {
                output_row[column] = static_cast<uint8_t>((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
            } else {
                const size_t offset = static_cast<size_t>(column) * 3;
                output_row[offset] = red;
                output_row[offset + 1] = green;
                output_row[offset + 2] = blue;
            }
        }
    }
    output.image = {source.width, source.height, channels, output.bytes.data()};
    return output;
}

sample_method_t sample_method(imagecpp_sample_method method) {
    switch (method) {
    case IMAGECPP_SAMPLE_METHOD_AUTO:
        return SAMPLE_METHOD_COUNT;
    case IMAGECPP_SAMPLE_METHOD_EULER:
        return EULER_SAMPLE_METHOD;
    case IMAGECPP_SAMPLE_METHOD_EULER_A:
        return EULER_A_SAMPLE_METHOD;
    case IMAGECPP_SAMPLE_METHOD_DPM_PLUS_PLUS_2M:
        return DPMPP2M_SAMPLE_METHOD;
    case IMAGECPP_SAMPLE_METHOD_LCM:
        return LCM_SAMPLE_METHOD;
    case IMAGECPP_SAMPLE_METHOD_DDIM:
        return DDIM_TRAILING_SAMPLE_METHOD;
    }
    throw Failure(IMAGECPP_STATUS_INVALID_ARGUMENT, "unsupported sample method");
}

scheduler_t scheduler(imagecpp_scheduler value) {
    switch (value) {
    case IMAGECPP_SCHEDULER_AUTO:
        return SCHEDULER_COUNT;
    case IMAGECPP_SCHEDULER_DISCRETE:
        return DISCRETE_SCHEDULER;
    case IMAGECPP_SCHEDULER_KARRAS:
        return KARRAS_SCHEDULER;
    case IMAGECPP_SCHEDULER_EXPONENTIAL:
        return EXPONENTIAL_SCHEDULER;
    case IMAGECPP_SCHEDULER_AYS:
        return AYS_SCHEDULER;
    case IMAGECPP_SCHEDULER_SGM_UNIFORM:
        return SGM_UNIFORM_SCHEDULER;
    case IMAGECPP_SCHEDULER_SIMPLE:
        return SIMPLE_SCHEDULER;
    }
    throw Failure(IMAGECPP_STATUS_INVALID_ARGUMENT, "unsupported scheduler");
}

class ProviderResults final {
  public:
    ProviderResults(sd_image_t *images, int count) : images_(images), count_(count) {}
    ~ProviderResults() {
        if (images_ != nullptr) {
            for (int index = 0; index < count_; ++index) {
                std::free(images_[index].data);
            }
            std::free(images_);
        }
    }

    ProviderResults(const ProviderResults &) = delete;
    ProviderResults &operator=(const ProviderResults &) = delete;

    sd_image_t *get() const { return images_; }

  private:
    sd_image_t *images_ = nullptr;
    int count_ = 0;
};

ImageOutput copy_output(const sd_image_t &image) {
    if (image.data == nullptr || image.width == 0 || image.height == 0 ||
        (image.channel != 1 && image.channel != 3 && image.channel != 4)) {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "provider returned an invalid image");
    }
    const size_t size = image_byte_size(image.width, image.height, image.channel);
    ImageOutput output;
    output.width = image.width;
    output.height = image.height;
    output.pixel_format = image.channel == 1   ? IMAGECPP_PIXEL_FORMAT_GRAY_U8
                          : image.channel == 3 ? IMAGECPP_PIXEL_FORMAT_RGB_U8
                                               : IMAGECPP_PIXEL_FORMAT_RGBA_U8;
    output.color_space = IMAGECPP_COLOR_SPACE_SRGB;
    output.data.assign(image.data, image.data + size);
    return output;
}

class DiffusionModel final : public Model {
  public:
    explicit DiffusionModel(const imagecpp_diffusion_model_options &options) {
        validate_provider_device(options.device, "diffusion");
        sd_ctx_params_t parameters;
        sd_ctx_params_init(&parameters);
        parameters.model_path = path_or_empty(options.model_path);
        parameters.diffusion_model_path = path_or_empty(options.diffusion_model_path);
        parameters.vae_path = path_or_empty(options.vae_path);
        parameters.clip_l_path = path_or_empty(options.clip_l_path);
        parameters.clip_g_path = path_or_empty(options.clip_g_path);
        parameters.t5xxl_path = path_or_empty(options.t5xxl_path);
        parameters.llm_path = path_or_empty(options.llm_path);
        parameters.n_threads = resolved_threads(options.threads);
        parameters.vae_decode_only = false;
        parameters.free_params_immediately = false;
        parameters.flash_attn = options.flash_attention != 0;
        parameters.diffusion_flash_attn = options.flash_attention != 0;
        parameters.keep_clip_on_cpu = options.keep_text_encoder_on_cpu != 0;
        parameters.keep_vae_on_cpu = options.keep_vae_on_cpu != 0;
        context_.reset(new_sd_ctx(&parameters));
        if (context_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load diffusion model and components");
        }
    }

    std::vector<ImageOutput> generate(const GenerateRequest &request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ProviderImage initial;
        ProviderImage mask;
        if (request.init_image != nullptr) {
            initial = convert_image(*request.init_image, 3);
        }
        if (request.mask != nullptr) {
            mask = convert_image(*request.mask, 1);
        }

        sd_img_gen_params_t parameters;
        sd_img_gen_params_init(&parameters);
        parameters.prompt = request.prompt.c_str();
        parameters.negative_prompt = request.negative_prompt.c_str();
        parameters.width = static_cast<int>(request.width);
        parameters.height = static_cast<int>(request.height);
        parameters.sample_params.sample_steps = request.steps;
        parameters.sample_params.guidance.txt_cfg = request.guidance;
        parameters.sample_params.guidance.img_cfg = request.guidance;
        parameters.sample_params.sample_method = sample_method(request.sample_method);
        if (parameters.sample_params.sample_method == SAMPLE_METHOD_COUNT) {
            parameters.sample_params.sample_method = sd_get_default_sample_method(context_.get());
        }
        parameters.sample_params.scheduler = scheduler(request.scheduler);
        if (parameters.sample_params.scheduler == SCHEDULER_COUNT) {
            parameters.sample_params.scheduler =
                sd_get_default_scheduler(context_.get(), parameters.sample_params.sample_method);
        }
        parameters.seed = request.seed;
        parameters.batch_count = request.batch_count;
        parameters.strength = request.strength;
        parameters.init_image = initial.image;
        parameters.mask_image = mask.image;

        ProviderResults provider_results(generate_image(context_.get(), &parameters), request.batch_count);
        if (provider_results.get() == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "diffusion generation failed");
        }
        std::vector<ImageOutput> outputs;
        outputs.reserve(static_cast<size_t>(request.batch_count));
        for (int index = 0; index < request.batch_count; ++index) {
            outputs.push_back(copy_output(provider_results.get()[index]));
        }
        return outputs;
    }

  private:
    struct ContextDeleter {
        void operator()(sd_ctx_t *context) const { free_sd_ctx(context); }
    };

    std::unique_ptr<sd_ctx_t, ContextDeleter> context_;
    std::mutex mutex_;
};

class UpscalerModel final : public Model {
  public:
    explicit UpscalerModel(const imagecpp_upscaler_model_options &options) {
        validate_provider_device(options.device, "ESRGAN upscaler");
        context_.reset(
            new_upscaler_ctx(options.model_path, false, false, resolved_threads(options.threads), options.tile_size));
        if (context_ == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to load ESRGAN upscaler model");
        }
    }

    ImageOutput upscale(const imagecpp_const_image_view &image, uint32_t factor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ProviderImage input = convert_image(image, 3);
        sd_image_t result = ::upscale(context_.get(), input.image, factor);
        if (result.data == nullptr) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "ESRGAN upscale failed");
        }
        std::unique_ptr<uint8_t, decltype(&std::free)> result_data(result.data, &std::free);
        ImageOutput output = copy_output(result);
        return output;
    }

  private:
    struct ContextDeleter {
        void operator()(upscaler_ctx_t *context) const { free_upscaler_ctx(context); }
    };

    std::unique_ptr<upscaler_ctx_t, ContextDeleter> context_;
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<Model> load_diffusion_model(const imagecpp_diffusion_model_options &options) {
    return std::make_shared<DiffusionModel>(options);
}

std::shared_ptr<Model> load_upscaler_model(const imagecpp_upscaler_model_options &options) {
    return std::make_shared<UpscalerModel>(options);
}

} // namespace imagecpp::detail
