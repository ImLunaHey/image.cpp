# Models

Model weights are external to the MIT-licensed `image.cpp` source tree. Each
validated model has a pinned download, checksum, upstream provenance, and its
own license.

## EdgeTAM Q4

The starter segmentation model is `edgetam_q4_0.ggml` (15 MB). Download and
verify it with CMake:

```sh
cmake --build build --target imagecpp_model_edgetam
```

The file is written to `models/edgetam_q4_0.ggml` and is not committed. Its
SHA-256 is
`a8a35e35fb9a1b6f099c3f35e3024548b0fc979c2a4184642562804192496e09`.
It was converted and quantized by the pinned `sam3.cpp` provider from the
official EdgeTAM checkpoint. EdgeTAM code and checkpoints are Apache-2.0;
`sam3.cpp` is MIT.

Point-prompted segmentation:

```sh
./build/imagecpp segment \
  models/edgetam_q4_0.ggml input.jpg mask.png \
  --point 640,420 --multimask
```

Box-prompted background removal:

```sh
./build/imagecpp remove-background \
  models/edgetam_q4_0.ggml input.jpg cutout.png \
  --box 120,80,1100,760
```

Metal is selected automatically on Apple hardware. Pass `--cpu` for CPU-only
execution or `--gpu` to require the GPU-capable provider build. A loaded model
and encoded image are reusable through the C and C++ session APIs, so an
interactive application does not pay load and image-encoding costs for every
prompt.

## Depth Anything 3 Base Q4_K

The starter understanding model is the 104 MB
`depth-anything-base-q4_k.gguf`. It produces dense relative depth and
confidence maps and can also recover camera extrinsics and intrinsics. Download
and verify it with:

```sh
cmake --build build --target imagecpp_model_depth_anything
```

Its SHA-256 is
`43cd45d00f9024f4319f4beabd73155db5132e4b575bc52eff4131262c9d78f1`.
The file comes from the `mudler/depth-anything.cpp-gguf` repository and was
converted from the Apache-2.0 Depth Anything 3 Base checkpoint. The native
provider is MIT licensed.

Write an 8-bit visualization, with nearer regions brighter by default:

```sh
./build/imagecpp depth \
  models/depth-anything-base-q4_k.gguf input.jpg depth.png --pose
```

Use `--no-invert` to make farther regions brighter. The C and C++ APIs retain
the raw `GRAY_F32` depth and confidence maps, the metric/relative flag, and the
optional 3x4 extrinsic and 3x3 intrinsic matrices. Model preprocessing may
change the output dimensions; the returned views report the exact processed
size. Depth Anything currently selects the best compiled device automatically.
The same provider accepts compatible self-describing Depth Anything 2 and 3
GGUFs, including metric models.

## Stable Diffusion 1.5 Q4_0

The validated starter generation model is `v1-5-pruned_Q4_0.gguf` (3.05 GB).
It is a community GGUF conversion of Stable Diffusion 1.5 and is distributed
under the CreativeML Open RAIL-M license inherited from the original model.
Download and checksum it with:

```sh
cmake --build build --target imagecpp_model_sd15_q4
```

Its SHA-256 is
`24bcd54c1d1f0354a1cd19d07ad9a20771d43fde8318d505d71bcd84b078f20a`.
The source repository revision recorded by Hugging Face when validated was
`101ff06c98bea9ac8affe6a2c867e9762958d219`.

Text-to-image generation:

```sh
./build/imagecpp generate \
  models/v1-5-pruned_Q4_0.gguf output.png \
  "a small orange cat on a windowsill, detailed photograph" \
  --size 512x512 --steps 20 --seed 42
```

Image-to-image editing and mask-guided inpainting use the same loaded model:

```sh
./build/imagecpp edit \
  models/v1-5-pruned_Q4_0.gguf input.png edited.png \
  "an orange cat wearing round red sunglasses" \
  --strength 0.45 --seed 43

./build/imagecpp edit \
  models/v1-5-pruned_Q4_0.gguf input.png inpainted.png \
  "an orange cat wearing round red sunglasses" \
  --mask mask.png --strength 0.9 --seed 45
```

The starter SD 1.5 checkpoint validates the edit path, but a checkpoint trained
specifically for inpainting gives better preservation outside the mask. The
mask must match the input dimensions; white pixels select the region to replace.

The typed library options also accept separate diffusion, VAE, CLIP-L, CLIP-G,
T5XXL, and LLM component paths for compatible modern model families. The CLI
exposes these as `--diffusion-model`, `--vae`, `--clip-l`, `--clip-g`,
`--t5xxl`, and `--llm`; pass `-` as the positional checkpoint when using
split components.

## RealESRGAN x4plus anime 6B

The validated starter upscaler is the compact 17 MB
`RealESRGAN_x4plus_anime_6B.pth` published by the Real-ESRGAN project. The
upstream project and provider code are BSD-3-Clause and MIT respectively.
Download and verify the model with:

```sh
cmake --build build --target imagecpp_model_realesrgan
```

Its SHA-256 is
`f872d837d3c90ed2e05227bed711af5671a6fd1c9f7d7e91c911a61f155e99da`.
Run it with:

```sh
./build/imagecpp upscale \
  models/RealESRGAN_x4plus_anime_6B.pth input.png upscaled.png \
  --factor 4
```

## Validated Apple M4 baseline

The default Metal build was exercised on an Apple M4 with 16 GB unified
memory. These are end-to-end CLI measurements, including model load and PNG
I/O:

| Operation | Input | Time | Peak footprint |
| --- | --- | ---: | ---: |
| EdgeTAM background removal | 1800x1200 JPEG | 1.06 s warm | 495 MB |
| Depth Anything 3 Base Q4_K | 512x512 PNG + pose | 8.18 s cold | 644 MB |
| SD 1.5 Q4 text-to-image | 512x512, 8 steps | 15.75 s | 3.39 GB |
| SD 1.5 Q4 img2img | 512x512, 4 effective steps | 12.68 s | 3.64 GB |
| RealESRGAN x4 | 2x2 fixture to 8x8 | 1.05 s | 78 MB |

The deterministic text-to-image validation used seed 42 and produced a valid
512x512 PNG with SHA-256
`94c056b2b33d0d83677859ab7991cdc5de4f3708228eb2febaf98056aad6d16b`.
Performance varies by model, backend, dimensions, sampler, and cache state.
