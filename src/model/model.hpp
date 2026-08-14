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

class Session {
  public:
    virtual ~Session() = default;
    virtual void set_image(const imagecpp_const_image_view &image) = 0;
    virtual std::vector<SegmentOutput> segment(const SegmentRequest &request) = 0;
};

class Model {
  public:
    virtual ~Model() = default;
    virtual std::unique_ptr<Session> create_session() = 0;
};

#if defined(IMAGECPP_WITH_SAM3)
std::shared_ptr<Model> load_sam3_model(const imagecpp_model_options &options);
#endif

} // namespace imagecpp::detail

#endif
