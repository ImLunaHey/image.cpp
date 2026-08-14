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
    imagecpp_image *handle_ = nullptr;
};

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

  private:
    imagecpp_runtime *handle_ = nullptr;
};

inline void resize(const imagecpp_const_image_view &source, const imagecpp_image_view &destination,
                   imagecpp_resize_filter filter) {
    imagecpp_error error{};
    check(imagecpp_resize(&source, &destination, filter, &error), error);
}

} // namespace imagecpp

#endif
