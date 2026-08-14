#include "imagecpp/imagecpp.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

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

void test_version_and_runtime() {
    require(std::string(imagecpp_version_string()) == "0.1.0-dev", "unexpected version string");
    imagecpp::Runtime runtime;
    const auto operations = runtime.operations();
    require(operations.size() == 1, "runtime should expose one foundation operation");
    require(operations[0].id == "image.resize", "runtime should expose image.resize");
    require(operations[0].input_kind == IMAGECPP_ARTIFACT_IMAGE, "resize should consume an image");
    require(operations[0].output_kind == IMAGECPP_ARTIFACT_IMAGE, "resize should emit an image");
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

} // namespace

int main() {
    try {
        test_version_and_runtime();
        test_layout_validation();
        test_nearest_resize();
        test_bilinear_resize_u8();
        test_bilinear_resize_f32();
        test_resize_rejects_mismatched_and_overlapping_views();
        std::cout << "all image.cpp tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
