#ifndef IMAGECPP_WORKFLOW_CUTOUT_INTERNAL_HPP
#define IMAGECPP_WORKFLOW_CUTOUT_INTERNAL_HPP

#include "imagecpp/imagecpp.h"

#include <cstdint>
#include <vector>

namespace imagecpp::workflow {

struct CutoutData {
    uint32_t width = 0;
    uint32_t height = 0;
    imagecpp_color_space color_space = IMAGECPP_COLOR_SPACE_UNKNOWN;
    std::vector<uint8_t> image;
    std::vector<uint8_t> mask;
    imagecpp_box source_box{};
};

imagecpp_status validate_cutout_source(const imagecpp_const_image_view &image, imagecpp_error *error);

imagecpp_status copy_gray_mask(const imagecpp_const_image_view &view, uint32_t expected_width, uint32_t expected_height,
                               std::vector<uint8_t> &mask, imagecpp_error *error);

imagecpp_status compose_cutout(const imagecpp_const_image_view &image, std::vector<uint8_t> full_mask, int crop_to_mask,
                               uint32_t padding, const imagecpp_model *upscaler_model, uint32_t upscale_factor,
                               CutoutData &output, imagecpp_error *error);

} // namespace imagecpp::workflow

#endif
