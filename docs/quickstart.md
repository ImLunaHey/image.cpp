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

The starter bundle is about 131 MB and enables promptable segmentation,
background removal, dense depth with optional camera pose, and 4x upscaling:

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

./build/imagecpp upscale \
  models/RealESRGAN_x4plus_anime_6B.pth input.jpg upscaled.png --factor 4
```

Prompt coordinates are image pixels. A positive point should be inside the
object; add repeatable `--negative x,y` points outside it or use
`--box x0,y0,x1,y1` when the initial mask is ambiguous.

## Add local generation and editing

The full bundle adds the 3.05 GB Stable Diffusion 1.5 Q4_0 checkpoint:

```sh
cmake --build build --target imagecpp_models_full

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

Installed consumers use `find_package(imagecpp CONFIG REQUIRED)` and link
`imagecpp::imagecpp`. The stable C ABI in `imagecpp/imagecpp.h` exposes the same
ownership and inference operations for C, Rust, Go, and other FFI callers.

See [model provenance and benchmarks](models.md) for exact checksums, licenses,
validated hardware, and split-component diffusion options.
