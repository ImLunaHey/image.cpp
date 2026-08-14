#ifndef IMAGECPP_MODEL_MODEL_HPP
#define IMAGECPP_MODEL_MODEL_HPP

#include "imagecpp/imagecpp.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace imagecpp::detail {

class Failure final : public std::runtime_error {
  public:
    Failure(imagecpp_status status, const std::string &message) : std::runtime_error(message), status_(status) {}

    imagecpp_status status() const noexcept { return status_; }

  private:
    imagecpp_status status_;
};

struct SegmentRequest {
    std::vector<imagecpp_point_prompt> points;
    imagecpp_box box{};
    bool use_box = false;
    bool multimask = false;
};

struct SegmentOutput {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> mask;
    imagecpp_box box{};
    float score = 0.0F;
    float iou_score = 0.0F;
};

struct GenerateRequest {
    std::string prompt;
    std::string negative_prompt;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t steps = 0;
    float guidance = 0.0F;
    int64_t seed = -1;
    int32_t batch_count = 0;
    float strength = 0.0F;
    imagecpp_sample_method sample_method = IMAGECPP_SAMPLE_METHOD_AUTO;
    imagecpp_scheduler scheduler = IMAGECPP_SCHEDULER_AUTO;
    const imagecpp_const_image_view *init_image = nullptr;
    const imagecpp_const_image_view *mask = nullptr;
};

struct ImageOutput {
    uint32_t width = 0;
    uint32_t height = 0;
    imagecpp_pixel_format pixel_format = IMAGECPP_PIXEL_FORMAT_UNKNOWN;
    imagecpp_color_space color_space = IMAGECPP_COLOR_SPACE_UNKNOWN;
    std::vector<uint8_t> data;
};

struct DepthOutput {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> depth;
    std::vector<float> confidence;
    std::vector<float> sky;
    bool is_metric = false;
    bool has_pose = false;
    float extrinsics[12]{};
    float intrinsics[9]{};
};

struct ClassificationOutput {
    size_t label_index = 0;
    std::string label;
    float score = 0.0F;
};

class Session {
  public:
    virtual ~Session() = default;
    virtual void set_image(const imagecpp_const_image_view &image) = 0;
    virtual std::vector<SegmentOutput> segment(const SegmentRequest &request) = 0;
};

class Model {
  public:
    virtual ~Model() = default;
    virtual std::unique_ptr<Session> create_session();
    virtual std::vector<ImageOutput> generate(const GenerateRequest &request);
    virtual ImageOutput upscale(const imagecpp_const_image_view &image, uint32_t factor);
    virtual DepthOutput depth(const imagecpp_const_image_view &image, bool include_pose);
    virtual std::vector<float> embed_image(const imagecpp_const_image_view &image);
    virtual std::vector<float> embed_text(const std::string &text);
    virtual std::vector<ClassificationOutput> classify(const imagecpp_const_image_view &image,
                                                       const std::vector<std::string> &labels);
};

#if defined(IMAGECPP_WITH_CLIP)
std::shared_ptr<Model> load_clip_model(const imagecpp_model_options &options);
#endif

#if defined(IMAGECPP_WITH_SAM3)
std::shared_ptr<Model> load_sam3_model(const imagecpp_model_options &options);
#endif
#if defined(IMAGECPP_WITH_DEPTH_ANYTHING)
std::shared_ptr<Model> load_depth_anything_model(const imagecpp_model_options &options);
#endif
#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
std::shared_ptr<Model> load_diffusion_model(const imagecpp_diffusion_model_options &options);
std::shared_ptr<Model> load_upscaler_model(const imagecpp_upscaler_model_options &options);
#endif

} // namespace imagecpp::detail

#endif
