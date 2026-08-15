# Third-party notices

`image.cpp` is MIT-licensed. It includes pinned third-party source dependencies
whose own notices and license texts remain authoritative:

| Component | Use | License | License text |
| --- | --- | --- | --- |
| stb | PNG/JPEG/BMP/TGA codecs | MIT or public domain | `external/stb/LICENSE` |
| libwebp | WebP codec | BSD 3-Clause | `external/libwebp/COPYING` |
| Tesseract | OCR and document layout analysis | Apache 2.0 | `external/tesseract/LICENSE` |
| Leptonica | Tesseract image processing primitives | BSD 2-Clause | `external/leptonica/leptonica-license.txt` |
| sam3.cpp | SAM 2/SAM 3/EdgeTAM inference | MIT | `external/sam3/LICENSE` |
| stable-diffusion.cpp | generation, editing, and ESRGAN inference | MIT | `external/stable-diffusion/LICENSE` |
| depth-anything.cpp | Depth Anything 2/3 inference and preprocessing | MIT | `external/depth-anything/LICENSE` |
| clip.cpp-derived engine | CLIP image/text inference and preprocessing | MIT | `external/clipcpp/LICENSE` |
| GGML | tensor runtime and device backends | MIT | `external/ggml/LICENSE` |

Model weights are downloaded separately and are not covered by the image.cpp
license. Their provenance, checksums, and licenses are recorded in
[`docs/models.md`](docs/models.md).

Redistributors must preserve the applicable third-party notices. The complete
license text for each pinned or vendored dependency is present in its source
directory.
