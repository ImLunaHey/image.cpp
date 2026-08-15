# Quickstart

The default build produces one native `imagecpp` executable and an embeddable
`libimagecpp`. Model weights are separate, checksum-verified files because they
retain their own licenses.

## Build the full runtime

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/imagecpp inspect
```

A normal runtime build does not need Python.

## Get the starter models

The starter bundle is about 487 MiB and enables promptable segmentation,
background removal, dense depth with optional camera pose, joint image/text
embeddings, zero-shot classification, OCR with document layout, image
captioning, visual question answering, 4x upscaling, and a composed transparent
cutout workflow:

```sh
cmake --build build --target imagecpp_models_starter
```

Each target verifies an existing file before use and downloads a replacement
only when the file is absent or its SHA-256 is wrong. Models are written under
`models/` and are intentionally not committed.

Given an `input.jpg`, try:

```sh
./build/imagecpp depth \
  models/depth-anything-base-q4_k.gguf input.jpg depth.png --pose

./build/imagecpp segment \
  models/edgetam_q4_0.ggml input.jpg mask.png --point 640,420

./build/imagecpp remove-background \
  models/edgetam_q4_0.ggml input.jpg cutout.png --point 640,420

./build/imagecpp cutout \
  models/edgetam_q4_0.ggml input.jpg cutout-4x.png \
  --point 640,420 --padding 16 \
  --upscaler models/RealESRGAN_x4plus_anime_6B.pth --factor 4

./build/imagecpp upscale \
  models/RealESRGAN_x4plus_anime_6B.pth input.jpg upscaled.png --factor 4

./build/imagecpp classify \
  models/clip-vit-b-32-laion2b-q4_0.gguf input.jpg cat dog bicycle

./build/imagecpp embed-image \
  models/clip-vit-b-32-laion2b-q4_0.gguf input.jpg > embedding.json

./build/imagecpp ocr \
  models/eng.traineddata input.jpg --json > document.json

./build/imagecpp caption \
  models/SmolVLM-256M-Instruct-Q8_0.gguf \
  models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf input.jpg --stream

./build/imagecpp ask \
  models/SmolVLM-256M-Instruct-Q8_0.gguf \
  models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf input.jpg \
  "What objects are visible?" --json
```

Prompt coordinates are image pixels. A positive point should be inside the
object; add repeatable `--negative x,y` points outside it or use
`--box x0,y0,x1,y1` when the initial mask is ambiguous.

`cutout` selects the best mask, crops to its foreground bounds, upscales the
color crop before applying alpha, and writes an RGBA image. Use
`--keep-canvas` to preserve the full input extent, or omit `--upscaler` for a
native-resolution result.

`classify` applies the standard `a photo of a <label>` prompt template and
prints probabilities normalized over the supplied labels. Use `embed-text`
when an application needs complete control over the text prompt. Both image
and text embeddings are unit-normalized 512-element vectors in the same space.

## Add grounding, generation, and editing

The full bundle adds the 674 MiB SAM 3 Q4_0 grounding model and the 3.05 GB
Stable Diffusion 1.5 Q4_0 checkpoint:

```sh
cmake --build build --target imagecpp_models_full

./build/imagecpp detect \
  models/sam3-q4_0.ggml input.jpg "yellow school bus" --threshold 0.4

./build/imagecpp ground \
  models/sam3-q4_0.ggml input.jpg buses.png "yellow school bus"

./build/imagecpp extract \
  models/sam3-q4_0.ggml input.jpg bus.png "yellow school bus" \
  --threshold 0.4 --padding 16

./build/imagecpp extract \
  models/sam3-q4_0.ggml input.jpg buses-4x.png "yellow school bus" \
  --all --padding 16 \
  --upscaler models/RealESRGAN_x4plus_anime_6B.pth --factor 4

./build/imagecpp generate \
  models/v1-5-pruned_Q4_0.gguf generated.png \
  "a small orange cat on a windowsill, detailed photograph" \
  --size 512x512 --steps 20 --seed 42

./build/imagecpp edit \
  models/v1-5-pruned_Q4_0.gguf generated.png edited.png \
  "an orange cat wearing round red sunglasses" \
  --strength 0.45 --seed 43

./build/imagecpp edit \
  models/v1-5-pruned_Q4_0.gguf generated.png inpainted.png \
  "an orange cat wearing round red sunglasses" \
  --mask mask.png --strength 0.9 --seed 45
```

An inpainting-specific checkpoint preserves unmasked regions better than the
general starter checkpoint. Input and mask dimensions must match.

## Embed the library

The C++ wrapper owns every runtime handle and result with RAII. This example
loads one model once and retains the raw float depth map rather than the CLI's
8-bit visualization:

```cpp
#include <imagecpp/imagecpp.hpp>

imagecpp::Runtime runtime;
imagecpp_model_options model_options{};
imagecpp_model_options_init(&model_options);
model_options.model_path = "models/depth-anything-base-q4_k.gguf";
imagecpp::Model model(runtime, "image.depth.depth-anything", model_options);

imagecpp::Image input = imagecpp::load("input.jpg");
imagecpp_depth_options depth_options{};
imagecpp_depth_options_init(&depth_options);
depth_options.include_pose = 1;

imagecpp::DepthResult result = imagecpp::depth(model, input, depth_options);
imagecpp_depth_info info = result.info();
const float *depth = static_cast<const float *>(info.depth.data);
// depth remains valid until result is destroyed.
```

OCR uses another task-specific result while retaining the same model and image
ownership rules:

```cpp
imagecpp_model_options ocr_model_options{};
imagecpp_model_options_init(&ocr_model_options);
ocr_model_options.model_path = "models/eng.traineddata";
imagecpp::Model ocr_model(runtime, "image.ocr.tesseract", ocr_model_options);

imagecpp_ocr_options ocr_options{};
imagecpp_ocr_options_init(&ocr_options);
imagecpp::OcrResult document = imagecpp::ocr(ocr_model, input, ocr_options);
imagecpp::OcrInfo page = document.info();
imagecpp::TextRegionInfo first_region = document.at(0);
// page.text owns the copied UTF-8 page text; regions retain layout metadata.
```

Captioning and visual questions use a language GGUF plus its matching
projection GGUF. The result owns its generated UTF-8 text and token metadata:

```cpp
imagecpp_vlm_model_options vlm_options{};
imagecpp_vlm_model_options_init(&vlm_options);
vlm_options.model_path = "models/SmolVLM-256M-Instruct-Q8_0.gguf";
vlm_options.projection_model_path =
    "models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf";
imagecpp::Model vlm(runtime, vlm_options);

imagecpp_visual_query_options query{};
imagecpp_visual_query_options_init(&query);
query.prompt = "What is the main subject? Answer briefly.";
query.temperature = 0.0F;

imagecpp::TextResult answer = imagecpp::visual_query(vlm, input, query);
imagecpp::TextInfo text = answer.info();
// text owns a copied UTF-8 answer plus token metadata.
```

Use the streaming overload when an interactive caller should receive text as
soon as it is decoded. Returning `false` from the callback stops generation;
the returned result contains the emitted prefix and reports
`IMAGECPP_TEXT_FINISH_CANCELLED`:

```cpp
imagecpp::TextResult streamed = imagecpp::visual_query_stream(
    vlm, input, query, [](std::string_view chunk) {
        std::cout << chunk << std::flush;
        return true;
    });
```

The C ABI exposes the same behavior through
`imagecpp_visual_query_stream`. Callback fragments carry an explicit byte
count and are not NUL-terminated; concatenating them produces exactly the text
owned by the final result. A callback must not re-enter the same model while a
query is running.

## Serve the native image API

The same binary exposes model-free transforms and whichever model families are
configured at startup:

```sh
./build/imagecpp serve \
  --vlm-model models/SmolVLM-256M-Instruct-Q8_0.gguf \
  --vlm-projection models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf \
  --segment-model models/edgetam_q4_0.ggml \
  --upscaler-model models/RealESRGAN_x4plus_anime_6B.pth \
  --model-cache-size 2 --job-workers 2

# Then open http://127.0.0.1:8080/playground in a browser.

curl --fail -F image=@input.jpg \
  -F 'question=What objects are visible?' \
  http://127.0.0.1:8080/v1/ask

curl --no-buffer -H 'Accept: text/event-stream' \
  -F image=@input.jpg \
  -F 'prompt=Describe this image in detail.' \
  http://127.0.0.1:8080/v1/caption

curl --fail -F image=@input.jpg -F 'points=[[640,420,true]]' \
  -F upscale=4 http://127.0.0.1:8080/v1/cutout --output cutout.png

# Queue work without holding the request open, then poll the returned status_url.
curl --fail -H 'Prefer: respond-async' -F image=@input.jpg \
  -F 'points=[[640,420,true]]' http://127.0.0.1:8080/v1/cutout
```

`GET /healthz` reports configured model families, and `GET /v1/operations`
returns the operations compiled into this binary. Model paths are fixed at
startup; clients cannot select arbitrary server files. The server accepts raw
image bodies and multipart uploads, limits requests to 32 MiB and outputs to
about 67 megapixels by default, and binds only to `127.0.0.1` unless explicitly
configured otherwise. The browser playground is embedded in the executable and
calls these same endpoints. Its native Jobs tray can queue any operation,
cancel waiting work, reopen retained results, release warm non-VLM models, and
store parameter-only presets in browser local storage. It does not add a Node,
Python, CDN, or runtime-file dependency. See the complete
[HTTP API](http-api.md).

The same ownership model applies to semantic results:

```cpp
imagecpp_model_options clip_options{};
imagecpp_model_options_init(&clip_options);
clip_options.model_path = "models/clip-vit-b-32-laion2b-q4_0.gguf";
imagecpp::Model clip(runtime, "image.embed.clip", clip_options);

imagecpp::EmbeddingResult vector = imagecpp::embed_image(clip, input);
imagecpp::ClassificationResult labels =
    imagecpp::classify(clip, input, {"cat", "dog", "bicycle"});
imagecpp::ClassificationInfo best = labels.at(0);
```

Open-vocabulary detection reuses the same encoded-image session for successive
concept prompts:

```cpp
imagecpp_model_options sam3_options{};
imagecpp_model_options_init(&sam3_options);
sam3_options.model_path = "models/sam3-q4_0.ggml";
imagecpp::Model detector(runtime, "image.detect.sam3", sam3_options);
imagecpp::Session detection_session(detector);
detection_session.set_image(input);

imagecpp_detect_options detect_options{};
imagecpp_detect_options_init(&detect_options);
detect_options.prompt = "yellow school bus";
detect_options.score_threshold = 0.4F;

imagecpp::DetectionResult detections = detection_session.detect(detect_options);
for (size_t index = 0; index < detections.size(); ++index) {
    imagecpp::DetectionInfo instance = detections.at(index);
    // instance.box, instance.score, and instance.mask are typed artifacts.
}
```

The grounded cutout workflow performs detection, instance selection, crop,
optional upscale, and alpha composition in one owned result:

```cpp
imagecpp_upscaler_model_options grounded_upscale_options{};
imagecpp_upscaler_model_options_init(&grounded_upscale_options);
grounded_upscale_options.model_path = "models/RealESRGAN_x4plus_anime_6B.pth";
grounded_upscale_options.tile_size = 128;
imagecpp::Model grounded_upscaler(runtime, grounded_upscale_options);

imagecpp_grounded_cutout_options grounded_options{};
imagecpp_grounded_cutout_options_init(&grounded_options);
grounded_options.detect.prompt = "yellow school bus";
grounded_options.detect.score_threshold = 0.4F;
grounded_options.selection = IMAGECPP_GROUNDED_CUTOUT_ALL;
grounded_options.padding = 16;
grounded_options.upscale_factor = 4;

imagecpp::GroundedCutoutResult grounded =
    imagecpp::grounded_cutout(detection_session, &grounded_upscaler, input,
                              grounded_options);
imagecpp::GroundedCutoutInfo grounded_info = grounded.info();
// grounded_info.image is RGBA; mask and source_box describe the exact asset.
```

Typed workflows use the same reusable model and session handles:

```cpp
imagecpp_model_options sam_options{};
imagecpp_model_options_init(&sam_options);
sam_options.model_path = "models/edgetam_q4_0.ggml";
imagecpp::Model sam(runtime, "image.segment.sam", sam_options);
imagecpp::Session sam_session(sam);

imagecpp_upscaler_model_options upscale_options{};
imagecpp_upscaler_model_options_init(&upscale_options);
upscale_options.model_path = "models/RealESRGAN_x4plus_anime_6B.pth";
imagecpp::Model upscaler(runtime, upscale_options);

imagecpp_point_prompt point{640.0F, 420.0F, 1};
imagecpp_cutout_options cutout_options{};
imagecpp_cutout_options_init(&cutout_options);
cutout_options.segment.points = &point;
cutout_options.segment.point_count = 1;
cutout_options.padding = 16;
cutout_options.upscale_factor = 4;

imagecpp::CutoutResult cutout =
    imagecpp::cutout(sam_session, &upscaler, input, cutout_options);
imagecpp::CutoutInfo cutout_info = cutout.info();
// cutout_info.image and cutout_info.mask remain valid while cutout is alive.
```

Installed consumers use `find_package(imagecpp CONFIG REQUIRED)` and link
`imagecpp::imagecpp`. The stable C ABI in `imagecpp/imagecpp.h` exposes the same
ownership and inference operations for C, Rust, Go, and other FFI callers.

See [model provenance and benchmarks](models.md) for exact checksums, licenses,
validated hardware, and split-component diffusion options.
