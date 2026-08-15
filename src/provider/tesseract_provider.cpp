#include "model/model.hpp"

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace imagecpp::detail {
namespace {

static_assert(TESSERACT_MAJOR_VERSION == 5, "image.cpp requires Tesseract 5");

constexpr float kRadiansToDegrees = 57.29577951308232F;

std::vector<uint8_t> read_model(const std::string &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw Failure(IMAGECPP_STATUS_IO_ERROR, "failed to open OCR model: " + path);
    }
    const std::streampos end = stream.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(std::numeric_limits<int>::max())) {
        throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "OCR model is empty or exceeds the provider size limit");
    }
    std::vector<uint8_t> data(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
        throw Failure(IMAGECPP_STATUS_IO_ERROR, "failed to read OCR model: " + path);
    }
    return data;
}

std::string model_language(const std::string &path) {
    const std::filesystem::path model_path(path);
    std::string extension = model_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const std::string language = model_path.stem().string();
    if (extension != ".traineddata" || language.empty()) {
        throw Failure(IMAGECPP_STATUS_INVALID_ARGUMENT,
                      "OCR model must be a named Tesseract .traineddata file (for example eng.traineddata)");
    }
    return language;
}

tesseract::PageSegMode page_segmentation(imagecpp_ocr_page_segmentation mode) {
    switch (mode) {
    case IMAGECPP_OCR_PAGE_AUTO:
        return tesseract::PSM_AUTO;
    case IMAGECPP_OCR_PAGE_SINGLE_COLUMN:
        return tesseract::PSM_SINGLE_COLUMN;
    case IMAGECPP_OCR_PAGE_SINGLE_BLOCK:
        return tesseract::PSM_SINGLE_BLOCK;
    case IMAGECPP_OCR_PAGE_SINGLE_LINE:
        return tesseract::PSM_SINGLE_LINE;
    case IMAGECPP_OCR_PAGE_SINGLE_WORD:
        return tesseract::PSM_SINGLE_WORD;
    case IMAGECPP_OCR_PAGE_SPARSE_TEXT:
        return tesseract::PSM_SPARSE_TEXT;
    case IMAGECPP_OCR_PAGE_RAW_LINE:
        return tesseract::PSM_RAW_LINE;
    }
    throw Failure(IMAGECPP_STATUS_INVALID_ARGUMENT, "unknown OCR page segmentation mode");
}

imagecpp_text_block_type block_type(tesseract::PolyBlockType type) {
    switch (type) {
    case tesseract::PT_FLOWING_TEXT:
        return IMAGECPP_TEXT_BLOCK_FLOWING_TEXT;
    case tesseract::PT_HEADING_TEXT:
        return IMAGECPP_TEXT_BLOCK_HEADING_TEXT;
    case tesseract::PT_PULLOUT_TEXT:
        return IMAGECPP_TEXT_BLOCK_PULLOUT_TEXT;
    case tesseract::PT_EQUATION:
        return IMAGECPP_TEXT_BLOCK_EQUATION;
    case tesseract::PT_INLINE_EQUATION:
        return IMAGECPP_TEXT_BLOCK_INLINE_EQUATION;
    case tesseract::PT_TABLE:
        return IMAGECPP_TEXT_BLOCK_TABLE;
    case tesseract::PT_VERTICAL_TEXT:
        return IMAGECPP_TEXT_BLOCK_VERTICAL_TEXT;
    case tesseract::PT_CAPTION_TEXT:
        return IMAGECPP_TEXT_BLOCK_CAPTION_TEXT;
    case tesseract::PT_FLOWING_IMAGE:
        return IMAGECPP_TEXT_BLOCK_FLOWING_IMAGE;
    case tesseract::PT_HEADING_IMAGE:
        return IMAGECPP_TEXT_BLOCK_HEADING_IMAGE;
    case tesseract::PT_PULLOUT_IMAGE:
        return IMAGECPP_TEXT_BLOCK_PULLOUT_IMAGE;
    case tesseract::PT_HORZ_LINE:
        return IMAGECPP_TEXT_BLOCK_HORIZONTAL_LINE;
    case tesseract::PT_VERT_LINE:
        return IMAGECPP_TEXT_BLOCK_VERTICAL_LINE;
    case tesseract::PT_NOISE:
        return IMAGECPP_TEXT_BLOCK_NOISE;
    case tesseract::PT_UNKNOWN:
    case tesseract::PT_COUNT:
        return IMAGECPP_TEXT_BLOCK_UNKNOWN;
    }
    return IMAGECPP_TEXT_BLOCK_UNKNOWN;
}

imagecpp_text_orientation orientation(tesseract::Orientation value) {
    switch (value) {
    case tesseract::ORIENTATION_PAGE_UP:
        return IMAGECPP_TEXT_ORIENTATION_PAGE_UP;
    case tesseract::ORIENTATION_PAGE_RIGHT:
        return IMAGECPP_TEXT_ORIENTATION_PAGE_RIGHT;
    case tesseract::ORIENTATION_PAGE_DOWN:
        return IMAGECPP_TEXT_ORIENTATION_PAGE_DOWN;
    case tesseract::ORIENTATION_PAGE_LEFT:
        return IMAGECPP_TEXT_ORIENTATION_PAGE_LEFT;
    }
    return IMAGECPP_TEXT_ORIENTATION_UNKNOWN;
}

imagecpp_writing_direction writing_direction(tesseract::WritingDirection value) {
    switch (value) {
    case tesseract::WRITING_DIRECTION_LEFT_TO_RIGHT:
        return IMAGECPP_WRITING_DIRECTION_LEFT_TO_RIGHT;
    case tesseract::WRITING_DIRECTION_RIGHT_TO_LEFT:
        return IMAGECPP_WRITING_DIRECTION_RIGHT_TO_LEFT;
    case tesseract::WRITING_DIRECTION_TOP_TO_BOTTOM:
        return IMAGECPP_WRITING_DIRECTION_TOP_TO_BOTTOM;
    }
    return IMAGECPP_WRITING_DIRECTION_UNKNOWN;
}

imagecpp_textline_order textline_order(tesseract::TextlineOrder value) {
    switch (value) {
    case tesseract::TEXTLINE_ORDER_LEFT_TO_RIGHT:
        return IMAGECPP_TEXTLINE_ORDER_LEFT_TO_RIGHT;
    case tesseract::TEXTLINE_ORDER_RIGHT_TO_LEFT:
        return IMAGECPP_TEXTLINE_ORDER_RIGHT_TO_LEFT;
    case tesseract::TEXTLINE_ORDER_TOP_TO_BOTTOM:
        return IMAGECPP_TEXTLINE_ORDER_TOP_TO_BOTTOM;
    }
    return IMAGECPP_TEXTLINE_ORDER_UNKNOWN;
}

void trim_line_endings(std::string &text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
}

std::string iterator_text(const tesseract::ResultIterator &iterator, tesseract::PageIteratorLevel level) {
    std::unique_ptr<char[]> text(iterator.GetUTF8Text(level));
    std::string result = text == nullptr ? "" : text.get();
    trim_line_endings(result);
    return result;
}

TextRegionOutput make_region(const tesseract::ResultIterator &iterator, tesseract::PageIteratorLevel tess_level,
                             imagecpp_text_region_level level, size_t block_index, size_t paragraph_index,
                             size_t line_index, size_t word_index) {
    TextRegionOutput output;
    output.level = level;
    output.text = iterator_text(iterator, tess_level);
    output.confidence = std::clamp(iterator.Confidence(tess_level) / 100.0F, 0.0F, 1.0F);
    output.block_index = block_index;
    output.paragraph_index = paragraph_index;
    output.line_index = line_index;
    output.word_index = word_index;
    output.block_type = block_type(iterator.BlockType());

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    if (iterator.BoundingBox(tess_level, &left, &top, &right, &bottom)) {
        output.box = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                      static_cast<float>(bottom)};
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (tess_level == tesseract::RIL_TEXTLINE && iterator.Baseline(tess_level, &x0, &y0, &x1, &y1)) {
        output.baseline = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                           static_cast<float>(y1)};
        output.has_baseline = true;
    }

    tesseract::Orientation tess_orientation = tesseract::ORIENTATION_PAGE_UP;
    tesseract::WritingDirection tess_direction = tesseract::WRITING_DIRECTION_LEFT_TO_RIGHT;
    tesseract::TextlineOrder tess_order = tesseract::TEXTLINE_ORDER_TOP_TO_BOTTOM;
    float deskew = 0.0F;
    iterator.Orientation(&tess_orientation, &tess_direction, &tess_order, &deskew);
    output.orientation = orientation(tess_orientation);
    output.writing_direction = writing_direction(tess_direction);
    output.textline_order = textline_order(tess_order);
    output.deskew_angle_degrees = deskew * kRadiansToDegrees;
    return output;
}

class TesseractModel final : public Model {
  public:
    explicit TesseractModel(const imagecpp_model_options &options)
        : language_(model_language(options.model_path)), model_data_(read_model(options.model_path)),
          api_(std::make_unique<tesseract::TessBaseAPI>()) {
        if (options.device == IMAGECPP_DEVICE_GPU) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "Tesseract OCR supports CPU execution only");
        }
        if (options.threads != 0) {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED,
                          "this Tesseract build does not expose per-model thread configuration");
        }
        const int status =
            api_->Init(reinterpret_cast<const char *>(model_data_.data()), static_cast<int>(model_data_.size()),
                       language_.c_str(), tesseract::OEM_LSTM_ONLY, nullptr, 0, nullptr, nullptr, false, nullptr);
        if (status != 0) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to initialize Tesseract OCR model");
        }
    }

    OcrOutput ocr(const imagecpp_const_image_view &image, const OcrRequest &request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            image.row_stride > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw Failure(IMAGECPP_STATUS_OUT_OF_RANGE, "OCR image dimensions exceed the provider limit");
        }

        const unsigned char *pixels = static_cast<const unsigned char *>(image.data);
        int channels = 0;
        int stride = static_cast<int>(image.row_stride);
        std::vector<uint8_t> converted;
        if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_GRAY_U8) {
            channels = 1;
        } else if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGB_U8) {
            channels = 3;
        } else if (image.pixel_format == IMAGECPP_PIXEL_FORMAT_RGBA_U8 ||
                   image.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8) {
            channels = 3;
            stride = static_cast<int>(image.width * 3U);
            converted.resize(static_cast<size_t>(stride) * image.height);
            for (uint32_t y = 0; y < image.height; ++y) {
                const auto *source = pixels + static_cast<size_t>(y) * image.row_stride;
                auto *destination = converted.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
                for (uint32_t x = 0; x < image.width; ++x) {
                    const size_t source_index = static_cast<size_t>(x) * 4U;
                    const size_t destination_index = static_cast<size_t>(x) * 3U;
                    const uint8_t alpha = source[source_index + 3U];
                    const uint8_t red =
                        source[source_index + (image.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8 ? 2U : 0U)];
                    const uint8_t green = source[source_index + 1U];
                    const uint8_t blue =
                        source[source_index + (image.pixel_format == IMAGECPP_PIXEL_FORMAT_BGRA_U8 ? 0U : 2U)];
                    const auto composite = [alpha](uint8_t value) {
                        return static_cast<uint8_t>(
                            (static_cast<uint32_t>(value) * alpha + 255U * (255U - alpha) + 127U) / 255U);
                    };
                    destination[destination_index] = composite(red);
                    destination[destination_index + 1U] = composite(green);
                    destination[destination_index + 2U] = composite(blue);
                }
            }
            pixels = converted.data();
        } else {
            throw Failure(IMAGECPP_STATUS_UNSUPPORTED, "OCR accepts GRAY_U8, RGB_U8, RGBA_U8, or BGRA_U8 images");
        }

        api_->SetPageSegMode(page_segmentation(request.page_segmentation));
        if (!api_->SetVariable("preserve_interword_spaces", request.preserve_interword_spaces ? "1" : "0")) {
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "failed to configure Tesseract spacing behavior");
        }
        api_->SetImage(pixels, static_cast<int>(image.width), static_cast<int>(image.height), channels, stride);
        if (request.source_dpi != 0) {
            api_->SetSourceResolution(static_cast<int>(request.source_dpi));
        }
        if (api_->Recognize(nullptr) != 0) {
            api_->Clear();
            throw Failure(IMAGECPP_STATUS_MODEL_ERROR, "Tesseract failed to recognize the image");
        }

        OcrOutput output;
        output.language = language_;
        output.mean_confidence = std::clamp(api_->MeanTextConf() / 100.0F, 0.0F, 1.0F);
        std::unique_ptr<char[]> text(api_->GetUTF8Text());
        output.text = text == nullptr ? "" : text.get();
        trim_line_endings(output.text);

        std::unique_ptr<tesseract::ResultIterator> iterator(api_->GetIterator());
        if (iterator != nullptr) {
            size_t block_index = IMAGECPP_NO_INDEX;
            size_t paragraph_index = IMAGECPP_NO_INDEX;
            size_t line_index = IMAGECPP_NO_INDEX;
            size_t word_index = IMAGECPP_NO_INDEX;
            do {
                if (block_index == IMAGECPP_NO_INDEX || iterator->IsAtBeginningOf(tesseract::RIL_BLOCK)) {
                    ++block_index;
                    output.regions.push_back(make_region(*iterator, tesseract::RIL_BLOCK, IMAGECPP_TEXT_REGION_BLOCK,
                                                         block_index, IMAGECPP_NO_INDEX, IMAGECPP_NO_INDEX,
                                                         IMAGECPP_NO_INDEX));
                }
                if (paragraph_index == IMAGECPP_NO_INDEX || iterator->IsAtBeginningOf(tesseract::RIL_PARA)) {
                    ++paragraph_index;
                    output.regions.push_back(make_region(*iterator, tesseract::RIL_PARA, IMAGECPP_TEXT_REGION_PARAGRAPH,
                                                         block_index, paragraph_index, IMAGECPP_NO_INDEX,
                                                         IMAGECPP_NO_INDEX));
                }
                if (line_index == IMAGECPP_NO_INDEX || iterator->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) {
                    ++line_index;
                    output.regions.push_back(make_region(*iterator, tesseract::RIL_TEXTLINE, IMAGECPP_TEXT_REGION_LINE,
                                                         block_index, paragraph_index, line_index, IMAGECPP_NO_INDEX));
                }
                ++word_index;
                output.regions.push_back(make_region(*iterator, tesseract::RIL_WORD, IMAGECPP_TEXT_REGION_WORD,
                                                     block_index, paragraph_index, line_index, word_index));
            } while (iterator->Next(tesseract::RIL_WORD));
        }
        api_->Clear();
        return output;
    }

  private:
    std::string language_;
    std::vector<uint8_t> model_data_;
    std::unique_ptr<tesseract::TessBaseAPI> api_;
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<Model> load_tesseract_model(const imagecpp_model_options &options) {
    return std::make_shared<TesseractModel>(options);
}

} // namespace imagecpp::detail
