# image.cpp

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

`image.cpp` is a native C/C++ runtime for local image intelligence. The goal is
one embeddable library, one command-line tool, and eventually one server for
generation, editing, understanding, restoration, and composable image
workflows—without requiring Python at runtime.

The repository currently provides the model-independent runtime foundation,
native PNG/JPEG/WebP/BMP/TGA codecs, promptable SAM 2/SAM 3/EdgeTAM
segmentation, transparent background removal, diffusion generation and editing,
Depth Anything 2/3 estimation with optional camera pose, CLIP image/text
embeddings and zero-shot classification, and ESRGAN upscaling.
See [the architecture](docs/architecture.md) for the
project boundary and roadmap.

Start with the [clean-checkout quickstart](docs/quickstart.md), including the
small starter model bundle and optional local generation bundle.

## Intended capabilities

- generation, editing, inpainting, outpainting, and conditioning;
- embeddings, similarity, classification, and tagging;
- detection, grounding, segmentation, matting, and tracking;
- depth, normals, pose, and keypoints;
- OCR, document layout, and captioning;
- upscale, restoration, denoise, deblur, and colorization; and
- native image utilities and typed multi-step workflows.

## Build

`image.cpp` requires a C11 and C++17 compiler plus CMake 3.20 or newer. Clone
the pinned codec dependencies with the repository:

```sh
git submodule update --init --recursive
cmake -S . -B build -DIMAGECPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

WebP support is on by default and can be removed with
`-DIMAGECPP_WITH_WEBP=OFF`. The default static package installs its pinned WebP
dependency alongside `libimagecpp`, so downstream CMake consumers do not
silently depend on a system copy.

The model-backed composite is on by default. Use
`-DIMAGECPP_WITH_SAM3=OFF` or `-DIMAGECPP_WITH_STABLE_DIFFUSION=OFF` for a
smaller custom build; depth can be removed with
`-DIMAGECPP_WITH_DEPTH_ANYTHING=OFF`, and CLIP with
`-DIMAGECPP_WITH_CLIP=OFF`. All providers compile against one pinned GGML
runtime.

The runtime is implemented in-process. Development-time conversion and parity
tools may use reference implementations, but shipped inference will not invoke
Python, shell commands, helper executables, or sidecars.

## Current usage

Inspect the operations compiled into the runtime:

```sh
./build/imagecpp inspect
```

Resize between common image formats; the output extension selects the encoder:

```sh
./build/imagecpp resize input.jpg output.webp 1024x1024 bilinear
```

Download the validated 15 MB EdgeTAM model and run real point- or box-prompted
segmentation:

```sh
cmake --build build --target imagecpp_model_edgetam
./build/imagecpp segment models/edgetam_q4_0.ggml input.jpg mask.png --point 640,420
./build/imagecpp remove-background models/edgetam_q4_0.ggml input.jpg cutout.png --box 120,80,1100,760
```

See [models](docs/models.md) for checksums, provenance, model licensing, device
selection, and reusable session behavior.

Estimate a dense depth map. The library returns the raw float map, confidence,
metric/relative flag, and optional camera matrices; the CLI writes a normalized
grayscale preview:

```sh
cmake --build build --target imagecpp_model_depth_anything
./build/imagecpp depth models/depth-anything-base-q4_k.gguf input.jpg depth.png --pose
```

Download the 86 MB two-tower CLIP model once, then produce normalized joint
embeddings or classify an image against labels supplied at runtime:

```sh
cmake --build build --target imagecpp_model_clip
./build/imagecpp embed-image models/clip-vit-b-32-laion2b-q4_0.gguf input.jpg
./build/imagecpp embed-text models/clip-vit-b-32-laion2b-q4_0.gguf "a photo of a red bicycle"
./build/imagecpp classify models/clip-vit-b-32-laion2b-q4_0.gguf input.jpg cat dog bicycle
```

Embedding output is JSON containing a normalized 512-element vector.
Classification output is sorted, tab-separated `label` and probability data;
probabilities are normalized over the labels supplied in that invocation.

Download the validated generation and upscale models, then use the same binary
for text-to-image, img2img/inpainting, and ESRGAN:

```sh
cmake --build build --target imagecpp_model_sd15_q4 imagecpp_model_realesrgan
./build/imagecpp generate models/v1-5-pruned_Q4_0.gguf output.png "an orange cat on a windowsill" --seed 42
./build/imagecpp edit models/v1-5-pruned_Q4_0.gguf input.png edited.png "a watercolor painting" --strength 0.45
./build/imagecpp upscale models/RealESRGAN_x4plus_anime_6B.pth input.png upscaled.png --factor 4
```

The CLI intentionally consumes only the installed C++ wrapper. Applications
can use the same library directly:

```cpp
#include <imagecpp/imagecpp.hpp>

imagecpp::Image image = imagecpp::load("input.png");
imagecpp::Blob encoded = imagecpp::encode(image, IMAGECPP_FILE_FORMAT_WEBP);
imagecpp::save("copy.png", image);

imagecpp::Runtime runtime;
for (const imagecpp::OperationInfo & operation : runtime.operations()) {
    // Discover built-in and model-backed operations here.
}
```

The C API offers the same file, memory codec, and inference operations. All ABI
structs passed as outputs must have `struct_size` initialized by the caller.
Every opaque image, blob, embedding, and classification result has a matching
`imagecpp_*_destroy` function; the C++ wrapper handles all of them with RAII.

## Project status

The public API will evolve during the foundation phase. Model weights remain
external assets and will use self-describing GGUF metadata or versioned package
manifests for multi-component families.

## License

`image.cpp` is available under the [MIT License](LICENSE). Model weights and
third-party dependencies retain their own licenses; see
[third-party notices](THIRD_PARTY.md) and [models](docs/models.md).
