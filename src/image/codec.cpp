#include "imagecpp/imagecpp.h"

#include "core/status.hpp"
#include "image/layout.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define STBI_FAILURE_USERMSG
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PIC
#define STBI_NO_PSD
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(IMAGECPP_WITH_WEBP)
#include <webp/decode.h>
#include <webp/encode.h>
#endif

struct imagecpp_blob {
    std::vector<uint8_t> bytes;
};

namespace imagecpp::detail {
namespace {

struct DecodeSettings {
    imagecpp_pixel_format pixel_format = IMAGECPP_PIXEL_FORMAT_UNKNOWN;
};

struct EncodeSettings {
    int quality = 90;
    bool lossless = true;
};

DecodeSettings decode_settings(const imagecpp_decode_options *options) {
    if (options == nullptr) {
        return {};
    }
    if (options->struct_size < sizeof(imagecpp_decode_options)) {
        throw std::invalid_argument("decode options struct_size is too small");
    }
    return {options->pixel_format};
}

EncodeSettings encode_settings(const imagecpp_encode_options *options) {
    if (options == nullptr) {
        return {};
    }
    if (options->struct_size < sizeof(imagecpp_encode_options)) {
        throw std::invalid_argument("encode options struct_size is too small");
    }
    if (options->quality < 1 || options->quality > 100) {
        throw std::invalid_argument("encode quality must be between 1 and 100");
    }
    return {options->quality, options->lossless != 0};
}

int requested_channels(imagecpp_pixel_format format) {
    switch (format) {
    case IMAGECPP_PIXEL_FORMAT_UNKNOWN:
        return 0;
    case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
        return 1;
    case IMAGECPP_PIXEL_FORMAT_RGB_U8:
        return 3;
    case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
        return 4;
    default:
        throw std::invalid_argument("decode supports only UNKNOWN, GRAY_U8, RGB_U8, or RGBA_U8 output");
    }
}

imagecpp_pixel_format format_for_channels(int channels) {
    switch (channels) {
    case 1:
        return IMAGECPP_PIXEL_FORMAT_GRAY_U8;
    case 3:
        return IMAGECPP_PIXEL_FORMAT_RGB_U8;
    case 4:
        return IMAGECPP_PIXEL_FORMAT_RGBA_U8;
    default:
        throw std::runtime_error("decoded image has an unsupported channel count");
    }
}

bool is_webp(const uint8_t *data, size_t size) {
    return size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0;
}

std::string lowercase_extension(std::string_view filename) {
    const size_t separator = filename.find_last_of("/\\");
    const size_t dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    std::string extension(filename.substr(dot + 1));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

imagecpp_file_format format_from_filename(std::string_view filename) {
    const std::string extension = lowercase_extension(filename);
    if (extension == "png") {
        return IMAGECPP_FILE_FORMAT_PNG;
    }
    if (extension == "jpg" || extension == "jpeg") {
        return IMAGECPP_FILE_FORMAT_JPEG;
    }
    if (extension == "webp") {
        return IMAGECPP_FILE_FORMAT_WEBP;
    }
    if (extension == "bmp") {
        return IMAGECPP_FILE_FORMAT_BMP;
    }
    if (extension == "tga") {
        return IMAGECPP_FILE_FORMAT_TGA;
    }
    return IMAGECPP_FILE_FORMAT_AUTO;
}

std::vector<uint8_t> read_file(const char *filename) {
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open image file");
    }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("image file is too large");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("failed to read image file");
    }
    return bytes;
}

void write_file(const char *filename, const std::vector<uint8_t> &bytes) {
    std::ofstream output(filename, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open output image file");
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("failed to write output image file");
    }
}

imagecpp_image *copy_decoded_image(const uint8_t *pixels, uint32_t width, uint32_t height,
                                   imagecpp_pixel_format format) {
    imagecpp_image_desc desc{
        sizeof(imagecpp_image_desc), width, height, 0, format, IMAGECPP_COLOR_SPACE_SRGB,
    };
    imagecpp_image *image = nullptr;
    imagecpp_error error{};
    const imagecpp_status status = imagecpp_image_create(&desc, &image, &error);
    if (status != IMAGECPP_STATUS_OK) {
        throw std::runtime_error(error.message);
    }
    imagecpp_image_view view{};
    view.struct_size = sizeof(view);
    if (imagecpp_image_get_view(image, &view, &error) != IMAGECPP_STATUS_OK) {
        imagecpp_image_destroy(image);
        throw std::runtime_error(error.message);
    }
    const size_t row_bytes = static_cast<size_t>(width) * imagecpp_pixel_format_bytes_per_pixel(format);
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(static_cast<uint8_t *>(view.data) + static_cast<size_t>(row) * view.row_stride,
                    pixels + static_cast<size_t>(row) * row_bytes, row_bytes);
    }
    return image;
}

imagecpp_image *decode_stb(const uint8_t *data, size_t size, const DecodeSettings &settings) {
    if (size > static_cast<size_t>(INT_MAX)) {
        throw std::runtime_error("encoded image exceeds the decoder size limit");
    }
    int width = 0;
    int height = 0;
    int source_channels = 0;
    if (stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &source_channels) == 0) {
        const char *reason = stbi_failure_reason();
        throw std::runtime_error(reason != nullptr ? reason : "unrecognized image data");
    }
    int channels = requested_channels(settings.pixel_format);
    if (channels == 0) {
        channels = source_channels == 1 ? 1 : (source_channels == 3 ? 3 : 4);
    }
    stbi_uc *pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &source_channels, channels);
    if (pixels == nullptr) {
        const char *reason = stbi_failure_reason();
        throw std::runtime_error(reason != nullptr ? reason : "failed to decode image");
    }
    imagecpp_image *image = nullptr;
    try {
        image = copy_decoded_image(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                   format_for_channels(channels));
    } catch (...) {
        stbi_image_free(pixels);
        throw;
    }
    stbi_image_free(pixels);
    return image;
}

#if defined(IMAGECPP_WITH_WEBP)
imagecpp_image *decode_webp(const uint8_t *data, size_t size, const DecodeSettings &settings) {
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK || features.width <= 0 || features.height <= 0) {
        throw std::runtime_error("invalid WebP image data");
    }
    int channels = requested_channels(settings.pixel_format);
    if (channels == 0) {
        channels = features.has_alpha != 0 ? 4 : 3;
    }
    const bool grayscale = channels == 1;
    const int decoded_channels = grayscale ? 3 : channels;
    if (decoded_channels != 3 && decoded_channels != 4) {
        throw std::invalid_argument("WebP decoding supports GRAY_U8, RGB_U8, or RGBA_U8 output");
    }
    const size_t row_bytes = static_cast<size_t>(features.width) * static_cast<size_t>(decoded_channels);
    if (row_bytes > std::numeric_limits<size_t>::max() / static_cast<size_t>(features.height)) {
        throw std::runtime_error("decoded WebP image is too large");
    }
    std::vector<uint8_t> pixels(row_bytes * static_cast<size_t>(features.height));
    uint8_t *decoded = decoded_channels == 4
                           ? WebPDecodeRGBAInto(data, size, pixels.data(), pixels.size(), static_cast<int>(row_bytes))
                           : WebPDecodeRGBInto(data, size, pixels.data(), pixels.size(), static_cast<int>(row_bytes));
    if (decoded == nullptr) {
        throw std::runtime_error("failed to decode WebP image");
    }
    if (grayscale) {
        std::vector<uint8_t> gray(static_cast<size_t>(features.width) * static_cast<size_t>(features.height));
        for (size_t source = 0, destination = 0; destination < gray.size(); source += 3, ++destination) {
            const unsigned value = 77U * pixels[source] + 150U * pixels[source + 1] + 29U * pixels[source + 2];
            gray[destination] = static_cast<uint8_t>((value + 128U) >> 8U);
        }
        return copy_decoded_image(gray.data(), static_cast<uint32_t>(features.width),
                                  static_cast<uint32_t>(features.height), IMAGECPP_PIXEL_FORMAT_GRAY_U8);
    }
    return copy_decoded_image(pixels.data(), static_cast<uint32_t>(features.width),
                              static_cast<uint32_t>(features.height), format_for_channels(decoded_channels));
}
#endif

std::vector<uint8_t> pack_pixels(const imagecpp_const_image_view &image, const ImageLayout &layout, int &channels) {
    switch (image.pixel_format) {
    case IMAGECPP_PIXEL_FORMAT_GRAY_U8:
        channels = 1;
        break;
    case IMAGECPP_PIXEL_FORMAT_RGB_U8:
        channels = 3;
        break;
    case IMAGECPP_PIXEL_FORMAT_RGBA_U8:
    case IMAGECPP_PIXEL_FORMAT_BGRA_U8:
        channels = 4;
        break;
    default:
        throw std::invalid_argument("encoding supports only 8-bit gray, RGB, RGBA, or BGRA images");
    }
    std::vector<uint8_t> packed(layout.row_bytes * static_cast<size_t>(image.height));
    const auto *source = static_cast<const uint8_t *>(image.data);
    for (uint32_t row = 0; row < image.height; ++row) {
        uint8_t *destination = packed.data() + static_cast<size_t>(row) * layout.row_bytes;
        const uint8_t *source_row = source + static_cast<size_t>(row) * layout.row_stride;
        std::memcpy(destination, source_row, layout.row_bytes);
        if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8) {
            for (uint32_t column = 0; column < image.width; ++column) {
                std::swap(destination[static_cast<size_t>(column) * 4],
                          destination[static_cast<size_t>(column) * 4 + 2]);
            }
        }
    }
    return packed;
}

struct StbWriter {
    std::vector<uint8_t> *bytes;
    bool failed = false;
};

void stb_write_callback(void *context, void *data, int size) {
    auto &writer = *static_cast<StbWriter *>(context);
    if (writer.failed || size <= 0) {
        return;
    }
    try {
        const auto *begin = static_cast<const uint8_t *>(data);
        writer.bytes->insert(writer.bytes->end(), begin, begin + size);
    } catch (...) {
        writer.failed = true;
    }
}

void encode_stb(imagecpp_blob &blob, const imagecpp_const_image_view &image, const ImageLayout &layout,
                imagecpp_file_format format, const EncodeSettings &settings) {
    int channels = 0;
    std::vector<uint8_t> packed = pack_pixels(image, layout, channels);
    StbWriter writer{&blob.bytes};
    int result = 0;
    switch (format) {
    case IMAGECPP_FILE_FORMAT_PNG:
        result = stbi_write_png_to_func(stb_write_callback, &writer, static_cast<int>(image.width),
                                        static_cast<int>(image.height), channels, packed.data(),
                                        static_cast<int>(layout.row_bytes));
        break;
    case IMAGECPP_FILE_FORMAT_JPEG:
        if (channels == 4) {
            std::vector<uint8_t> rgb(static_cast<size_t>(image.width) * image.height * 3);
            for (size_t source = 0, destination = 0; source < packed.size(); source += 4, destination += 3) {
                std::memcpy(rgb.data() + destination, packed.data() + source, 3);
            }
            packed = std::move(rgb);
            channels = 3;
        }
        result = stbi_write_jpg_to_func(stb_write_callback, &writer, static_cast<int>(image.width),
                                        static_cast<int>(image.height), channels, packed.data(), settings.quality);
        break;
    case IMAGECPP_FILE_FORMAT_BMP:
        result = stbi_write_bmp_to_func(stb_write_callback, &writer, static_cast<int>(image.width),
                                        static_cast<int>(image.height), channels, packed.data());
        break;
    case IMAGECPP_FILE_FORMAT_TGA:
        result = stbi_write_tga_to_func(stb_write_callback, &writer, static_cast<int>(image.width),
                                        static_cast<int>(image.height), channels, packed.data());
        break;
    default:
        throw std::invalid_argument("format is not handled by the built-in encoder");
    }
    if (result == 0 || writer.failed) {
        throw std::runtime_error("failed to encode image");
    }
}

#if defined(IMAGECPP_WITH_WEBP)
void encode_webp(imagecpp_blob &blob, const imagecpp_const_image_view &image, const ImageLayout &layout,
                 const EncodeSettings &settings) {
    int channels = 0;
    std::vector<uint8_t> packed = pack_pixels(image, layout, channels);
    if (channels == 1) {
        std::vector<uint8_t> rgb(static_cast<size_t>(image.width) * image.height * 3);
        for (size_t source = 0, destination = 0; source < packed.size(); ++source, destination += 3) {
            rgb[destination] = packed[source];
            rgb[destination + 1] = packed[source];
            rgb[destination + 2] = packed[source];
        }
        packed = std::move(rgb);
        channels = 3;
    }

    uint8_t *encoded = nullptr;
    size_t encoded_size = 0;
    const int stride = static_cast<int>(static_cast<size_t>(image.width) * static_cast<size_t>(channels));
    if (channels == 4) {
        encoded_size = settings.lossless ? WebPEncodeLosslessRGBA(packed.data(), static_cast<int>(image.width),
                                                                  static_cast<int>(image.height), stride, &encoded)
                                         : WebPEncodeRGBA(packed.data(), static_cast<int>(image.width),
                                                          static_cast<int>(image.height), stride,
                                                          static_cast<float>(settings.quality), &encoded);
    } else {
        encoded_size = settings.lossless
                           ? WebPEncodeLosslessRGB(packed.data(), static_cast<int>(image.width),
                                                   static_cast<int>(image.height), stride, &encoded)
                           : WebPEncodeRGB(packed.data(), static_cast<int>(image.width), static_cast<int>(image.height),
                                           stride, static_cast<float>(settings.quality), &encoded);
    }
    if (encoded_size == 0 || encoded == nullptr) {
        throw std::runtime_error("failed to encode WebP image");
    }
    try {
        blob.bytes.assign(encoded, encoded + encoded_size);
    } catch (...) {
        WebPFree(encoded);
        throw;
    }
    WebPFree(encoded);
}
#endif

imagecpp_status exception_status(imagecpp_error *error, const std::exception &exception) noexcept {
    if (dynamic_cast<const std::bad_alloc *>(&exception) != nullptr) {
        return core::fail(error, IMAGECPP_STATUS_OUT_OF_MEMORY, "image codec allocation failed");
    }
    if (dynamic_cast<const std::invalid_argument *>(&exception) != nullptr) {
        return core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, exception.what());
    }
    return core::fail(error, IMAGECPP_STATUS_INTERNAL, exception.what());
}

} // namespace
} // namespace imagecpp::detail

extern "C" {

void imagecpp_decode_options_init(imagecpp_decode_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_decode_options), IMAGECPP_PIXEL_FORMAT_UNKNOWN};
    }
}

void imagecpp_encode_options_init(imagecpp_encode_options *options) {
    if (options != nullptr) {
        *options = {sizeof(imagecpp_encode_options), 90, 1};
    }
}

imagecpp_status imagecpp_image_decode(const void *data, size_t data_size, imagecpp_file_format format,
                                      const imagecpp_decode_options *options, imagecpp_image **output,
                                      imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output image pointer is null");
    }
    *output = nullptr;
    if (data == nullptr || data_size == 0) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "encoded image data is empty");
    }
    try {
        const auto settings = imagecpp::detail::decode_settings(options);
        const auto *bytes = static_cast<const uint8_t *>(data);
        const bool webp = format == IMAGECPP_FILE_FORMAT_WEBP ||
                          (format == IMAGECPP_FILE_FORMAT_AUTO && imagecpp::detail::is_webp(bytes, data_size));
        if (webp) {
#if defined(IMAGECPP_WITH_WEBP)
            *output = imagecpp::detail::decode_webp(bytes, data_size, settings);
#else
            return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "WebP support is not compiled in");
#endif
        } else {
            *output = imagecpp::detail::decode_stb(bytes, data_size, settings);
        }
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::exception_status(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image decode failure");
    }
}

imagecpp_status imagecpp_image_load(const char *filename, const imagecpp_decode_options *options,
                                    imagecpp_image **output, imagecpp_error *error) {
    if (filename == nullptr || filename[0] == '\0') {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image filename is empty");
    }
    try {
        const std::vector<uint8_t> bytes = imagecpp::detail::read_file(filename);
        return imagecpp_image_decode(bytes.data(), bytes.size(), IMAGECPP_FILE_FORMAT_AUTO, options, output, error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::exception_status(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image load failure");
    }
}

imagecpp_status imagecpp_image_encode(const imagecpp_const_image_view *image, imagecpp_file_format format,
                                      const imagecpp_encode_options *options, imagecpp_blob **output,
                                      imagecpp_error *error) {
    if (output == nullptr) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "output blob pointer is null");
    }
    *output = nullptr;
    imagecpp::detail::ImageLayout layout;
    imagecpp_status status = imagecpp::detail::validate_const_view(image, layout, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    if (image->width > static_cast<uint32_t>(INT_MAX) || image->height > static_cast<uint32_t>(INT_MAX)) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_OUT_OF_RANGE, "image exceeds encoder dimensions");
    }
    if (format == IMAGECPP_FILE_FORMAT_AUTO) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                    "memory encoding requires an explicit file format");
    }
    try {
        const auto settings = imagecpp::detail::encode_settings(options);
        auto *blob = new imagecpp_blob;
        try {
            if (format == IMAGECPP_FILE_FORMAT_WEBP) {
#if defined(IMAGECPP_WITH_WEBP)
                imagecpp::detail::encode_webp(*blob, *image, layout, settings);
#else
                delete blob;
                return imagecpp::core::fail(error, IMAGECPP_STATUS_UNSUPPORTED, "WebP support is not compiled in");
#endif
            } else {
                imagecpp::detail::encode_stb(*blob, *image, layout, format, settings);
            }
        } catch (...) {
            delete blob;
            throw;
        }
        *output = blob;
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        return imagecpp::detail::exception_status(error, exception);
    } catch (...) {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image encode failure");
    }
}

imagecpp_status imagecpp_image_save(const char *filename, const imagecpp_const_image_view *image,
                                    imagecpp_file_format format, const imagecpp_encode_options *options,
                                    imagecpp_error *error) {
    if (filename == nullptr || filename[0] == '\0') {
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT, "image filename is empty");
    }
    if (format == IMAGECPP_FILE_FORMAT_AUTO) {
        format = imagecpp::detail::format_from_filename(filename);
        if (format == IMAGECPP_FILE_FORMAT_AUTO) {
            return imagecpp::core::fail(error, IMAGECPP_STATUS_INVALID_ARGUMENT,
                                        "cannot infer output image format from filename");
        }
    }
    imagecpp_blob *blob = nullptr;
    imagecpp_status status = imagecpp_image_encode(image, format, options, &blob, error);
    if (status != IMAGECPP_STATUS_OK) {
        return status;
    }
    try {
        imagecpp::detail::write_file(filename, blob->bytes);
        imagecpp_blob_destroy(blob);
        return imagecpp::core::succeed(error);
    } catch (const std::exception &exception) {
        imagecpp_blob_destroy(blob);
        return imagecpp::detail::exception_status(error, exception);
    } catch (...) {
        imagecpp_blob_destroy(blob);
        return imagecpp::core::fail(error, IMAGECPP_STATUS_INTERNAL, "unexpected image save failure");
    }
}

const void *imagecpp_blob_data(const imagecpp_blob *blob) {
    return blob == nullptr || blob->bytes.empty() ? nullptr : blob->bytes.data();
}

size_t imagecpp_blob_size(const imagecpp_blob *blob) { return blob == nullptr ? 0 : blob->bytes.size(); }

void imagecpp_blob_destroy(imagecpp_blob *blob) { delete blob; }

} // extern "C"
