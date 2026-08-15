#include "imagecpp/imagecpp.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

imagecpp_image_desc make_desc(uint32_t width, uint32_t height, imagecpp_pixel_format format,
                              imagecpp_color_space color_space = IMAGECPP_COLOR_SPACE_SRGB) {
    return {sizeof(imagecpp_image_desc), width, height, 0, format, color_space};
}

imagecpp_const_image_view as_const(const imagecpp_image_view &view) {
    return {
        sizeof(imagecpp_const_image_view),
        view.data,
        view.data_size,
        view.width,
        view.height,
        view.row_stride,
        view.pixel_format,
        view.color_space,
    };
}

std::vector<uint8_t> packed_bytes(const imagecpp::Image &image) {
    const imagecpp_const_image_view view = image.view();
    const size_t row_bytes = static_cast<size_t>(view.width) * imagecpp_pixel_format_bytes_per_pixel(view.pixel_format);
    std::vector<uint8_t> result(row_bytes * view.height);
    for (uint32_t row = 0; row < view.height; ++row) {
        std::memcpy(result.data() + static_cast<size_t>(row) * row_bytes,
                    static_cast<const uint8_t *>(view.data) + static_cast<size_t>(row) * view.row_stride, row_bytes);
    }
    return result;
}

void set_packed_bytes(imagecpp::Image &image, const std::vector<uint8_t> &bytes) {
    imagecpp_image_view view = image.view();
    const size_t row_bytes = static_cast<size_t>(view.width) * imagecpp_pixel_format_bytes_per_pixel(view.pixel_format);
    require(bytes.size() == row_bytes * view.height, "test pixel data has the wrong size");
    for (uint32_t row = 0; row < view.height; ++row) {
        std::memcpy(static_cast<uint8_t *>(view.data) + static_cast<size_t>(row) * view.row_stride,
                    bytes.data() + static_cast<size_t>(row) * row_bytes, row_bytes);
    }
}

void test_version_and_runtime() {
    require(std::string(imagecpp_version_string()) == "0.1.0-dev", "unexpected version string");
    imagecpp::Runtime runtime;
    const auto operations = runtime.operations();
    size_t expected_count = 1;
#if defined(IMAGECPP_TEST_WITH_SAM3)
    expected_count += 4;
#endif
#if defined(IMAGECPP_TEST_WITH_DEPTH_ANYTHING)
    expected_count += 1;
#endif
#if defined(IMAGECPP_TEST_WITH_CLIP)
    expected_count += 2;
#endif
#if defined(IMAGECPP_TEST_WITH_STABLE_DIFFUSION)
    expected_count += 3;
#endif
    require(operations.size() == expected_count, "runtime operation count mismatch");
    size_t index = 0;
    require(operations[index].id == "image.resize", "runtime should expose image.resize");
    require(operations[index].input_kind == IMAGECPP_ARTIFACT_IMAGE, "resize should consume an image");
    require(operations[index].output_kind == IMAGECPP_ARTIFACT_IMAGE, "resize should emit an image");
    ++index;
#if defined(IMAGECPP_TEST_WITH_SAM3)
    require(operations[index].id == "image.detect.sam3", "runtime should expose SAM 3 detection");
    require(operations[index].task == IMAGECPP_TASK_DETECT, "SAM 3 detection task mismatch");
    ++index;
    require(operations[index].id == "image.workflow.grounded-cutout",
            "runtime should expose the grounded cutout workflow");
    require(operations[index].task == IMAGECPP_TASK_WORKFLOW, "grounded cutout workflow task mismatch");
    ++index;
    require(operations[index].id == "image.segment.sam", "runtime should expose image.segment.sam");
    require(operations[index].task == IMAGECPP_TASK_SEGMENT, "SAM operation task mismatch");
    ++index;
    require(operations[index].id == "image.workflow.cutout", "runtime should expose the cutout workflow");
    require(operations[index].task == IMAGECPP_TASK_WORKFLOW, "cutout workflow task mismatch");
    ++index;
#endif
#if defined(IMAGECPP_TEST_WITH_DEPTH_ANYTHING)
    require(operations[index].id == "image.depth.depth-anything", "runtime should expose Depth Anything");
    require(operations[index].task == IMAGECPP_TASK_DEPTH, "Depth Anything operation task mismatch");
    ++index;
#endif
#if defined(IMAGECPP_TEST_WITH_CLIP)
    require(operations[index].id == "image.embed.clip", "runtime should expose CLIP embeddings");
    require(operations[index].task == IMAGECPP_TASK_EMBED, "CLIP embedding operation task mismatch");
    ++index;
    require(operations[index].id == "image.classify.clip", "runtime should expose CLIP classification");
    require(operations[index].task == IMAGECPP_TASK_CLASSIFY, "CLIP classification operation task mismatch");
    ++index;
#endif
#if defined(IMAGECPP_TEST_WITH_STABLE_DIFFUSION)
    require(operations[index++].id == "image.generate.diffusion", "runtime should expose diffusion generation");
    require(operations[index++].id == "image.edit.diffusion", "runtime should expose diffusion editing");
    require(operations[index].id == "image.upscale.esrgan", "runtime should expose ESRGAN upscaling");
#endif
}

void test_model_api_validation() {
    imagecpp::Runtime runtime;
    imagecpp_model_options options{};
    imagecpp_model_options_init(&options);
    options.model_path = "/path/that/does/not/exist.ggml";
    imagecpp_model *model = nullptr;
    imagecpp_error error{};
    const imagecpp_status status = imagecpp_model_load(runtime.get(), "image.segment.sam", &options, &model, &error);
#if defined(IMAGECPP_TEST_WITH_SAM3)
    require(status == IMAGECPP_STATUS_MODEL_ERROR, "invalid SAM path should return a model error");
#else
    require(status == IMAGECPP_STATUS_UNSUPPORTED, "disabled SAM provider should return unsupported");
#endif
    require(model == nullptr, "failed model load returned a handle");

    imagecpp_segment_options segment_options{};
    imagecpp_segment_options_init(&segment_options);
    require(segment_options.struct_size == sizeof(segment_options), "segment options initializer is invalid");

    imagecpp_detect_options detect_options{};
    imagecpp_detect_options_init(&detect_options);
    require(detect_options.struct_size == sizeof(detect_options), "detection options initializer is invalid");
    require(detect_options.score_threshold == 0.5F && detect_options.nms_threshold == 0.1F,
            "detection option defaults are invalid");

    imagecpp_grounded_cutout_options grounded_options{};
    imagecpp_grounded_cutout_options_init(&grounded_options);
    require(grounded_options.struct_size == sizeof(grounded_options), "grounded cutout options initializer is invalid");
    require(grounded_options.detect.struct_size == sizeof(grounded_options.detect),
            "nested detection options initializer is invalid");
    require(grounded_options.selection == IMAGECPP_GROUNDED_CUTOUT_BEST && grounded_options.crop_to_mask == 1 &&
                grounded_options.upscale_factor == 1,
            "grounded cutout option defaults are invalid");

    imagecpp_cutout_options cutout_options{};
    imagecpp_cutout_options_init(&cutout_options);
    require(cutout_options.struct_size == sizeof(cutout_options), "cutout options initializer is invalid");
    require(cutout_options.segment.struct_size == sizeof(cutout_options.segment),
            "nested segment options initializer is invalid");
    require(cutout_options.crop_to_mask == 1 && cutout_options.upscale_factor == 1,
            "cutout option defaults are invalid");

    imagecpp_diffusion_model_options diffusion_options{};
    imagecpp_diffusion_model_options_init(&diffusion_options);
    require(diffusion_options.struct_size == sizeof(diffusion_options),
            "diffusion model options initializer is invalid");
    require(diffusion_options.flash_attention == 1, "diffusion flash attention should default on");

    imagecpp_upscaler_model_options upscaler_options{};
    imagecpp_upscaler_model_options_init(&upscaler_options);
    require(upscaler_options.struct_size == sizeof(upscaler_options), "upscaler model options initializer is invalid");

    imagecpp_generate_options generate_options{};
    imagecpp_generate_options_init(&generate_options);
    require(generate_options.struct_size == sizeof(generate_options), "generation options initializer is invalid");
    require(generate_options.width == 512 && generate_options.height == 512,
            "generation dimensions defaults are invalid");

    imagecpp_depth_options depth_options{};
    imagecpp_depth_options_init(&depth_options);
    require(depth_options.struct_size == sizeof(depth_options), "depth options initializer is invalid");
}

void test_layout_validation() {
    uint8_t bytes[8]{};
    imagecpp_const_image_view valid{
        sizeof(imagecpp_const_image_view), bytes, sizeof(bytes), 2, 1, sizeof(bytes), IMAGECPP_PIXEL_FORMAT_RGB_U8,
        IMAGECPP_COLOR_SPACE_SRGB,
    };
    imagecpp_error error{};
    require(imagecpp_validate_const_image_view(&valid, &error) == IMAGECPP_STATUS_OK,
            "valid padded image layout was rejected");

    valid.row_stride = 5;
    require(imagecpp_validate_const_image_view(&valid, &error) == IMAGECPP_STATUS_INVALID_ARGUMENT,
            "undersized row stride was accepted");
    require(std::string(error.message).find("row stride") != std::string::npos, "layout error lacks context");
}

void test_nearest_resize() {
    imagecpp::Image source(make_desc(2, 2, IMAGECPP_PIXEL_FORMAT_GRAY_U8));
    imagecpp_image_view source_view = source.view();
    auto *source_bytes = static_cast<uint8_t *>(source_view.data);
    source_bytes[0] = 10;
    source_bytes[1] = 20;
    source_bytes[source_view.row_stride] = 30;
    source_bytes[source_view.row_stride + 1] = 40;

    imagecpp::Image destination(make_desc(4, 4, IMAGECPP_PIXEL_FORMAT_GRAY_U8));
    imagecpp_image_view destination_view = destination.view();
    imagecpp::resize(as_const(source_view), destination_view, IMAGECPP_RESIZE_NEAREST);
    const auto *output = static_cast<const uint8_t *>(destination_view.data);
    require(output[0] == 10 && output[1] == 10 && output[2] == 20 && output[3] == 20, "nearest top row mismatch");
    const size_t bottom = destination_view.row_stride * 3;
    require(output[bottom] == 30 && output[bottom + 3] == 40, "nearest bottom row mismatch");
}

void test_bilinear_resize_u8() {
    imagecpp::Image source(make_desc(2, 1, IMAGECPP_PIXEL_FORMAT_GRAY_U8));
    imagecpp_image_view source_view = source.view();
    auto *source_bytes = static_cast<uint8_t *>(source_view.data);
    source_bytes[0] = 0;
    source_bytes[1] = 255;

    imagecpp::Image destination(make_desc(3, 1, IMAGECPP_PIXEL_FORMAT_GRAY_U8));
    imagecpp_image_view destination_view = destination.view();
    imagecpp::resize(as_const(source_view), destination_view, IMAGECPP_RESIZE_BILINEAR);
    const auto *output = static_cast<const uint8_t *>(destination_view.data);
    require(output[0] == 0 && output[1] == 128 && output[2] == 255, "bilinear u8 interpolation mismatch");
}

void test_bilinear_resize_f32() {
    imagecpp::Image source(make_desc(2, 1, IMAGECPP_PIXEL_FORMAT_GRAY_F32, IMAGECPP_COLOR_SPACE_LINEAR_SRGB));
    imagecpp_image_view source_view = source.view();
    const float values[2] = {0.0F, 1.0F};
    std::memcpy(source_view.data, values, sizeof(values));

    imagecpp::Image destination(make_desc(3, 1, IMAGECPP_PIXEL_FORMAT_GRAY_F32, IMAGECPP_COLOR_SPACE_LINEAR_SRGB));
    imagecpp_image_view destination_view = destination.view();
    imagecpp::resize(as_const(source_view), destination_view, IMAGECPP_RESIZE_BILINEAR);
    float output[3]{};
    std::memcpy(output, destination_view.data, sizeof(output));
    require(std::fabs(output[0]) < 1e-6F, "bilinear f32 first sample mismatch");
    require(std::fabs(output[1] - 0.5F) < 1e-6F, "bilinear f32 midpoint mismatch");
    require(std::fabs(output[2] - 1.0F) < 1e-6F, "bilinear f32 final sample mismatch");
}

void test_resize_rejects_mismatched_and_overlapping_views() {
    imagecpp::Image image(make_desc(2, 2, IMAGECPP_PIXEL_FORMAT_RGB_U8));
    imagecpp_image_view mutable_view = image.view();
    const imagecpp_const_image_view const_view = as_const(mutable_view);
    imagecpp_error error{};
    require(imagecpp_resize(&const_view, &mutable_view, IMAGECPP_RESIZE_NEAREST, &error) ==
                IMAGECPP_STATUS_INVALID_ARGUMENT,
            "overlapping resize was accepted");

    imagecpp::Image gray(make_desc(2, 2, IMAGECPP_PIXEL_FORMAT_GRAY_U8));
    imagecpp_image_view gray_view = gray.view();
    require(imagecpp_resize(&const_view, &gray_view, IMAGECPP_RESIZE_NEAREST, &error) ==
                IMAGECPP_STATUS_INVALID_ARGUMENT,
            "mismatched pixel formats were accepted");
}

void test_lossless_memory_codecs() {
    imagecpp::Image rgba(make_desc(3, 2, IMAGECPP_PIXEL_FORMAT_RGBA_U8));
    const std::vector<uint8_t> rgba_pixels{
        255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 12, 34, 56, 78, 90, 123, 210, 255, 5, 4, 3, 2,
    };
    set_packed_bytes(rgba, rgba_pixels);

    imagecpp::Blob png = imagecpp::encode(rgba.view(), IMAGECPP_FILE_FORMAT_PNG);
    require(png.size() > 8, "PNG encoder returned an empty blob");
    const auto *png_bytes = static_cast<const uint8_t *>(png.data());
    require(png_bytes[0] == 0x89 && png_bytes[1] == 'P' && png_bytes[2] == 'N' && png_bytes[3] == 'G',
            "PNG signature mismatch");
    imagecpp::Image decoded_png = imagecpp::decode(png.data(), png.size());
    require(decoded_png.view().pixel_format == IMAGECPP_PIXEL_FORMAT_RGBA_U8, "PNG alpha channel was not preserved");
    require(packed_bytes(decoded_png) == rgba_pixels, "PNG round trip changed pixels");

    imagecpp::Image rgb(make_desc(3, 2, IMAGECPP_PIXEL_FORMAT_RGB_U8));
    const std::vector<uint8_t> rgb_pixels{
        255, 0, 0, 0, 255, 0, 0, 0, 255, 12, 34, 56, 90, 123, 210, 5, 4, 3,
    };
    set_packed_bytes(rgb, rgb_pixels);
#if defined(IMAGECPP_TEST_WITH_WEBP)
    imagecpp::Blob webp = imagecpp::encode(rgb.view(), IMAGECPP_FILE_FORMAT_WEBP);
    require(webp.size() > 12, "WebP encoder returned an empty blob");
    const auto *webp_bytes = static_cast<const uint8_t *>(webp.data());
    require(std::memcmp(webp_bytes, "RIFF", 4) == 0 && std::memcmp(webp_bytes + 8, "WEBP", 4) == 0,
            "WebP signature mismatch");
    imagecpp::Image decoded_webp = imagecpp::decode(webp.bytes());
    require(decoded_webp.view().pixel_format == IMAGECPP_PIXEL_FORMAT_RGB_U8,
            "lossless WebP unexpectedly changed pixel format");
    require(packed_bytes(decoded_webp) == rgb_pixels, "lossless WebP round trip changed pixels");

    imagecpp_decode_options gray_options{};
    imagecpp_decode_options_init(&gray_options);
    gray_options.pixel_format = IMAGECPP_PIXEL_FORMAT_GRAY_U8;
    imagecpp::Image gray_webp = imagecpp::decode(webp.data(), webp.size(), IMAGECPP_FILE_FORMAT_WEBP, &gray_options);
    require(gray_webp.view().pixel_format == IMAGECPP_PIXEL_FORMAT_GRAY_U8, "WebP grayscale decode format mismatch");
    require(packed_bytes(gray_webp).size() == 6, "WebP grayscale decode size mismatch");
#else
    bool unsupported = false;
    try {
        (void)imagecpp::encode(rgb.view(), IMAGECPP_FILE_FORMAT_WEBP);
    } catch (const imagecpp::Error &error) {
        unsupported = error.status() == IMAGECPP_STATUS_UNSUPPORTED;
    }
    require(unsupported, "WebP encoding should report unsupported when it is not compiled in");
#endif
}

void test_bgra_and_classic_formats() {
    imagecpp::Image bgra(make_desc(2, 1, IMAGECPP_PIXEL_FORMAT_BGRA_U8));
    set_packed_bytes(bgra, {1, 2, 3, 4, 10, 20, 30, 40});
    for (const imagecpp_file_format format :
         {IMAGECPP_FILE_FORMAT_PNG, IMAGECPP_FILE_FORMAT_BMP, IMAGECPP_FILE_FORMAT_TGA}) {
        imagecpp::Blob encoded = imagecpp::encode(bgra.view(), format);
        imagecpp_decode_options options{};
        imagecpp_decode_options_init(&options);
        options.pixel_format = IMAGECPP_PIXEL_FORMAT_RGBA_U8;
        imagecpp::Image decoded = imagecpp::decode(encoded.data(), encoded.size(), format, &options);
        require(packed_bytes(decoded) == std::vector<uint8_t>({3, 2, 1, 4, 30, 20, 10, 40}),
                "BGRA channel conversion failed");
    }
}

void test_jpeg_and_file_io() {
    imagecpp::Image source(make_desc(16, 16, IMAGECPP_PIXEL_FORMAT_RGB_U8));
    std::vector<uint8_t> pixels(16 * 16 * 3);
    for (size_t index = 0; index < pixels.size(); index += 3) {
        pixels[index] = 40;
        pixels[index + 1] = 120;
        pixels[index + 2] = 200;
    }
    set_packed_bytes(source, pixels);
    imagecpp::Blob jpeg = imagecpp::encode(source.view(), IMAGECPP_FILE_FORMAT_JPEG);
    imagecpp::Image decoded_jpeg = imagecpp::decode(jpeg.data(), jpeg.size(), IMAGECPP_FILE_FORMAT_JPEG);
    require(decoded_jpeg.view().width == 16 && decoded_jpeg.view().height == 16, "JPEG dimensions changed");
    const std::vector<uint8_t> jpeg_pixels = packed_bytes(decoded_jpeg);
    require(std::abs(static_cast<int>(jpeg_pixels[0]) - 40) <= 4 &&
                std::abs(static_cast<int>(jpeg_pixels[1]) - 120) <= 4 &&
                std::abs(static_cast<int>(jpeg_pixels[2]) - 200) <= 4,
            "JPEG color error is unexpectedly large");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "imagecpp-codec-test.png";
    std::filesystem::remove(path);
    imagecpp::save(path.string(), source.view());
    imagecpp::Image loaded = imagecpp::load(path.string());
    std::filesystem::remove(path);
    require(packed_bytes(loaded) == pixels, "file save/load round trip changed pixels");
}

void test_codec_errors() {
    const uint8_t invalid[]{1, 2, 3, 4};
    bool rejected = false;
    try {
        (void)imagecpp::decode(invalid, sizeof(invalid));
    } catch (const imagecpp::Error &error) {
        rejected = error.status() != IMAGECPP_STATUS_OK;
    }
    require(rejected, "invalid encoded image data was accepted");
}

} // namespace

int main() {
    try {
        test_version_and_runtime();
        test_model_api_validation();
        test_layout_validation();
        test_nearest_resize();
        test_bilinear_resize_u8();
        test_bilinear_resize_f32();
        test_resize_rejects_mismatched_and_overlapping_views();
        test_lossless_memory_codecs();
        test_bgra_and_classic_formats();
        test_jpeg_and_file_io();
        test_codec_errors();
        std::cout << "all image.cpp tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
