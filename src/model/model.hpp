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

struct DetectRequest {
    std::string prompt;
    std::vector<imagecpp_box> positive_exemplars;
    std::vector<imagecpp_box> negative_exemplars;
    float score_threshold = 0.5F;
    float nms_threshold = 0.1F;
};

struct DetectionOutput {
    std::string label;
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

struct OcrRequest {
    imagecpp_ocr_page_segmentation page_segmentation = IMAGECPP_OCR_PAGE_AUTO;
    uint32_t source_dpi = 300;
    bool preserve_interword_spaces = false;
};

struct TextRegionOutput {
    imagecpp_text_region_level level = IMAGECPP_TEXT_REGION_WORD;
    std::string text;
    imagecpp_box box{};
    float confidence = 0.0F;
    size_t block_index = IMAGECPP_NO_INDEX;
    size_t paragraph_index = IMAGECPP_NO_INDEX;
    size_t line_index = IMAGECPP_NO_INDEX;
    size_t word_index = IMAGECPP_NO_INDEX;
    imagecpp_text_block_type block_type = IMAGECPP_TEXT_BLOCK_UNKNOWN;
    imagecpp_line_segment baseline{};
    bool has_baseline = false;
    imagecpp_text_orientation orientation = IMAGECPP_TEXT_ORIENTATION_UNKNOWN;
    imagecpp_writing_direction writing_direction = IMAGECPP_WRITING_DIRECTION_UNKNOWN;
    imagecpp_textline_order textline_order = IMAGECPP_TEXTLINE_ORDER_UNKNOWN;
    float deskew_angle_degrees = 0.0F;
};

struct OcrOutput {
    std::string text;
    std::string language;
    float mean_confidence = 0.0F;
    std::vector<TextRegionOutput> regions;
};

struct VisualQueryRequest {
    std::string prompt;
    uint32_t max_tokens = 128;
    float temperature = 0.1F;
    float top_p = 0.9F;
    int32_t top_k = 40;
    uint32_t seed = 0;
};

struct TextOutput {
    std::string text;
    size_t prompt_tokens = 0;
    size_t generated_tokens = 0;
    imagecpp_text_finish_reason finish_reason = IMAGECPP_TEXT_FINISH_END_OF_GENERATION;
};

class Session {
  public:
    virtual ~Session() = default;
    virtual void set_image(const imagecpp_const_image_view &image) = 0;
    virtual std::vector<SegmentOutput> segment(const SegmentRequest &request) = 0;
    virtual std::vector<DetectionOutput> detect(const DetectRequest &request);
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
    virtual OcrOutput ocr(const imagecpp_const_image_view &image, const OcrRequest &request);
    virtual TextOutput visual_query(const imagecpp_const_image_view &image, const VisualQueryRequest &request,
                                    imagecpp_text_stream_callback callback, void *user_data);
};

#if defined(IMAGECPP_WITH_CLIP)
std::shared_ptr<Model> load_clip_model(const imagecpp_model_options &options);
#endif

#if defined(IMAGECPP_WITH_TESSERACT)
std::shared_ptr<Model> load_tesseract_model(const imagecpp_model_options &options);
#endif

#if defined(IMAGECPP_WITH_SAM3)
std::shared_ptr<Model> load_sam3_model(const imagecpp_model_options &options, bool require_text_detection = false);
#endif
#if defined(IMAGECPP_WITH_DEPTH_ANYTHING)
std::shared_ptr<Model> load_depth_anything_model(const imagecpp_model_options &options);
#endif
#if defined(IMAGECPP_WITH_STABLE_DIFFUSION)
std::shared_ptr<Model> load_diffusion_model(const imagecpp_diffusion_model_options &options);
std::shared_ptr<Model> load_upscaler_model(const imagecpp_upscaler_model_options &options);
#endif

#if defined(IMAGECPP_WITH_VLM)
std::shared_ptr<Model> load_vlm_model(const imagecpp_vlm_model_options &options);
#endif

} // namespace imagecpp::detail

#endif
