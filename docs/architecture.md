# image.cpp architecture

## Product boundary

`image.cpp` is the image-domain counterpart to broad native runtimes such as
`audio.cpp`: a shared runtime for many model families and task shapes, rather
than a single diffusion implementation or a collection of unrelated examples.

The user-facing distribution consists of:

- `libimagecpp`, an embeddable library with a stable C ABI;
- `imagecpp`, a CLI built only from the library's public interface;
- optional model-family composites selected at build time; and
- later, an HTTP server and embedded Web UI using the same library.

Model weights are external. "One binary" means users do not need a Python
environment, subprocess-based adapter, model-specific executable, or sidecar.

## Design principles

1. **Native and in-process.** Production operations use C/C++ libraries and
   native platform APIs. Reference Python is development tooling only.
2. **Library first.** The CLI, server, and UI consume the same narrow public
   API available to third-party applications.
3. **Typed artifacts.** Images, masks, boxes, keypoints, depth maps, text
   regions, embeddings, and future 3D outputs retain their domain semantics.
4. **Typed operations.** Task-specific requests and results sit above common
   model/session lifecycle primitives. A single catch-all request structure is
   not the permanent API.
5. **Exact preprocessing.** Orientation, color space, alpha, resize, crop,
   normalization, and tensor layout are part of model parity—not incidental
   CLI behavior.
6. **Reusable sessions.** Loaded weights, compiled graphs, workspaces, and
   cached image embeddings survive across requests where the model permits it.
7. **One compute runtime.** All built-in tensor providers use one selected,
   pinned GGML source tree and common backend/device selection. With VLM support
   enabled this is llama.cpp's GGML; provider-local GGML submodules are never
   compiled into `image.cpp`.
8. **Evidence with every model.** A model family needs reference parity,
   golden outputs or metrics, reproducible commands, latency, and peak-memory
   measurements before it becomes a supported core family.

## Artifact model

The workflow layer will connect operations through a closed set of typed
artifacts rather than unstructured JSON blobs:

| Artifact | Essential representation |
| --- | --- |
| Image | dimensions, row stride, pixel format, color space, bytes |
| Mask | dimensions, row stride, scalar type, semantic meaning |
| Boxes | coordinates, coordinate space, labels, confidence |
| Keypoints | coordinates, visibility/confidence, optional skeleton |
| Depth map | dimensions, scalar type, relative/metric units, confidence |
| Text regions | hierarchy, boxes, baselines, text, language, confidence, orientation |
| Generated text | UTF-8 text, prompt/generated token counts, finish reason |
| Embedding | element type, dimensions, normalization, semantic space |
| Metadata | namespaced typed properties and provenance |

An operation declares accepted input artifacts, emitted output artifacts,
capabilities, and option schema. This supports pipelines such as:

```text
detect -> crop -> upscale
segment -> remove-background
depth -> control-condition -> generate
ocr -> translate -> redraw
caption -> prompt-enhance -> edit
```

The detection, crop, upscale, segmentation, and remove-background primitives
in the first two example paths are now implemented. Typed compositions cover
both coordinate-prompted cutouts and text-grounded best/all-instance asset
extraction. A general programmatic graph will build on the same artifact
contracts.

## Runtime layers

```text
public C ABI and C++ wrapper
             |
task APIs and typed workflow operations
             |
model registry, packages, sessions, jobs
             |
shared vision modules and image preprocessing
             |
GGML graph execution, memory, and device backends
```

Source code follows the same dependency direction. Model implementations may
depend on shared modules and the core runtime; the core never depends on a
specific model family. Applications depend on public headers, never private
model headers.

## Public API direction

The C ABI uses opaque runtime, model, session, and job handles. Plain structs
cross the ABI only when their size, ownership, and lifetime are explicit. The
C++ wrapper owns handles with RAII and offers task-specific value types.

Every image view specifies:

- width and height;
- row stride and total accessible byte size;
- pixel format and color space; and
- mutable or immutable ownership semantics.

This avoids assuming tightly packed RGB and permits zero-copy host integration.
Device-resident image/tensor interop will be added as a separate explicit API.

## Model packages

A single-component model should be self-describing through GGUF metadata. A
multi-component family uses a versioned package manifest that describes:

- family, architecture, variant, and supported tasks;
- weights and auxiliary components;
- preprocessing and postprocessing contracts;
- default precision and supported quantization;
- adapter, tokenizer, vocabulary, and configuration assets;
- upstream source, model license, checksums, and provenance; and
- validated backends, limits, and expected output metrics.

GGUF is a tensor container, not a universal architecture adapter. Loading a
file requires a registered implementation matching its architecture metadata.

## Build composites

- `core`: image primitives, package inspection, runtime, and no large model
  families;
- `full`: all maintained core model families and native providers; and
- `custom`: an explicit family list for applications with size constraints.

Community model families begin outside the core composite while retaining the
same parity and packaging contract.

## Initial vertical slice

The first useful release should provide one binary containing:

1. CLIP embeddings and zero-shot classification (implemented);
2. SAM 2 or EdgeTAM segmentation (implemented);
3. Depth Anything depth estimation (implemented);
4. ESRGAN-class upscaling (implemented);
5. generation and editing through an in-process native provider (implemented); and
6. captioning and visual question answering (implemented); and
7. at least one typed workflow joining multiple operations (implemented:
   prompted segmentation -> crop -> optional upscale -> alpha cutout).

Native OCR and document layout are also implemented through the Tesseract
library API, with caller-owned image buffers and an in-memory traineddata
model. No Tesseract helper executable is built or invoked.

Native captioning and visual question answering are implemented through the
pinned llama.cpp `libllama` and `libmtmd` libraries. The public boundary is a
task-specific text result; llama.cpp examples, tools, server, subprocess, and
video components are not built or invoked.

Foundation work lands first: correct image buffers, resize/crop/normalize,
runtime introspection, errors, cancellation, tests, and packaging boundaries.

## Deliberate deferrals

- Dynamic binary plugins are deferred until the compile-time provider contract
  has proven stable.
- Video is represented later as a timed sequence of image and audio artifacts;
  it will not distort the initial still-image API.
- A node editor is not required for workflows. JSON and programmatic graphs
  come first; a UI can render the same typed operation schema later.
- Network model download is tooling, not an implicit side effect of model load.
