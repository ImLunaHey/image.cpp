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
