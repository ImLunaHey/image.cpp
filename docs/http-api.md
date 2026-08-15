# HTTP API

`imagecpp serve` runs the native HTTP API inside the normal `imagecpp` process.
The server calls the public C++ library surface; it does not invoke another
binary, Python, a shell, or the llama.cpp server.

## Start the server

Load the validated language and vision-projection GGUFs once at startup:

```sh
./build/imagecpp serve \
  --vlm-model models/SmolVLM-256M-Instruct-Q8_0.gguf \
  --vlm-projection models/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf \
  --host 127.0.0.1 --port 8080 --gpu
```

The relevant options are:

| Option | Default | Meaning |
| --- | --- | --- |
| `--host` | `127.0.0.1` | Bind address |
| `--port` | `8080` | HTTP port |
| `--max-upload-mb` | `32` | Maximum complete request size |
| `--vlm-model` | unset | Language-model GGUF |
| `--vlm-projection` | unset | Matching vision-projection GGUF |
| `--context` | `4096` | Prompt plus generation context |
| `--threads` | automatic | CPU worker threads |
| `--cpu`, `--gpu` | automatic | VLM compute device |

Both VLM paths are required together. Starting without them leaves health and
operation introspection available while caption and VQA return `503`.

## Introspection

`GET /healthz` returns process health and model readiness:

```json
{"status":"ok","version":"0.1.0-dev","vlm_loaded":true}
```

`GET /v1/operations` returns the typed operations compiled into this build.
`GET /` returns basic service metadata and endpoint paths.

## Caption an image

Send the encoded image as the request body. PNG, JPEG, WebP, BMP, and TGA use
the same native decoder as the library:

```sh
curl --fail --data-binary @input.jpg \
  -H 'Content-Type: image/jpeg' \
  'http://127.0.0.1:8080/v1/caption?temperature=0&max_tokens=128'
```

The response is:

```json
{
  "text": "A cat sitting beside a window.",
  "prompt_tokens": 152,
  "generated_tokens": 8,
  "finish_reason": "end_of_generation"
}
```

Multipart is also accepted. The file field must be named `image`:

```sh
curl --fail -F image=@input.jpg \
  -F 'prompt=Describe the lighting and main subject.' \
  -F temperature=0 \
  http://127.0.0.1:8080/v1/caption
```

## Ask a visual question

`POST /v1/ask` uses the same image forms and requires `question` as a query or
multipart field:

```sh
curl --fail -F image=@input.jpg \
  -F 'question=What animal is shown? Answer with one word.' \
  -F temperature=0 \
  http://127.0.0.1:8080/v1/ask
```

Both routes accept `max_tokens`, `temperature`, `top_p`, `top_k`, and `seed`.
Multipart fields take precedence over query parameters.

## Stream with SSE

Send `Accept: text/event-stream` or set `stream=true`. Each UTF-8 delta is a
complete JSON SSE event:

```sh
curl --no-buffer -H 'Accept: text/event-stream' \
  -F image=@input.jpg \
  -F 'question=What is happening?' \
  http://127.0.0.1:8080/v1/ask
```

```text
event: delta
data: {"delta":"A cat"}

event: delta
data: {"delta":" is sleeping."}

event: done
data: {"text":"A cat is sleeping.","prompt_tokens":156,"generated_tokens":5,"finish_reason":"end_of_generation"}
```

If the client disconnects, transport backpressure stops the library callback
and cancels the remaining generation. An inference failure after streaming has
started is delivered as an `error` event.

## Errors and deployment boundary

Non-streaming failures use a stable envelope:

```json
{"error":{"code":"invalid_image","message":"Image not of any known type, or corrupt"}}
```

Malformed inputs return `400`, unsupported inputs `415`, unloaded models `503`,
and model request failures `422`. The payload limit is enforced before model
inference.

The server currently provides plain HTTP without authentication or browser
CORS policy. Its safe default is loopback. Do not bind it to a public or
untrusted network; put authentication, TLS, request accounting, and any wider
network policy in a trusted reverse proxy. Model outputs remain untrusted text.
