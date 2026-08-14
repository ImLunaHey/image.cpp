# image.cpp

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

`image.cpp` is a native C/C++ runtime for local image intelligence. The goal is
one embeddable library, one command-line tool, and eventually one server for
generation, editing, understanding, restoration, and composable image
workflows—without requiring Python at runtime.

The repository is in its foundation phase. The initial implementation provides
the public image types, runtime introspection, image validation, allocation,
and resizing needed by future model families. See
[the architecture](docs/architecture.md) for the project boundary and roadmap.

## Intended capabilities

- generation, editing, inpainting, outpainting, and conditioning;
- embeddings, similarity, classification, and tagging;
- detection, grounding, segmentation, matting, and tracking;
- depth, normals, pose, and keypoints;
- OCR, document layout, and captioning;
- upscale, restoration, denoise, deblur, and colorization; and
- native image utilities and typed multi-step workflows.

## Build

`image.cpp` requires a C11 and C++17 compiler plus CMake 3.20 or newer.

```sh
cmake -S . -B build -DIMAGECPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The runtime is implemented in-process. Development-time conversion and parity
tools may use reference implementations, but shipped inference will not invoke
Python, shell commands, helper executables, or sidecars.

## Current usage

Inspect the operations compiled into the runtime:

```sh
./build/imagecpp inspect
```

The foundation CLI can resize binary PGM/PPM images while the codec layer is
being established:

```sh
./build/imagecpp resize input.ppm output.ppm 1024x1024 bilinear
```

The CLI intentionally consumes only the installed C++ wrapper. Applications
can use the same library directly:

```cpp
#include <imagecpp/imagecpp.hpp>

imagecpp_image_desc description{
    sizeof(imagecpp_image_desc),
    512,
    512,
    0,
    IMAGECPP_PIXEL_FORMAT_RGB_U8,
    IMAGECPP_COLOR_SPACE_SRGB,
};

imagecpp::Image image(description);
imagecpp::Runtime runtime;
for (const imagecpp::OperationInfo & operation : runtime.operations()) {
    // Discover built-in and model-backed operations here.
}
```

All ABI structs passed as outputs must have `struct_size` initialized by the
caller. Images allocated by the library must be released with
`imagecpp_image_destroy`; the C++ wrapper handles that ownership with RAII.

## Project status

The public API will evolve during the foundation phase. Model weights remain
external assets and will use self-describing GGUF metadata or versioned package
manifests for multi-component families.

## License

`image.cpp` is available under the [MIT License](LICENSE). Model weights and
optional providers retain their own licenses, recorded separately from the
runtime license.
