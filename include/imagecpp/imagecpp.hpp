#ifndef IMAGECPP_IMAGECPP_HPP
#define IMAGECPP_IMAGECPP_HPP

#include "imagecpp/imagecpp.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace imagecpp {

class Error final : public std::runtime_error {
  public:
    Error(imagecpp_status status, const std::string &message) : std::runtime_error(message), status_(status) {}

    imagecpp_status status() const noexcept { return status_; }

  private:
    imagecpp_status status_;
};

inline void check(imagecpp_status status, const imagecpp_error &error) {
    if (status != IMAGECPP_STATUS_OK) {
        const char *message = error.message[0] != '\0' ? error.message : imagecpp_status_string(status);
        throw Error(status, message);
    }
}

class Image final {
  public:
    explicit Image(const imagecpp_image_desc &desc) {
        imagecpp_error error{};
        check(imagecpp_image_create(&desc, &handle_, &error), error);
    }

    ~Image() { imagecpp_image_destroy(handle_); }

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    Image(Image &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Image &operator=(Image &&other) noexcept {
        if (this != &other) {
            imagecpp_image_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    imagecpp_image_view view() {
        imagecpp_image_view result{};
        result.struct_size = sizeof(result);
        imagecpp_error error{};
        check(imagecpp_image_get_view(handle_, &result, &error), error);
        return result;
    }

    imagecpp_const_image_view view() const {
        imagecpp_const_image_view result{};
        result.struct_size = sizeof(result);
        imagecpp_error error{};
        check(imagecpp_image_get_const_view(handle_, &result, &error), error);
        return result;
    }

    imagecpp_image *get() noexcept { return handle_; }
    const imagecpp_image *get() const noexcept { return handle_; }

  private:
    struct AdoptTag {};

    Image(imagecpp_image *handle, AdoptTag) noexcept : handle_(handle) {}

    friend Image decode(const void *, size_t, imagecpp_file_format, const imagecpp_decode_options *);
    friend Image load(const std::string &, const imagecpp_decode_options *);

    imagecpp_image *handle_ = nullptr;
};

class Blob final {
  public:
    ~Blob() { imagecpp_blob_destroy(handle_); }

    Blob(const Blob &) = delete;
    Blob &operator=(const Blob &) = delete;

    Blob(Blob &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Blob &operator=(Blob &&other) noexcept {
        if (this != &other) {
            imagecpp_blob_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    const void *data() const noexcept { return imagecpp_blob_data(handle_); }
    size_t size() const noexcept { return imagecpp_blob_size(handle_); }

    std::vector<uint8_t> bytes() const {
        const auto *begin = static_cast<const uint8_t *>(data());
        return begin == nullptr ? std::vector<uint8_t>{} : std::vector<uint8_t>(begin, begin + size());
    }

  private:
    explicit Blob(imagecpp_blob *handle) noexcept : handle_(handle) {}

    friend Blob encode(const imagecpp_const_image_view &, imagecpp_file_format, const imagecpp_encode_options *);

    imagecpp_blob *handle_ = nullptr;
};

inline Image decode(const void *data, size_t size, imagecpp_file_format format = IMAGECPP_FILE_FORMAT_AUTO,
                    const imagecpp_decode_options *options = nullptr) {
    imagecpp_image *handle = nullptr;
    imagecpp_error error{};
    check(imagecpp_image_decode(data, size, format, options, &handle, &error), error);
    return Image(handle, Image::AdoptTag{});
}

inline Image decode(const std::vector<uint8_t> &bytes, imagecpp_file_format format = IMAGECPP_FILE_FORMAT_AUTO,
                    const imagecpp_decode_options *options = nullptr) {
    return decode(bytes.data(), bytes.size(), format, options);
}

inline Image load(const std::string &filename, const imagecpp_decode_options *options = nullptr) {
    imagecpp_image *handle = nullptr;
    imagecpp_error error{};
    check(imagecpp_image_load(filename.c_str(), options, &handle, &error), error);
    return Image(handle, Image::AdoptTag{});
}

inline Blob encode(const imagecpp_const_image_view &image, imagecpp_file_format format,
                   const imagecpp_encode_options *options = nullptr) {
    imagecpp_blob *handle = nullptr;
    imagecpp_error error{};
    check(imagecpp_image_encode(&image, format, options, &handle, &error), error);
    return Blob(handle);
}

inline Blob encode(const imagecpp_image_view &image, imagecpp_file_format format,
                   const imagecpp_encode_options *options = nullptr) {
    const imagecpp_const_image_view const_image{
        sizeof(imagecpp_const_image_view),
        image.data,
        image.data_size,
        image.width,
        image.height,
        image.row_stride,
        image.pixel_format,
        image.color_space,
    };
    return encode(const_image, format, options);
}

inline Blob encode(const Image &image, imagecpp_file_format format, const imagecpp_encode_options *options = nullptr) {
    return encode(image.view(), format, options);
}

inline void save(const std::string &filename, const imagecpp_const_image_view &image,
                 imagecpp_file_format format = IMAGECPP_FILE_FORMAT_AUTO,
                 const imagecpp_encode_options *options = nullptr) {
    imagecpp_error error{};
    check(imagecpp_image_save(filename.c_str(), &image, format, options, &error), error);
}

inline void save(const std::string &filename, const imagecpp_image_view &image,
                 imagecpp_file_format format = IMAGECPP_FILE_FORMAT_AUTO,
                 const imagecpp_encode_options *options = nullptr) {
    const imagecpp_const_image_view const_image{
        sizeof(imagecpp_const_image_view),
        image.data,
        image.data_size,
        image.width,
        image.height,
        image.row_stride,
        image.pixel_format,
        image.color_space,
    };
    save(filename, const_image, format, options);
}

inline void save(const std::string &filename, const Image &image,
                 imagecpp_file_format format = IMAGECPP_FILE_FORMAT_AUTO,
                 const imagecpp_encode_options *options = nullptr) {
    save(filename, image.view(), format, options);
}

struct OperationInfo {
    std::string id;
    std::string name;
    std::string description;
    imagecpp_task task{};
    imagecpp_artifact_kind input_kind{};
    imagecpp_artifact_kind output_kind{};
};

class Runtime final {
  public:
    Runtime() {
        imagecpp_error error{};
        check(imagecpp_runtime_create(&handle_, &error), error);
    }

    ~Runtime() { imagecpp_runtime_destroy(handle_); }

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    Runtime(Runtime &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Runtime &operator=(Runtime &&other) noexcept {
        if (this != &other) {
            imagecpp_runtime_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    std::vector<OperationInfo> operations() const {
        std::vector<OperationInfo> result;
        const size_t count = imagecpp_runtime_operation_count(handle_);
        result.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            imagecpp_operation_info info{};
            info.struct_size = sizeof(info);
            imagecpp_error error{};
            check(imagecpp_runtime_operation_info(handle_, index, &info, &error), error);
            result.push_back({info.id, info.name, info.description, info.task, info.input_kind, info.output_kind});
        }
        return result;
    }

    imagecpp_runtime *get() noexcept { return handle_; }
    const imagecpp_runtime *get() const noexcept { return handle_; }

  private:
    imagecpp_runtime *handle_ = nullptr;
};

class Model final {
  public:
    Model(const Runtime &runtime, const std::string &operation_id, const imagecpp_model_options &options) {
        imagecpp_error error{};
        check(imagecpp_model_load(runtime.get(), operation_id.c_str(), &options, &handle_, &error), error);
    }

    Model(const Runtime &runtime, const imagecpp_diffusion_model_options &options) {
        imagecpp_error error{};
        check(imagecpp_diffusion_model_load(runtime.get(), &options, &handle_, &error), error);
    }

    Model(const Runtime &runtime, const imagecpp_upscaler_model_options &options) {
        imagecpp_error error{};
        check(imagecpp_upscaler_model_load(runtime.get(), &options, &handle_, &error), error);
    }

    ~Model() { imagecpp_model_destroy(handle_); }

    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    Model(Model &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Model &operator=(Model &&other) noexcept {
        if (this != &other) {
            imagecpp_model_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    imagecpp_model *get() noexcept { return handle_; }
    const imagecpp_model *get() const noexcept { return handle_; }

  private:
    imagecpp_model *handle_ = nullptr;
};

class EmbeddingResult final {
  public:
    ~EmbeddingResult() { imagecpp_embedding_result_destroy(handle_); }

    EmbeddingResult(const EmbeddingResult &) = delete;
    EmbeddingResult &operator=(const EmbeddingResult &) = delete;
    EmbeddingResult(EmbeddingResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    EmbeddingResult &operator=(EmbeddingResult &&other) noexcept {
        if (this != &other) {
            imagecpp_embedding_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    size_t size() const noexcept { return imagecpp_embedding_result_size(handle_); }
    const float *data() const noexcept { return imagecpp_embedding_result_data(handle_); }
    std::vector<float> values() const {
        const float *begin = data();
        return begin == nullptr ? std::vector<float>{} : std::vector<float>(begin, begin + size());
    }

  private:
    explicit EmbeddingResult(imagecpp_embedding_result *handle) noexcept : handle_(handle) {}
    friend EmbeddingResult embed_image(const Model &, const imagecpp_const_image_view &);
    friend EmbeddingResult embed_text(const Model &, const std::string &);
    imagecpp_embedding_result *handle_ = nullptr;
};

inline EmbeddingResult embed_image(const Model &model, const imagecpp_const_image_view &image) {
    imagecpp_embedding_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_embed_image(model.get(), &image, &result, &error), error);
    return EmbeddingResult(result);
}

inline EmbeddingResult embed_image(const Model &model, const Image &image) { return embed_image(model, image.view()); }

inline EmbeddingResult embed_text(const Model &model, const std::string &text) {
    imagecpp_embedding_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_embed_text(model.get(), text.c_str(), &result, &error), error);
    return EmbeddingResult(result);
}

struct ClassificationInfo {
    size_t label_index = 0;
    std::string label;
    float score = 0.0F;
};

class ClassificationResult final {
  public:
    ~ClassificationResult() { imagecpp_classification_result_destroy(handle_); }

    ClassificationResult(const ClassificationResult &) = delete;
    ClassificationResult &operator=(const ClassificationResult &) = delete;
    ClassificationResult(ClassificationResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    ClassificationResult &operator=(ClassificationResult &&other) noexcept {
        if (this != &other) {
            imagecpp_classification_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    size_t size() const noexcept { return imagecpp_classification_result_count(handle_); }

    ClassificationInfo at(size_t index) const {
        imagecpp_classification_info info{};
        info.struct_size = sizeof(info);
        imagecpp_error error{};
        check(imagecpp_classification_result_info(handle_, index, &info, &error), error);
        return {info.label_index, info.label == nullptr ? "" : info.label, info.score};
    }

  private:
    explicit ClassificationResult(imagecpp_classification_result *handle) noexcept : handle_(handle) {}
    friend ClassificationResult classify(const Model &, const imagecpp_const_image_view &,
                                         const std::vector<std::string> &);
    imagecpp_classification_result *handle_ = nullptr;
};

inline ClassificationResult classify(const Model &model, const imagecpp_const_image_view &image,
                                     const std::vector<std::string> &labels) {
    std::vector<const char *> label_pointers;
    label_pointers.reserve(labels.size());
    for (const std::string &label : labels) {
        label_pointers.push_back(label.c_str());
    }
    imagecpp_classification_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_classify(model.get(), &image, label_pointers.data(), label_pointers.size(), &result, &error), error);
    return ClassificationResult(result);
}

inline ClassificationResult classify(const Model &model, const Image &image, const std::vector<std::string> &labels) {
    return classify(model, image.view(), labels);
}

class ImageResult final {
  public:
    ~ImageResult() { imagecpp_image_result_destroy(handle_); }

    ImageResult(const ImageResult &) = delete;
    ImageResult &operator=(const ImageResult &) = delete;

    ImageResult(ImageResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    ImageResult &operator=(ImageResult &&other) noexcept {
        if (this != &other) {
            imagecpp_image_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    size_t size() const noexcept { return imagecpp_image_result_count(handle_); }

    imagecpp_const_image_view at(size_t index) const {
        imagecpp_const_image_view view{};
        view.struct_size = sizeof(view);
        imagecpp_error error{};
        check(imagecpp_image_result_view(handle_, index, &view, &error), error);
        return view;
    }

  private:
    explicit ImageResult(imagecpp_image_result *handle) noexcept : handle_(handle) {}

    friend ImageResult generate(const Model &, const imagecpp_generate_options &);
    friend ImageResult upscale(const Model &, const imagecpp_const_image_view &, uint32_t);

    imagecpp_image_result *handle_ = nullptr;
};

inline ImageResult generate(const Model &model, const imagecpp_generate_options &options) {
    imagecpp_image_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_generate(model.get(), &options, &result, &error), error);
    return ImageResult(result);
}

inline ImageResult upscale(const Model &model, const imagecpp_const_image_view &image, uint32_t factor) {
    imagecpp_image_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_upscale(model.get(), &image, factor, &result, &error), error);
    return ImageResult(result);
}

inline ImageResult upscale(const Model &model, const Image &image, uint32_t factor) {
    return upscale(model, image.view(), factor);
}

class DepthResult final {
  public:
    ~DepthResult() { imagecpp_depth_result_destroy(handle_); }

    DepthResult(const DepthResult &) = delete;
    DepthResult &operator=(const DepthResult &) = delete;

    DepthResult(DepthResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    DepthResult &operator=(DepthResult &&other) noexcept {
        if (this != &other) {
            imagecpp_depth_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    imagecpp_depth_info info() const {
        imagecpp_depth_info result{};
        result.struct_size = sizeof(result);
        imagecpp_error error{};
        check(imagecpp_depth_result_info(handle_, &result, &error), error);
        return result;
    }

  private:
    explicit DepthResult(imagecpp_depth_result *handle) noexcept : handle_(handle) {}

    friend DepthResult depth(const Model &, const imagecpp_const_image_view &, const imagecpp_depth_options &);

    imagecpp_depth_result *handle_ = nullptr;
};

inline DepthResult depth(const Model &model, const imagecpp_const_image_view &image,
                         const imagecpp_depth_options &options) {
    imagecpp_depth_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_depth(model.get(), &image, &options, &result, &error), error);
    return DepthResult(result);
}

inline DepthResult depth(const Model &model, const Image &image, const imagecpp_depth_options &options) {
    return depth(model, image.view(), options);
}

struct SegmentInfo {
    imagecpp_const_image_view mask{};
    imagecpp_box box{};
    float score = 0.0F;
    float iou_score = 0.0F;
};

class SegmentResult final {
  public:
    ~SegmentResult() { imagecpp_segment_result_destroy(handle_); }

    SegmentResult(const SegmentResult &) = delete;
    SegmentResult &operator=(const SegmentResult &) = delete;

    SegmentResult(SegmentResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    SegmentResult &operator=(SegmentResult &&other) noexcept {
        if (this != &other) {
            imagecpp_segment_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    size_t size() const noexcept { return imagecpp_segment_result_count(handle_); }

    SegmentInfo at(size_t index) const {
        imagecpp_segment_info info{};
        info.struct_size = sizeof(info);
        imagecpp_error error{};
        check(imagecpp_segment_result_info(handle_, index, &info, &error), error);
        return {info.mask, info.box, info.score, info.iou_score};
    }

  private:
    explicit SegmentResult(imagecpp_segment_result *handle) noexcept : handle_(handle) {}

    friend class Session;

    imagecpp_segment_result *handle_ = nullptr;
};

struct DetectionInfo {
    std::string label;
    imagecpp_box box{};
    imagecpp_const_image_view mask{};
    float score = 0.0F;
    float iou_score = 0.0F;
};

class DetectionResult final {
  public:
    ~DetectionResult() { imagecpp_detection_result_destroy(handle_); }

    DetectionResult(const DetectionResult &) = delete;
    DetectionResult &operator=(const DetectionResult &) = delete;

    DetectionResult(DetectionResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    DetectionResult &operator=(DetectionResult &&other) noexcept {
        if (this != &other) {
            imagecpp_detection_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    size_t size() const noexcept { return imagecpp_detection_result_count(handle_); }

    DetectionInfo at(size_t index) const {
        imagecpp_detection_info info{};
        info.struct_size = sizeof(info);
        imagecpp_error error{};
        check(imagecpp_detection_result_info(handle_, index, &info, &error), error);
        return {info.label == nullptr ? "" : info.label, info.box, info.mask, info.score, info.iou_score};
    }

  private:
    explicit DetectionResult(imagecpp_detection_result *handle) noexcept : handle_(handle) {}

    friend class Session;

    imagecpp_detection_result *handle_ = nullptr;
};

class Session final {
  public:
    explicit Session(const Model &model) {
        imagecpp_error error{};
        check(imagecpp_session_create(model.get(), &handle_, &error), error);
    }

    ~Session() { imagecpp_session_destroy(handle_); }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    Session(Session &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Session &operator=(Session &&other) noexcept {
        if (this != &other) {
            imagecpp_session_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    void set_image(const imagecpp_const_image_view &image) {
        imagecpp_error error{};
        check(imagecpp_session_set_image(handle_, &image, &error), error);
    }

    void set_image(const Image &image) { set_image(image.view()); }

    SegmentResult segment(const imagecpp_segment_options &options) {
        imagecpp_segment_result *result = nullptr;
        imagecpp_error error{};
        check(imagecpp_segment(handle_, &options, &result, &error), error);
        return SegmentResult(result);
    }

    DetectionResult detect(const imagecpp_detect_options &options) {
        imagecpp_detection_result *result = nullptr;
        imagecpp_error error{};
        check(imagecpp_detect(handle_, &options, &result, &error), error);
        return DetectionResult(result);
    }

    imagecpp_session *get() noexcept { return handle_; }
    const imagecpp_session *get() const noexcept { return handle_; }

  private:
    imagecpp_session *handle_ = nullptr;
};

struct CutoutInfo {
    imagecpp_const_image_view image{};
    imagecpp_const_image_view mask{};
    imagecpp_box source_box{};
    size_t selected_mask_index = 0;
    float score = 0.0F;
    float iou_score = 0.0F;
};

class CutoutResult final {
  public:
    ~CutoutResult() { imagecpp_cutout_result_destroy(handle_); }

    CutoutResult(const CutoutResult &) = delete;
    CutoutResult &operator=(const CutoutResult &) = delete;

    CutoutResult(CutoutResult &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    CutoutResult &operator=(CutoutResult &&other) noexcept {
        if (this != &other) {
            imagecpp_cutout_result_destroy(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    CutoutInfo info() const {
        imagecpp_cutout_info result{};
        result.struct_size = sizeof(result);
        imagecpp_error error{};
        check(imagecpp_cutout_result_info(handle_, &result, &error), error);
        return {result.image, result.mask,     result.source_box, result.selected_mask_index,
                result.score, result.iou_score};
    }

  private:
    explicit CutoutResult(imagecpp_cutout_result *handle) noexcept : handle_(handle) {}

    friend CutoutResult cutout(Session &, const Model *, const imagecpp_const_image_view &,
                               const imagecpp_cutout_options &);

    imagecpp_cutout_result *handle_ = nullptr;
};

inline CutoutResult cutout(Session &segment_session, const Model *upscaler_model,
                           const imagecpp_const_image_view &image, const imagecpp_cutout_options &options) {
    imagecpp_cutout_result *result = nullptr;
    imagecpp_error error{};
    check(imagecpp_cutout(segment_session.get(), upscaler_model == nullptr ? nullptr : upscaler_model->get(), &image,
                          &options, &result, &error),
          error);
    return CutoutResult(result);
}

inline CutoutResult cutout(Session &segment_session, const Model *upscaler_model, const Image &image,
                           const imagecpp_cutout_options &options) {
    return cutout(segment_session, upscaler_model, image.view(), options);
}

inline void resize(const imagecpp_const_image_view &source, const imagecpp_image_view &destination,
                   imagecpp_resize_filter filter) {
    imagecpp_error error{};
    check(imagecpp_resize(&source, &destination, filter, &error), error);
}

} // namespace imagecpp

#endif
