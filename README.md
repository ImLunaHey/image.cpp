# image.cpp

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

## Project status

The public API will evolve during the foundation phase. Model weights remain
external assets and will use self-describing GGUF metadata or versioned package
manifests for multi-component families.

