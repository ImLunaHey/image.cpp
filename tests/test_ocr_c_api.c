#include "imagecpp/imagecpp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GLYPH_WIDTH = 5,
    GLYPH_HEIGHT = 7,
    GLYPH_SCALE = 16,
    GLYPH_ADVANCE = 6,
    MARGIN = 32,
};

typedef struct glyph {
    char character;
    const char *rows[GLYPH_HEIGHT];
} glyph;

static const glyph GLYPHS[] = {
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
};

static const glyph *find_glyph(char character) {
    size_t index;
    for (index = 0; index < sizeof(GLYPHS) / sizeof(GLYPHS[0]); ++index) {
        if (GLYPHS[index].character == character) {
            return &GLYPHS[index];
        }
    }
    return NULL;
}

static void draw_text(imagecpp_image_view *view, const char *text) {
    size_t character_index;
    memset(view->data, 255, view->data_size);
    for (character_index = 0; text[character_index] != '\0'; ++character_index) {
        const glyph *current = find_glyph(text[character_index]);
        size_t row;
        if (current == NULL) {
            continue;
        }
        for (row = 0; row < GLYPH_HEIGHT; ++row) {
            size_t column;
            for (column = 0; column < GLYPH_WIDTH; ++column) {
                size_t dy;
                if (current->rows[row][column] != '1') {
                    continue;
                }
                for (dy = 0; dy < GLYPH_SCALE; ++dy) {
                    const size_t y = MARGIN + row * GLYPH_SCALE + dy;
                    unsigned char *destination = (unsigned char *)view->data + y * view->row_stride;
                    size_t dx;
                    for (dx = 0; dx < GLYPH_SCALE; ++dx) {
                        const size_t x =
                            MARGIN + character_index * GLYPH_ADVANCE * GLYPH_SCALE + column * GLYPH_SCALE + dx;
                        destination[x] = 0;
                    }
                }
            }
        }
    }
}

static int fail(const char *operation, const imagecpp_error *error) {
    fprintf(stderr, "%s failed: %s\n", operation, error->message);
    return 1;
}

int main(int argc, char **argv) {
    static const char TEXT[] = "HELLO 123";
    const uint32_t width = MARGIN * 2U + (uint32_t)(strlen(TEXT) * GLYPH_ADVANCE * GLYPH_SCALE);
    const uint32_t height = MARGIN * 2U + GLYPH_HEIGHT * GLYPH_SCALE;
    imagecpp_error error = {0};
    imagecpp_runtime *runtime = NULL;
    imagecpp_model *model = NULL;
    imagecpp_image *image = NULL;
    imagecpp_ocr_result *result = NULL;
    imagecpp_image_desc description = {
        sizeof(imagecpp_image_desc), width, height, 0, IMAGECPP_PIXEL_FORMAT_GRAY_U8, IMAGECPP_COLOR_SPACE_SRGB,
    };
    imagecpp_image_view view = {0};
    imagecpp_const_image_view const_view = {0};
    imagecpp_model_options model_options;
    imagecpp_ocr_options options;
    imagecpp_ocr_info info = {0};
    size_t index;
    int saw_line = 0;
    int saw_word = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: test_ocr_c_api <eng.traineddata> [fixture.png]\n");
        return 2;
    }
    if (imagecpp_runtime_create(&runtime, &error) != IMAGECPP_STATUS_OK) {
        return fail("runtime create", &error);
    }
    imagecpp_model_options_init(&model_options);
    model_options.model_path = argv[1];
    if (imagecpp_model_load(runtime, "image.ocr.tesseract", &model_options, &model, &error) != IMAGECPP_STATUS_OK) {
        return fail("OCR model load", &error);
    }
    if (imagecpp_image_create(&description, &image, &error) != IMAGECPP_STATUS_OK) {
        return fail("image create", &error);
    }
    view.struct_size = sizeof(view);
    if (imagecpp_image_get_view(image, &view, &error) != IMAGECPP_STATUS_OK) {
        return fail("image view", &error);
    }
    draw_text(&view, TEXT);
    const_view.struct_size = sizeof(const_view);
    if (imagecpp_image_get_const_view(image, &const_view, &error) != IMAGECPP_STATUS_OK) {
        return fail("const image view", &error);
    }
    if (argc >= 3 &&
        imagecpp_image_save(argv[2], &const_view, IMAGECPP_FILE_FORMAT_PNG, NULL, &error) != IMAGECPP_STATUS_OK) {
        return fail("fixture save", &error);
    }

    imagecpp_ocr_options_init(&options);
    options.page_segmentation = IMAGECPP_OCR_PAGE_SINGLE_LINE;
    if (imagecpp_ocr(model, &const_view, &options, &result, &error) != IMAGECPP_STATUS_OK) {
        return fail("OCR", &error);
    }
    info.struct_size = sizeof(info);
    if (imagecpp_ocr_result_info(result, &info, &error) != IMAGECPP_STATUS_OK) {
        return fail("OCR info", &error);
    }
    if (info.text == NULL || strstr(info.text, "HELL") == NULL || info.language == NULL ||
        strcmp(info.language, "eng") != 0 || info.mean_confidence < 0.0F || info.mean_confidence > 1.0F ||
        info.region_count == 0) {
        fprintf(stderr, "unexpected OCR result: text='%s', language='%s', confidence=%f, regions=%zu\n",
                info.text == NULL ? "" : info.text, info.language == NULL ? "" : info.language, info.mean_confidence,
                info.region_count);
        return 3;
    }
    for (index = 0; index < imagecpp_ocr_result_region_count(result); ++index) {
        imagecpp_text_region_info region = {0};
        region.struct_size = sizeof(region);
        if (imagecpp_ocr_result_region_info(result, index, &region, &error) != IMAGECPP_STATUS_OK) {
            return fail("OCR region", &error);
        }
        if (region.box.x1 <= region.box.x0 || region.box.y1 <= region.box.y0 || region.confidence < 0.0F ||
            region.confidence > 1.0F) {
            fprintf(stderr, "invalid OCR region geometry or confidence\n");
            return 4;
        }
        saw_line |= region.level == IMAGECPP_TEXT_REGION_LINE && region.has_baseline != 0;
        saw_word |= region.level == IMAGECPP_TEXT_REGION_WORD && region.word_index != IMAGECPP_NO_INDEX;
    }
    if (!saw_line || !saw_word) {
        fprintf(stderr, "OCR result did not contain line and word layout metadata\n");
        return 5;
    }

    imagecpp_ocr_result_destroy(result);
    imagecpp_image_destroy(image);
    imagecpp_model_destroy(model);
    imagecpp_runtime_destroy(runtime);
    return 0;
}
