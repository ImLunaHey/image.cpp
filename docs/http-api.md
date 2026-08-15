# HTTP API

`imagecpp serve` runs the native image API inside the normal `imagecpp`
process. It calls the same public C++ library as the CLI and never invokes a
shell, Python, another `imagecpp` process, or a provider's example server.

## Start the server

Only configure the model families the process should expose:

```sh
./build/imagecpp serve \
  --host 127.0.0.1 --port 8080 \
  --vlm-model models/SmolVLM-256M-Instruct-Q8_0.gguf \
  --vlm-projection models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf \
  --segment-model models/edgetam_q4_0.ggml \
  --detect-model models/sam3-q4_0.ggml \
  --depth-model models/depth-anything-base-q4_k.gguf \
  --clip-model models/clip-vit-b-32-laion2b-q4_0.gguf \
  --ocr-model models/eng.traineddata \
  --diffusion-checkpoint models/v1-5-pruned_Q4_0.gguf \
  --upscaler-model models/RealESRGAN_x4plus_anime_6B.pth
```

Model paths are accepted only at startup. HTTP clients cannot select an
arbitrary file from the server's filesystem. The VLM remains resident because
it owns a reusable language context. Other configured families use a bounded
least-recently-used cache: one family remains warm by default, while evicted
models stay alive until any in-flight request releases its shared lease.
Model-backed requests are serialized because provider instances are not yet
promised to be thread-safe; health and non-model routes remain available
concurrently.

| Option | Default | Meaning |
| --- | --- | --- |
| `--host`, `--port` | `127.0.0.1`, `8080` | Bind address and port |
| `--max-upload-mb` | `32` | Maximum complete request size |
| `--max-output-mp` | `67` | Maximum output pixels, in decimal megapixels |
| `--threads` | automatic | CPU worker threads |
| `--cpu`, `--gpu` | automatic | Preferred model compute device; automatic is best for mixed families |
| `--model-cache-size` | `1` | Warm non-VLM model families; `0` loads per request and retains none |
| `--job-workers` | `1` | Native background workers, at most 16 |
| `--job-queue` | `16` | Maximum waiting jobs before submissions return `429` |
| `--job-retain` | `64` | Maximum retained terminal jobs and response bodies |
| `--job-ttl` | `900` | Terminal-job retention in seconds |
| `--vlm-model`, `--vlm-projection` | unset | Matching VLM language and projection GGUFs |
| `--segment-model` | unset | SAM 2, SAM 3, or EdgeTAM model |
| `--detect-model` | unset | Full SAM 3 open-vocabulary model |
| `--depth-model` | unset | Depth Anything model |
| `--clip-model` | unset | Two-tower CLIP GGUF |
| `--ocr-model` | unset | Tesseract traineddata file |
| `--diffusion-checkpoint` | unset | Monolithic diffusion model |
| `--diffusion-model`, `--vae`, `--clip-l`, `--clip-g`, `--t5xxl`, `--llm` | unset | Split diffusion components |
| `--upscaler-model`, `--upscaler-tile` | unset, `128` | ESRGAN model and tile size |

Run `imagecpp serve --help` for the remaining VLM and diffusion loading
controls.

## Embedded playground

Open `http://127.0.0.1:8080/playground` after starting the service. The studio
exposes all 16 image and text operations, reports which model families are
configured, accepts drag/drop or clipboard images, previews image and text
results, and downloads image or JSON responses. Segmentation and cutout support
positive click prompts and Shift-click negative prompts in source-image
coordinates. Caption and VQA can render their SSE response incrementally.

Every operation can instead run in the native background queue. The Jobs tray
shows queue position, lifecycle progress, cancellation, retained results, and
model-cache state without blocking the current workspace. Parameter presets
are stored only in that browser's local storage; input image bytes and model
paths are never stored in a preset.

The HTML, CSS, and JavaScript are compiled into the `imagecpp` executable. No
Node runtime, frontend server, CDN, network font, or asset directory is needed.
A browser request to `/` also receives the playground; clients without an
`Accept: text/html` header retain the service-info JSON. `GET /v1/info` always
returns that JSON explicitly.

## Endpoint map

| Endpoint | Result | Required startup model |
| --- | --- | --- |
| `GET /playground` | Embedded browser studio | none |
| `GET /healthz` | Process and configured-model status | none |
| `GET /v1/info` | Service version and endpoint list | none |
| `GET /v1/operations` | Operations compiled into the binary | none |
| `GET /v1/models` | Model cache and resident-VLM state | none |
| `DELETE /v1/models/cache` | Release cached non-VLM models | none |
| `GET /v1/jobs` | Recent and active background jobs | none |
| `GET /v1/jobs/{id}` | One job's lifecycle state | none |
| `GET /v1/jobs/{id}/result` | Original response after completion | none |
| `DELETE /v1/jobs/{id}` | Request cancellation | none |
| `POST /v1/resize` | Encoded image | none |
| `POST /v1/ocr` | Text and document-region JSON | OCR |
| `POST /v1/depth` | Depth JSON or encoded visualization | depth |
| `POST /v1/embed/image` | Embedding JSON | CLIP |
| `POST /v1/embed/text` | Embedding JSON | CLIP |
| `POST /v1/classify` | Ranked label JSON | CLIP |
| `POST /v1/segment` | Mask and score JSON | segment |
| `POST /v1/detect` | Instance boxes, masks, and scores | detect |
| `POST /v1/cutout` | Prompted transparent image | segment; upscaler if requested |
| `POST /v1/remove-background` | Alias of `/v1/cutout` | segment; upscaler if requested |
| `POST /v1/extract` | Text-grounded transparent image | detect; upscaler if requested |
| `POST /v1/generate` | Generated image(s) | diffusion |
| `POST /v1/edit` | Edited image(s) | diffusion |
| `POST /v1/upscale` | Upscaled image | upscaler |
| `POST /v1/caption` | Text JSON or SSE | VLM |
| `POST /v1/ask` | Text JSON or SSE | VLM |

Unconfigured model endpoints return `503 model_not_configured`. Caption and
VQA retain `503 model_not_loaded` when their paired VLM files are absent.

## Background jobs

All 16 `POST` operations accept `Prefer: respond-async`. The equivalent
`async=true` query or multipart field is also supported. A successful
submission returns `202 Accepted`, `Preference-Applied: respond-async`, and a
`Location` header naming the job:

```sh
curl --fail -D job-headers.txt -H 'Prefer: respond-async' \
  -F image=@input.jpg -F prompt=cat \
  http://127.0.0.1:8080/v1/detect
```

```json
{
  "id": "job-0000000000000001",
  "operation": "detect",
  "status": "queued",
  "progress": 0.0,
  "stage": "queued",
  "queue_position": 1,
  "status_url": "/v1/jobs/job-0000000000000001",
  "result_url": "/v1/jobs/job-0000000000000001/result"
}
```

Poll `status_url` until `status` is `completed`, `failed`, or `cancelled`, then
fetch `result_url`. The result preserves the original HTTP status, media type,
body, and application response headers, so a completed PNG job is still a PNG
and a provider error retains its normal JSON envelope. `GET /v1/jobs?limit=50`
lists newest jobs first and includes queue capacity and worker counts. Limits
above 100 are clamped; invalid limits return `400`.

`DELETE status_url` cancels a waiting job immediately. A running job moves to
`cancellation_requested`; current non-VLM provider kernels cannot yet be
preempted, so their output is discarded and the job becomes `cancelled` when
the provider returns. Existing foreground VLM SSE retains cooperative
disconnect cancellation. Async SSE is deliberately rejected with
`400 async_stream_unsupported`; a background caption or VQA job returns its
complete JSON result instead.

Terminal jobs and their response bodies are bounded by both `--job-retain` and
`--job-ttl`. A result that has expired returns `404 job_not_found`. A full
waiting queue returns `429 job_queue_full` with `Retry-After: 1`.

## Model lifecycle

`GET /v1/models` reports cache capacity, loaded families in most-recently-used
order, hits, misses, evictions, clears, and whether the paired VLM is resident.
The same cache and job counters are included in `GET /healthz`.

```sh
curl --fail http://127.0.0.1:8080/v1/models
curl --fail -X DELETE http://127.0.0.1:8080/v1/models/cache
```

Clearing the cache removes future reuse immediately. Shared leases keep any
currently executing request safe; the model is destroyed after that request
releases its lease. The resident VLM is not affected by this endpoint.

## Image requests and responses

Most image endpoints accept either the encoded image as the complete request
body or `multipart/form-data` with a file field named `image`. PNG, JPEG, WebP,
BMP, and TGA use the native library decoder. Parameters may be query parameters
or multipart fields; multipart fields take precedence.

Image-producing routes default to PNG. Set `format=png|jpeg|webp|bmp|tga`,
`quality=1..100`, and `lossless=true|false` where applicable. Workflow,
generation, and upscaling routes accept `response=json` to return this object:

```json
{
  "image": {
    "format": "png",
    "mime_type": "image/png",
    "width": 512,
    "height": 512,
    "base64": "iVBORw0KGgo..."
  }
}
```

## Resize

```sh
curl --fail --data-binary @input.jpg -H 'Content-Type: image/jpeg' \
  'http://127.0.0.1:8080/v1/resize?width=1024&height=768&filter=bilinear' \
  --output resized.png
```

`width` and `height` are required. `filter` is `nearest` or `bilinear`.

## OCR, depth, embeddings, and classification

```sh
curl --fail -F image=@document.png -F psm=auto -F dpi=300 \
  http://127.0.0.1:8080/v1/ocr

curl --fail --data-binary @input.jpg -H 'Content-Type: image/jpeg' \
  'http://127.0.0.1:8080/v1/depth?pose=true'

curl --fail --data-binary @input.jpg -H 'Content-Type: image/jpeg' \
  http://127.0.0.1:8080/v1/embed/image

curl --fail -H 'Content-Type: application/json' \
  -d '{"text":"a studio photograph of a cat"}' \
  http://127.0.0.1:8080/v1/embed/text

curl --fail -F image=@input.jpg -F 'labels=["cat","dog","car"]' \
  http://127.0.0.1:8080/v1/classify
```

OCR accepts `psm=auto|column|block|line|word|sparse|raw-line`, `dpi`, and
`preserve_spaces`. Depth returns base64 PNG visualizations and optional pose
matrices by default; set `response=image` for the depth PNG directly and
`invert=false` to reverse its visualization convention. Classification labels
may be a JSON array or a comma-separated field.

## Segmentation and detection

Point prompts are JSON arrays containing `[x,y]` or `[x,y,positive]`. A box is
`[x0,y0,x1,y1]`:

```sh
curl --fail -F image=@input.jpg \
  -F 'points=[[640,420,true],[120,80,false]]' \
  -F 'box=[300,120,900,700]' -F multimask=true \
  http://127.0.0.1:8080/v1/segment
```

Every returned segment contains its box, scores, and a base64 PNG mask.

```sh
curl --fail -F image=@input.jpg -F prompt=cat \
  -F threshold=0.3 -F nms=0.1 \
  -F 'positive_boxes=[[100,80,420,500]]' \
  http://127.0.0.1:8080/v1/detect
```

`positive_boxes` and `negative_boxes` are optional JSON arrays of boxes.

## Cutout and grounded extraction

`/v1/cutout` uses the segmentation prompts above. `/v1/extract` uses the
detection prompt and thresholds. Both accept `crop=true|false`, `padding`,
`upscale`, and `response=image|json`:

```sh
curl --fail -F image=@input.jpg -F 'points=[[640,420,true]]' \
  -F padding=16 -F upscale=4 \
  http://127.0.0.1:8080/v1/cutout --output subject.png

curl --fail -F image=@input.jpg -F prompt=cat -F selection=all \
  -F response=json http://127.0.0.1:8080/v1/extract
```

`selection` is `best` or `all`. Direct image responses carry workflow metadata
in `X-Imagecpp-*` headers; JSON responses include the final mask and source box.

## Generate, edit, and upscale

Generation accepts a JSON object. `prompt` is required:

```sh
curl --fail -H 'Content-Type: application/json' \
  -d '{"prompt":"a red fox in snow","width":768,"height":768,
       "steps":20,"guidance":7,"seed":42,"response":"image"}' \
  http://127.0.0.1:8080/v1/generate --output fox.png
```

Supported fields are `prompt`, `negative_prompt`, `width`, `height`, `steps`,
`guidance`, `seed`, `batch_count` (at most 8), `strength`, `sampler`,
`scheduler`, and `response`. Samplers are `auto`, `euler`, `euler-a`,
`dpm++2m`, `lcm`, or `ddim`. Schedulers are `auto`, `discrete`, `karras`,
`exponential`, `ays`, `sgm-uniform`, or `simple`.

Editing requires multipart input and optionally accepts a `mask` file:

```sh
curl --fail -F image=@input.png -F mask=@mask.png \
  -F 'prompt=replace the background with a forest' \
  -F strength=0.8 -F steps=20 -F response=image \
  http://127.0.0.1:8080/v1/edit --output edited.png
```

Generation and editing return a JSON `images` array unless `response=image`
is requested for a single result.

```sh
curl --fail --data-binary @input.png -H 'Content-Type: image/png' \
  'http://127.0.0.1:8080/v1/upscale?factor=4' --output upscaled.png
```

## Caption, VQA, and SSE

```sh
curl --fail -F image=@input.jpg -F temperature=0 \
  http://127.0.0.1:8080/v1/caption

curl --fail -F image=@input.jpg \
  -F 'question=What animal is shown? Answer with one word.' \
  http://127.0.0.1:8080/v1/ask
```

Both accept `max_tokens`, `temperature`, `top_p`, `top_k`, and `seed`.
Caption additionally accepts `prompt`; VQA requires `question`.

Send `Accept: text/event-stream` or set `stream=true` for incremental UTF-8
JSON events:

```text
event: delta
data: {"delta":"A cat"}

event: done
data: {"text":"A cat is sleeping.","prompt_tokens":156,"generated_tokens":5,"finish_reason":"end_of_generation"}
```

Disconnecting the client cancels the remaining generation. A failure after
streaming begins is delivered as an `error` event.

## Errors and deployment boundary

Non-streaming failures use a stable envelope:

```json
{"error":{"code":"invalid_image","message":"Image not of any known type, or corrupt"}}
```

Malformed inputs return `400`, unsupported operations or encodings `415`,
unavailable resources `503`, and model request failures `422`. Upload and
output limits are enforced before expensive work where dimensions are known.

The server provides plain HTTP without authentication or a browser CORS policy.
Its safe default is loopback. Do not bind it to a public or untrusted network;
put authentication, TLS, request accounting, and wider network policy in a
trusted reverse proxy. Treat generated text and images as untrusted output.
