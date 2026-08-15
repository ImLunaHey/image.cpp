#include "server/playground.hpp"

namespace imagecpp::server {
namespace {

constexpr char kHtml[] = R"IMAGECPP_HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="color-scheme" content="dark">
  <meta name="theme-color" content="#0b0d0c">
  <title>image.cpp studio</title>
  <link rel="stylesheet" href="/assets/playground.css">
</head>
<body>
  <header class="topbar">
    <a class="brand" href="/playground" aria-label="image.cpp studio home">
      <span class="mark" aria-hidden="true"><i></i><i></i><i></i><i></i></span>
      <span>image.cpp</span><em>studio</em>
    </a>
    <div class="top-actions">
      <button class="jobs-button" id="jobs-button" type="button"><span>Jobs</span><b id="job-count">0</b></button>
      <div class="server-state" id="server-state" role="status">
        <span class="state-dot"></span><span id="server-label">Connecting</span>
      </div>
    </div>
  </header>

  <div class="drawer-shell" id="drawer-shell" hidden>
    <button class="drawer-backdrop" id="drawer-backdrop" type="button" aria-label="Close jobs"></button>
    <aside class="job-drawer" aria-label="Background jobs">
      <div class="drawer-head"><div><span>Runtime</span><h2>Jobs & models</h2></div><button class="icon-button" id="close-jobs" type="button" aria-label="Close jobs">×</button></div>
      <div class="runtime-card">
        <div><span>Model cache</span><strong id="cache-summary">Connecting</strong></div>
        <button class="quiet-button" id="clear-cache" type="button">Release</button>
      </div>
      <div class="job-list-head"><span>Recent jobs</span><button class="quiet-button" id="refresh-jobs" type="button">Refresh</button></div>
      <div class="job-list" id="job-list"><p>No background jobs yet.</p></div>
    </aside>
  </div>

  <main class="shell">
    <aside class="toolbox" aria-label="Image operations">
      <div class="toolbox-heading">
        <span>Operations</span>
        <button class="icon-button" id="refresh-health" title="Refresh model status" aria-label="Refresh model status">↻</button>
      </div>
      <nav id="operation-nav"></nav>
      <a class="api-link" href="/v1/operations" target="_blank" rel="noreferrer">
        <span>API manifest</span><span aria-hidden="true">↗</span>
      </a>
    </aside>

    <section class="workspace">
      <div class="workspace-head">
        <div>
          <p class="eyebrow" id="operation-group">Transform</p>
          <h1 id="operation-title">Resize</h1>
          <p id="operation-description">Resize and re-encode an image locally.</p>
        </div>
        <span class="model-badge" id="model-badge">No model needed</span>
      </div>

      <div class="stage" id="stage">
        <input id="image-input" type="file" accept="image/png,image/jpeg,image/webp,image/bmp,image/x-tga,.tga" hidden>
        <div class="empty-stage" id="empty-stage">
          <div class="drop-art" aria-hidden="true"><span></span></div>
          <h2>Drop an image here</h2>
          <p>or paste from your clipboard</p>
          <button class="secondary-button" id="choose-image" type="button">Choose image</button>
          <small>PNG, JPEG, WebP, BMP or TGA · processed on this machine</small>
        </div>
        <div class="preview-stage" id="preview-stage" hidden>
          <div class="stage-toolbar">
            <div class="view-toggle" role="group" aria-label="Preview selection">
              <button class="active" data-view="input" type="button">Input</button>
              <button data-view="output" type="button">Output</button>
            </div>
            <div class="file-meta"><span id="file-name"></span><span id="image-size"></span></div>
            <button class="quiet-button" id="replace-image" type="button">Replace</button>
          </div>
          <div class="image-canvas" id="image-canvas">
            <img id="preview-image" alt="Selected input">
            <div class="point-layer" id="point-layer" aria-hidden="true"></div>
            <div class="canvas-hint" id="canvas-hint" hidden>Click = include · Shift-click = exclude</div>
          </div>
        </div>
        <div class="generation-stage" id="generation-stage" hidden>
          <div class="orb" aria-hidden="true"><span></span></div>
          <h2>Start from words</h2>
          <p>Your prompt becomes pixels, entirely through the local model.</p>
        </div>
        <div class="working-overlay" id="working-overlay" hidden>
          <div class="working-mark"><i></i><i></i><i></i></div>
          <strong id="working-title">Processing</strong>
          <span id="working-time">0.0s</span>
        </div>
      </div>

      <section class="results" id="results" hidden aria-live="polite">
        <div class="result-head">
          <div><span class="result-kicker">Result</span><strong id="result-summary"></strong></div>
          <div class="result-actions">
            <button class="quiet-button" id="copy-result" type="button">Copy JSON</button>
            <button class="secondary-button" id="download-result" type="button">Download</button>
          </div>
        </div>
        <div class="result-gallery" id="result-gallery"></div>
        <div class="text-result" id="text-result" hidden></div>
        <details class="json-result" id="json-details">
          <summary>Structured response</summary>
          <pre id="json-result"></pre>
        </details>
      </section>
    </section>

    <aside class="controls">
      <form id="operation-form">
        <div class="controls-head">
          <div><span>Parameters</span><small id="endpoint-label">POST /v1/resize</small></div>
          <button class="quiet-button" id="reset-form" type="button">Reset</button>
        </div>
        <div class="preset-bar">
          <select id="preset-select" aria-label="Saved presets"><option value="">Saved presets</option></select>
          <button class="quiet-button" id="save-preset" type="button">Save</button>
          <button class="quiet-button" id="delete-preset" type="button" disabled>Delete</button>
        </div>
        <div id="dynamic-fields"></div>
        <div class="mask-field" id="mask-field" hidden>
          <label>Optional mask</label>
          <input id="mask-input" type="file" accept="image/png,image/jpeg,image/webp,image/bmp,image/x-tga,.tga">
        </div>
        <div class="form-message" id="form-message" hidden></div>
        <div class="job-mode">
          <div><label for="background-run">Run in background</label><small>Queue it and keep using the studio</small></div>
          <input id="background-run" type="checkbox">
        </div>
        <button class="run-button" id="run-button" type="submit">
          <span id="run-label">Run resize</span><span aria-hidden="true">→</span>
        </button>
        <p class="shortcut">⌘ Enter to run · Esc to clear result</p>
      </form>
    </aside>
  </main>

  <footer>
    <span>Native inference. One process. No cloud calls.</span>
    <span id="version-label">image.cpp</span>
  </footer>
  <script src="/assets/playground.js" defer></script>
</body>
</html>)IMAGECPP_HTML";

constexpr char kCss[] = R"IMAGECPP_CSS(:root {
  --ink: #f1f4ed;
  --muted: #8d968d;
  --dim: #626a64;
  --ground: #0b0d0c;
  --panel: #111412;
  --panel-2: #161a17;
  --line: #292e2a;
  --line-soft: #1e231f;
  --acid: #c9ff43;
  --acid-ink: #172000;
  --blue: #73a7ff;
  --danger: #ff766d;
  --radius: 14px;
  --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  --sans: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

* { box-sizing: border-box; }
html { background: var(--ground); }
body {
  margin: 0;
  min-width: 320px;
  min-height: 100vh;
  color: var(--ink);
  background:
    radial-gradient(circle at 58% -20%, rgba(121, 151, 99, .11), transparent 40rem),
    var(--ground);
  font-family: var(--sans);
  -webkit-font-smoothing: antialiased;
}
button, input, textarea, select { font: inherit; }
button, input, textarea, select, a { -webkit-tap-highlight-color: transparent; }
button { color: inherit; }
[hidden] { display: none !important; }

.topbar {
  height: 66px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  border-bottom: 1px solid var(--line);
  background: rgba(11, 13, 12, .86);
  backdrop-filter: blur(16px);
  position: sticky;
  top: 0;
  z-index: 10;
}
.brand { display: flex; align-items: center; gap: 9px; color: var(--ink); text-decoration: none; font-weight: 720; letter-spacing: -.035em; }
.brand em { color: var(--muted); font-size: 12px; font-style: normal; font-weight: 560; letter-spacing: .02em; border: 1px solid var(--line); padding: 3px 7px; border-radius: 999px; }
.mark { width: 25px; height: 25px; display: grid; grid-template-columns: 1fr 1fr; gap: 2px; transform: rotate(3deg); }
.mark i { display: block; background: var(--acid); border-radius: 2px; }
.mark i:nth-child(2), .mark i:nth-child(3) { opacity: .48; }
.server-state { display: flex; align-items: center; gap: 8px; color: var(--muted); font: 11px var(--mono); text-transform: uppercase; letter-spacing: .07em; }
.state-dot { width: 7px; height: 7px; border-radius: 50%; background: #d6a63f; box-shadow: 0 0 0 3px rgba(214,166,63,.12); }
.server-state.ready .state-dot { background: var(--acid); box-shadow: 0 0 0 3px rgba(201,255,67,.12); }
.server-state.error .state-dot { background: var(--danger); box-shadow: 0 0 0 3px rgba(255,118,109,.12); }
.top-actions { display: flex; align-items: center; gap: 14px; }
.jobs-button { display: flex; align-items: center; gap: 7px; border: 1px solid var(--line); border-radius: 8px; padding: 6px 8px; color: var(--muted); background: var(--panel); cursor: pointer; font-size: 11px; }
.jobs-button:hover { color: var(--ink); border-color: #434a44; }
.jobs-button b { min-width: 18px; padding: 2px 5px; border-radius: 99px; color: var(--acid-ink); background: var(--acid); font: 9px var(--mono); text-align: center; }

.drawer-shell { position: fixed; inset: 0; z-index: 50; }
.drawer-backdrop { position: absolute; inset: 0; width: 100%; border: 0; background: rgba(2, 3, 2, .62); backdrop-filter: blur(3px); cursor: default; }
.job-drawer { position: absolute; top: 0; right: 0; width: min(430px, 94vw); height: 100%; overflow: auto; padding: 24px; border-left: 1px solid var(--line); background: #0e110f; box-shadow: -24px 0 70px rgba(0,0,0,.42); }
.drawer-head { display: flex; align-items: flex-start; justify-content: space-between; padding-bottom: 20px; border-bottom: 1px solid var(--line); }
.drawer-head span, .job-list-head span { color: var(--acid); font: 9px var(--mono); letter-spacing: .1em; text-transform: uppercase; }
.drawer-head h2 { margin: 5px 0 0; font-size: 22px; letter-spacing: -.035em; }
.runtime-card { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin: 18px 0 25px; padding: 14px; border: 1px solid var(--line); border-radius: 10px; background: var(--panel); }
.runtime-card div { display: flex; flex-direction: column; gap: 5px; }
.runtime-card span { color: var(--dim); font: 9px var(--mono); text-transform: uppercase; letter-spacing: .08em; }
.runtime-card strong { color: #c8cec8; font-size: 11px; font-weight: 550; }
.job-list-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px; }
.job-list { display: grid; gap: 9px; }
.job-list > p { margin: 28px 0; color: var(--dim); text-align: center; font-size: 11px; }
.job-item { padding: 13px; border: 1px solid var(--line-soft); border-radius: 10px; background: var(--panel); }
.job-item-top, .job-item-bottom { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.job-item-title { min-width: 0; }
.job-item-title strong { display: block; overflow: hidden; text-overflow: ellipsis; color: #d2d8d1; font-size: 12px; font-weight: 620; text-transform: capitalize; }
.job-item-title span, .job-time { color: var(--dim); font: 9px var(--mono); }
.job-status { flex: none; padding: 4px 7px; border-radius: 99px; color: var(--muted); background: #242925; font: 8px var(--mono); text-transform: uppercase; letter-spacing: .05em; }
.job-status.completed { color: var(--acid-ink); background: var(--acid); }
.job-status.failed, .job-status.cancelled { color: #ffb0aa; background: rgba(255,118,109,.12); }
.job-progress { height: 3px; margin: 12px 0 9px; overflow: hidden; border-radius: 99px; background: #292e2a; }
.job-progress i { display: block; height: 100%; border-radius: inherit; background: var(--acid); transition: width .25s ease; }
.job-item-bottom span { overflow: hidden; color: var(--dim); font: 9px var(--mono); text-overflow: ellipsis; white-space: nowrap; }
.job-actions { display: flex; flex: none; gap: 4px; }
.job-actions button { padding: 4px 6px; }

.shell { min-height: calc(100vh - 110px); display: grid; grid-template-columns: 224px minmax(420px, 1fr) 326px; }
.toolbox, .controls { background: rgba(17, 20, 18, .74); }
.toolbox { border-right: 1px solid var(--line); padding: 22px 14px; }
.controls { border-left: 1px solid var(--line); padding: 22px 18px; }
.toolbox-heading, .controls-head { display: flex; align-items: center; justify-content: space-between; min-height: 31px; padding: 0 8px; color: var(--muted); font-size: 11px; font-weight: 650; letter-spacing: .09em; text-transform: uppercase; }
.icon-button, .quiet-button { border: 0; background: transparent; cursor: pointer; }
.icon-button { width: 28px; height: 28px; border-radius: 8px; color: var(--muted); font-size: 18px; }
.icon-button:hover, .quiet-button:hover { color: var(--ink); background: var(--line-soft); }
.tool-group { margin-top: 20px; }
.tool-group-label { display: block; padding: 0 10px 7px; color: var(--dim); font: 10px var(--mono); letter-spacing: .09em; text-transform: uppercase; }
.tool-button {
  width: 100%; display: flex; align-items: center; gap: 10px; border: 0; border-radius: 9px; padding: 9px 10px;
  background: transparent; color: #aab1aa; cursor: pointer; text-align: left; font-size: 13px; transition: .16s ease;
}
.tool-button:hover { background: var(--line-soft); color: var(--ink); }
.tool-button.active { color: var(--ink); background: #222821; box-shadow: inset 2px 0 var(--acid); }
.tool-button.unavailable { opacity: .43; }
.tool-icon { width: 22px; color: var(--dim); font: 13px var(--mono); text-align: center; }
.tool-button.active .tool-icon { color: var(--acid); }
.availability { margin-left: auto; width: 5px; height: 5px; background: var(--acid); border-radius: 50%; }
.tool-button.unavailable .availability { background: var(--dim); }
.api-link { display: flex; justify-content: space-between; margin: 26px 9px 0; padding-top: 17px; border-top: 1px solid var(--line-soft); color: var(--dim); text-decoration: none; font: 11px var(--mono); }
.api-link:hover { color: var(--acid); }

.workspace { min-width: 0; padding: 34px clamp(22px, 4vw, 58px) 52px; }
.workspace-head { display: flex; align-items: flex-start; justify-content: space-between; gap: 24px; margin-bottom: 25px; }
.eyebrow { margin: 0 0 7px; color: var(--acid); font: 10px var(--mono); letter-spacing: .12em; text-transform: uppercase; }
h1 { margin: 0; font-size: clamp(27px, 3vw, 38px); line-height: 1; letter-spacing: -.045em; font-weight: 640; }
.workspace-head p:last-child { max-width: 590px; margin: 11px 0 0; color: var(--muted); font-size: 13px; line-height: 1.55; }
.model-badge { flex: none; border: 1px solid var(--line); border-radius: 999px; padding: 7px 10px; color: var(--muted); font: 10px var(--mono); }
.model-badge.unavailable { color: var(--danger); border-color: rgba(255,118,109,.25); }

.stage {
  position: relative; min-height: min(57vh, 610px); border: 1px solid var(--line); border-radius: var(--radius); overflow: hidden;
  background:
    linear-gradient(90deg, rgba(255,255,255,.018) 1px, transparent 1px),
    linear-gradient(rgba(255,255,255,.018) 1px, transparent 1px), #0e110f;
  background-size: 28px 28px;
}
.stage.dragging { border-color: var(--acid); box-shadow: 0 0 0 3px rgba(201,255,67,.08); }
.empty-stage, .generation-stage { min-height: inherit; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; padding: 38px; }
.empty-stage h2, .generation-stage h2 { margin: 20px 0 5px; font-size: 18px; letter-spacing: -.025em; }
.empty-stage p, .generation-stage p { margin: 0 0 20px; color: var(--muted); font-size: 13px; }
.empty-stage small { margin-top: 18px; color: var(--dim); font: 10px var(--mono); }
.drop-art { width: 76px; height: 76px; border: 1px dashed #454d45; border-radius: 17px; display: grid; place-items: center; transform: rotate(-3deg); }
.drop-art span { position: relative; width: 27px; height: 27px; border: 1px solid var(--muted); border-radius: 4px; }
.drop-art span::before { content: ''; position: absolute; width: 6px; height: 6px; border-radius: 50%; background: var(--acid); top: 5px; right: 5px; }
.drop-art span::after { content: ''; position: absolute; left: 4px; bottom: 4px; width: 17px; height: 11px; background: linear-gradient(145deg, transparent 45%, var(--muted) 46% 55%, transparent 56%), linear-gradient(35deg, transparent 43%, var(--muted) 44% 54%, transparent 55%); }
.secondary-button { border: 1px solid #3a413b; background: #1a1f1b; border-radius: 8px; padding: 9px 13px; color: var(--ink); cursor: pointer; font-size: 12px; }
.secondary-button:hover { border-color: #596259; background: #202520; }
.preview-stage { min-height: inherit; display: flex; flex-direction: column; }
.stage-toolbar { min-height: 48px; display: flex; align-items: center; gap: 15px; padding: 8px 12px; border-bottom: 1px solid var(--line-soft); background: rgba(17,20,18,.9); }
.view-toggle { display: flex; padding: 2px; border: 1px solid var(--line); border-radius: 7px; }
.view-toggle button { border: 0; border-radius: 5px; padding: 5px 9px; color: var(--dim); background: transparent; cursor: pointer; font-size: 10px; }
.view-toggle button.active { color: var(--ink); background: #2b312c; }
.file-meta { min-width: 0; display: flex; align-items: center; gap: 9px; color: var(--muted); font: 10px var(--mono); }
.file-meta span:first-child { max-width: 180px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: #b4bcb4; }
.quiet-button { border-radius: 7px; padding: 6px 8px; color: var(--dim); font-size: 11px; }
.stage-toolbar .quiet-button { margin-left: auto; }
.image-canvas { position: relative; min-height: 360px; flex: 1; display: grid; place-items: center; overflow: hidden; padding: 18px; }
.image-canvas img { display: block; max-width: 100%; max-height: min(51vh, 550px); object-fit: contain; box-shadow: 0 18px 55px rgba(0,0,0,.38); user-select: none; }
.point-layer { position: absolute; pointer-events: none; }
.prompt-point { position: absolute; width: 14px; height: 14px; transform: translate(-50%, -50%); border: 2px solid #fff; border-radius: 50%; background: var(--acid); box-shadow: 0 1px 8px #000; }
.prompt-point.negative { background: var(--danger); }
.canvas-hint { position: absolute; bottom: 13px; left: 50%; transform: translateX(-50%); padding: 7px 10px; border: 1px solid var(--line); border-radius: 99px; color: #c0c7c0; background: rgba(11,13,12,.84); backdrop-filter: blur(8px); font: 10px var(--mono); }
.orb { width: 112px; height: 112px; display: grid; place-items: center; border-radius: 50%; background: radial-gradient(circle at 35% 30%, #e9ff9c, var(--acid) 20%, #587519 68%, transparent 70%); filter: drop-shadow(0 0 30px rgba(201,255,67,.16)); }
.orb span { width: 54px; height: 54px; border: 1px solid rgba(12,17,0,.65); border-radius: 35% 65% 55% 45%; animation: morph 7s ease-in-out infinite alternate; }
@keyframes morph { to { transform: rotate(160deg) scale(.74); border-radius: 65% 35% 40% 60%; } }
.working-overlay { position: absolute; inset: 0; z-index: 5; display: flex; flex-direction: column; align-items: center; justify-content: center; background: rgba(8,10,9,.86); backdrop-filter: blur(9px); }
.working-overlay strong { margin-top: 17px; font-size: 14px; }
.working-overlay span { margin-top: 5px; color: var(--muted); font: 11px var(--mono); }
.working-mark { height: 28px; display: flex; align-items: end; gap: 4px; }
.working-mark i { display: block; width: 5px; height: 9px; border-radius: 3px; background: var(--acid); animation: bars .8s ease-in-out infinite alternate; }
.working-mark i:nth-child(2) { height: 24px; animation-delay: -.25s; }
.working-mark i:nth-child(3) { height: 15px; animation-delay: -.5s; }
@keyframes bars { to { height: 5px; opacity: .45; } }

.results { margin-top: 17px; border: 1px solid var(--line); border-radius: var(--radius); background: var(--panel); overflow: hidden; }
.result-head { min-height: 58px; display: flex; align-items: center; justify-content: space-between; gap: 15px; padding: 10px 14px 10px 18px; border-bottom: 1px solid var(--line-soft); }
.result-head > div:first-child { display: flex; align-items: baseline; gap: 11px; min-width: 0; }
.result-kicker { color: var(--acid); font: 9px var(--mono); letter-spacing: .1em; text-transform: uppercase; }
.result-head strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 12px; font-weight: 550; color: #c8cec8; }
.result-actions { display: flex; gap: 7px; }
.result-gallery { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1px; background: var(--line-soft); }
.result-card { min-height: 190px; display: grid; place-items: center; position: relative; background: #0e110f; padding: 15px; }
.result-card img { display: block; max-width: 100%; max-height: 430px; object-fit: contain; }
.result-card a { position: absolute; right: 10px; bottom: 10px; padding: 6px 8px; border-radius: 6px; color: var(--ink); background: rgba(11,13,12,.82); text-decoration: none; font: 10px var(--mono); opacity: 0; transition: opacity .15s; }
.result-card:hover a { opacity: 1; }
.text-result { padding: 22px; color: #dce1db; white-space: pre-wrap; line-height: 1.65; font-size: 14px; }
.json-result { border-top: 1px solid var(--line-soft); }
.json-result summary { padding: 12px 18px; color: var(--dim); cursor: pointer; font: 10px var(--mono); }
.json-result pre { max-height: 420px; overflow: auto; margin: 0; padding: 18px; border-top: 1px solid var(--line-soft); color: #aeb8af; background: #0d0f0e; font: 11px/1.6 var(--mono); white-space: pre-wrap; word-break: break-word; }

.controls form { position: sticky; top: 88px; }
.controls-head { padding: 0 0 15px; border-bottom: 1px solid var(--line); }
.controls-head > div { display: flex; flex-direction: column; gap: 5px; }
.controls-head small { color: var(--dim); font: 9px var(--mono); letter-spacing: 0; text-transform: none; }
.preset-bar { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; gap: 5px; margin-top: 13px; }
.preset-bar select { min-width: 0; border: 1px solid var(--line); border-radius: 7px; outline: 0; padding: 6px 8px; color: var(--muted); background: #0c0f0d; font-size: 10px; }
.preset-bar button:disabled { opacity: .35; cursor: not-allowed; }
.field { margin-top: 17px; }
.field-row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.field label, .mask-field label { display: flex; justify-content: space-between; margin-bottom: 7px; color: #aeb5ae; font-size: 11px; font-weight: 560; }
.field label small { color: var(--dim); font: 9px var(--mono); }
.field input:not([type=checkbox]), .field textarea, .field select, .mask-field input {
  width: 100%; border: 1px solid var(--line); border-radius: 8px; outline: 0; color: var(--ink); background: #0c0f0d; padding: 9px 10px; font-size: 12px; transition: border .15s, box-shadow .15s;
}
.field textarea { min-height: 78px; resize: vertical; line-height: 1.45; }
.field input:focus, .field textarea:focus, .field select:focus { border-color: #637051; box-shadow: 0 0 0 3px rgba(201,255,67,.06); }
.field input::placeholder, .field textarea::placeholder { color: #4e554f; }
.field select { appearance: none; background-image: linear-gradient(45deg, transparent 50%, var(--muted) 50%), linear-gradient(135deg, var(--muted) 50%, transparent 50%); background-position: calc(100% - 15px) 14px, calc(100% - 11px) 14px; background-size: 4px 4px; background-repeat: no-repeat; }
.check-field { display: flex; align-items: center; justify-content: space-between; border: 1px solid var(--line-soft); border-radius: 8px; padding: 9px 10px; }
.check-field label { margin: 0; }
.check-field input { appearance: none; width: 28px; height: 16px; padding: 2px; border-radius: 99px; background: #343a35; cursor: pointer; transition: .15s; }
.check-field input::after { content: ''; display: block; width: 12px; height: 12px; border-radius: 50%; background: #939b94; transition: .15s; }
.check-field input:checked { background: var(--acid); }
.check-field input:checked::after { background: var(--acid-ink); transform: translateX(12px); }
.mask-field { margin-top: 17px; }
.mask-field input { color: var(--muted); font: 10px var(--mono); }
.form-message { margin-top: 17px; padding: 10px; border: 1px solid rgba(255,118,109,.22); border-radius: 8px; color: #ffaaa4; background: rgba(255,118,109,.05); font-size: 11px; line-height: 1.45; }
.job-mode { display: flex; align-items: center; justify-content: space-between; gap: 14px; margin-top: 19px; padding: 10px; border: 1px solid var(--line-soft); border-radius: 8px; }
.job-mode div { display: flex; flex-direction: column; gap: 3px; }
.job-mode label { color: #aeb5ae; font-size: 11px; font-weight: 560; }
.job-mode small { color: var(--dim); font: 9px var(--mono); }
.job-mode input { flex: none; appearance: none; width: 32px; height: 18px; padding: 2px; border-radius: 99px; background: #343a35; cursor: pointer; transition: .15s; }
.job-mode input::after { content: ''; display: block; width: 14px; height: 14px; border-radius: 50%; background: #939b94; transition: .15s; }
.job-mode input:checked { background: var(--acid); }
.job-mode input:checked::after { background: var(--acid-ink); transform: translateX(14px); }
.run-button { width: 100%; display: flex; justify-content: space-between; align-items: center; margin-top: 22px; border: 0; border-radius: 9px; padding: 12px 14px; color: var(--acid-ink); background: var(--acid); cursor: pointer; font-size: 12px; font-weight: 720; transition: transform .12s, filter .12s; }
.run-button:hover { filter: brightness(1.06); transform: translateY(-1px); }
.run-button:active { transform: translateY(0); }
.run-button:disabled { cursor: not-allowed; filter: grayscale(.7); opacity: .42; transform: none; }
.shortcut { margin: 10px 0 0; color: var(--dim); text-align: center; font: 9px var(--mono); }

footer { min-height: 44px; display: flex; justify-content: space-between; align-items: center; padding: 0 24px; border-top: 1px solid var(--line); color: var(--dim); font: 9px var(--mono); letter-spacing: .04em; }

@media (max-width: 1100px) {
  .shell { grid-template-columns: 184px minmax(390px, 1fr) 288px; }
  .workspace { padding-left: 25px; padding-right: 25px; }
}
@media (max-width: 860px) {
  .shell { display: block; }
  .toolbox { border-right: 0; border-bottom: 1px solid var(--line); padding: 13px 15px; overflow-x: auto; }
  .toolbox-heading, .api-link, .tool-group-label { display: none; }
  #operation-nav { display: flex; gap: 6px; }
  .tool-group { display: contents; }
  .tool-button { flex: none; width: auto; border: 1px solid var(--line-soft); }
  .availability { display: none; }
  .controls { border-left: 0; border-top: 1px solid var(--line); padding: 24px max(22px, 7vw) 38px; }
  .controls form { position: static; }
  .workspace { padding: 29px max(18px, 4vw) 36px; }
}
@media (max-width: 520px) {
  .topbar { height: 58px; padding: 0 15px; }
  .brand em { display: none; }
  .server-state { font-size: 9px; }
  .jobs-button span { display: none; }
  .workspace-head { display: block; }
  .model-badge { display: inline-block; margin-top: 13px; }
  .stage { min-height: 430px; }
  .empty-stage, .generation-stage { padding: 25px; }
  .stage-toolbar { flex-wrap: wrap; }
  .file-meta { order: 3; width: 100%; }
  .result-head { align-items: flex-start; }
  .result-head > div:first-child { display: block; }
  .result-head strong { display: block; margin-top: 4px; max-width: 165px; }
  .result-actions { flex-direction: column; }
  footer span:first-child { display: none; }
  footer { justify-content: center; }
}
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { scroll-behavior: auto !important; animation-duration: .01ms !important; animation-iteration-count: 1 !important; }
})IMAGECPP_CSS";

constexpr char kJavascript[] = R"IMAGECPP_JS((() => {
  'use strict';

  const field = (name, label, type = 'text', value = '', extra = {}) => ({ name, label, type, value, ...extra });
  const operations = [
    { id: 'resize', group: 'Transform', icon: '↔', title: 'Resize', endpoint: '/v1/resize', description: 'Resize and re-encode an image locally.', model: null, output: 'image', fields: [
      field('width', 'Width', 'number', 1024, { min: 1 }), field('height', 'Height', 'number', 1024, { min: 1 }),
      field('filter', 'Filter', 'select', 'bilinear', { options: ['bilinear', 'nearest'] }), field('format', 'Format', 'select', 'png', { options: ['png', 'jpeg', 'webp', 'bmp', 'tga'] }),
      field('quality', 'Quality', 'number', 90, { min: 1, max: 100 }), field('lossless', 'Lossless encoding', 'checkbox', false)
    ]},
    { id: 'upscale', group: 'Transform', icon: '↗', title: 'Upscale', endpoint: '/v1/upscale', description: 'Increase resolution with a native ESRGAN model.', model: 'upscaler', output: 'image', fields: [field('factor', 'Scale factor', 'select', '4', { options: ['2', '3', '4'] })] },
    { id: 'caption', group: 'Understand', icon: '¶', title: 'Caption', endpoint: '/v1/caption', description: 'Describe the content of an image with a vision-language model.', model: 'vlm', output: 'text', fields: [
      field('prompt', 'Guidance', 'textarea', '', { placeholder: 'Optional captioning instructions' }), field('max_tokens', 'Max tokens', 'number', 128, { min: 1 }), field('temperature', 'Temperature', 'number', 0, { min: 0, step: .1 }), field('top_p', 'Top P', 'number', .95, { min: 0, max: 1, step: .05 }), field('top_k', 'Top K', 'number', 40, { min: 0 }), field('seed', 'Seed', 'number', 0, { min: 0 }), field('stream', 'Stream response', 'checkbox', true)
    ]},
    { id: 'ask', group: 'Understand', icon: '?', title: 'Ask image', endpoint: '/v1/ask', description: 'Ask a natural-language question about the selected image.', model: 'vlm', output: 'text', fields: [
      field('question', 'Question', 'textarea', 'What is happening in this image?', { required: true }), field('max_tokens', 'Max tokens', 'number', 128, { min: 1 }), field('temperature', 'Temperature', 'number', 0, { min: 0, step: .1 }), field('top_p', 'Top P', 'number', .95, { min: 0, max: 1, step: .05 }), field('top_k', 'Top K', 'number', 40, { min: 0 }), field('seed', 'Seed', 'number', 0, { min: 0 }), field('stream', 'Stream response', 'checkbox', true)
    ]},
    { id: 'ocr', group: 'Understand', icon: 'Aa', title: 'Read text', endpoint: '/v1/ocr', description: 'Extract text and document regions with native OCR.', model: 'ocr', output: 'text', fields: [
      field('psm', 'Page layout', 'select', 'auto', { options: ['auto', 'column', 'block', 'line', 'word', 'sparse', 'raw-line'] }), field('dpi', 'Source DPI', 'number', 300, { min: 1 }), field('preserve_spaces', 'Preserve spaces', 'checkbox', false)
    ]},
    { id: 'depth', group: 'Understand', icon: '◒', title: 'Depth map', endpoint: '/v1/depth', description: 'Estimate per-pixel scene depth and optional camera pose.', model: 'depth', output: 'json-images', fields: [field('pose', 'Include pose', 'checkbox', false), field('invert', 'Near is bright', 'checkbox', true)] },
    { id: 'classify', group: 'Understand', icon: '≡', title: 'Classify', endpoint: '/v1/classify', description: 'Rank your labels against the image with CLIP.', model: 'clip', output: 'json', fields: [field('labels', 'Candidate labels', 'textarea', 'cat, dog, person, landscape', { required: true, placeholder: 'Comma-separated labels' })] },
    { id: 'embed-image', group: 'Understand', icon: '∿', title: 'Image embedding', endpoint: '/v1/embed/image', description: 'Create a normalized CLIP vector for the image.', model: 'clip', output: 'json', fields: [] },
    { id: 'embed-text', group: 'Understand', icon: '∿', title: 'Text embedding', endpoint: '/v1/embed/text', description: 'Create a normalized CLIP vector for text.', model: 'clip', output: 'json', input: 'text', fields: [field('text', 'Text', 'textarea', 'a studio photograph of a cat', { required: true })] },
    { id: 'segment', group: 'Isolate', icon: '◎', title: 'Segment', endpoint: '/v1/segment', description: 'Click the image to prompt precise segmentation masks.', model: 'segment', output: 'json-images', points: true, fields: [
      field('points', 'Prompt points', 'textarea', '', { placeholder: 'Click image or enter [[x,y,true]]' }), field('box', 'Prompt box', 'text', '', { placeholder: '[x0,y0,x1,y1]' }), field('multimask', 'Multiple masks', 'checkbox', true)
    ]},
    { id: 'detect', group: 'Isolate', icon: '⌗', title: 'Detect', endpoint: '/v1/detect', description: 'Find objects from an open-vocabulary text prompt.', model: 'detect', output: 'json-images', fields: [
      field('prompt', 'Object prompt', 'text', 'person', { required: true }), field('threshold', 'Threshold', 'number', .3, { min: 0, max: 1, step: .05 }), field('nms', 'NMS', 'number', .1, { min: 0, max: 1, step: .05 }), field('positive_boxes', 'Positive boxes', 'textarea', '', { placeholder: 'Optional [[x0,y0,x1,y1]]' }), field('negative_boxes', 'Negative boxes', 'textarea', '', { placeholder: 'Optional [[x0,y0,x1,y1]]' })
    ]},
    { id: 'cutout', group: 'Isolate', icon: '✂', title: 'Cutout', endpoint: '/v1/cutout', description: 'Click a subject to create a transparent PNG cutout.', model: 'segment', output: 'image', points: true, fields: [
      field('points', 'Prompt points', 'textarea', '', { placeholder: 'Click image or enter [[x,y,true]]' }), field('box', 'Prompt box', 'text', '', { placeholder: '[x0,y0,x1,y1]' }), field('multimask', 'Multiple masks', 'checkbox', true), field('crop', 'Crop to subject', 'checkbox', true), field('padding', 'Padding', 'number', 16, { min: 0 }), field('upscale', 'Upscale', 'select', '1', { options: ['1', '2', '3', '4'] })
    ]},
    { id: 'remove-background', group: 'Isolate', icon: '◌', title: 'Remove background', endpoint: '/v1/remove-background', description: 'Create transparency around a click-prompted subject.', model: 'segment', output: 'image', points: true, fields: [
      field('points', 'Prompt points', 'textarea', '', { placeholder: 'Click image or enter [[x,y,true]]' }), field('box', 'Prompt box', 'text', '', { placeholder: '[x0,y0,x1,y1]' }), field('multimask', 'Multiple masks', 'checkbox', true), field('crop', 'Crop to subject', 'checkbox', false), field('padding', 'Padding', 'number', 0, { min: 0 })
    ]},
    { id: 'extract', group: 'Isolate', icon: '◇', title: 'Extract by text', endpoint: '/v1/extract', description: 'Detect a named subject and return a transparent extraction.', model: 'detect', output: 'image', fields: [
      field('prompt', 'Object prompt', 'text', 'person', { required: true }), field('threshold', 'Threshold', 'number', .3, { min: 0, max: 1, step: .05 }), field('nms', 'NMS', 'number', .1, { min: 0, max: 1, step: .05 }), field('selection', 'Selection', 'select', 'best', { options: ['best', 'all'] }), field('crop', 'Crop to subject', 'checkbox', true), field('padding', 'Padding', 'number', 16, { min: 0 }), field('upscale', 'Upscale', 'select', '1', { options: ['1', '2', '3', '4'] })
    ]},
    { id: 'generate', group: 'Create', icon: '✦', title: 'Generate', endpoint: '/v1/generate', description: 'Create images from a text prompt with local diffusion.', model: 'diffusion', output: 'image', input: 'none', fields: [
      field('prompt', 'Prompt', 'textarea', 'A quiet brutalist house in a mossy forest, soft morning light', { required: true }), field('negative_prompt', 'Negative prompt', 'textarea', '', { placeholder: 'Optional exclusions' }), field('width', 'Width', 'number', 512, { min: 64, step: 64 }), field('height', 'Height', 'number', 512, { min: 64, step: 64 }), field('steps', 'Steps', 'number', 20, { min: 1 }), field('guidance', 'Guidance', 'number', 7, { min: 0, step: .5 }), field('seed', 'Seed', 'number', -1), field('batch_count', 'Batch count', 'number', 1, { min: 1, max: 8 }), field('sampler', 'Sampler', 'select', 'auto', { options: ['auto', 'euler', 'euler-a', 'dpm++2m', 'lcm', 'ddim'] }), field('scheduler', 'Scheduler', 'select', 'auto', { options: ['auto', 'discrete', 'karras', 'exponential', 'ays', 'sgm-uniform', 'simple'] })
    ]},
    { id: 'edit', group: 'Create', icon: '✎', title: 'Edit', endpoint: '/v1/edit', description: 'Transform an image from a prompt, optionally through a mask.', model: 'diffusion', output: 'image', mask: true, fields: [
      field('prompt', 'Edit prompt', 'textarea', 'Replace the background with a sunlit forest', { required: true }), field('negative_prompt', 'Negative prompt', 'textarea', ''), field('strength', 'Strength', 'number', .75, { min: 0, max: 1, step: .05 }), field('steps', 'Steps', 'number', 20, { min: 1 }), field('guidance', 'Guidance', 'number', 7, { min: 0, step: .5 }), field('seed', 'Seed', 'number', -1), field('batch_count', 'Batch count', 'number', 1, { min: 1, max: 8 }), field('sampler', 'Sampler', 'select', 'auto', { options: ['auto', 'euler', 'euler-a', 'dpm++2m', 'lcm', 'ddim'] }), field('scheduler', 'Scheduler', 'select', 'auto', { options: ['auto', 'discrete', 'karras', 'exponential', 'ays', 'sgm-uniform', 'simple'] })
    ]}
  ];

  const $ = id => document.getElementById(id);
  const state = {
    operation: operations[0], image: null, imageUrl: '', outputUrl: '', result: null, points: [], health: null,
    started: 0, timer: 0, jobs: new Map(), autoShowJobs: new Set(), presets: [], pollingJobs: false
  };
  const el = {
    nav: $('operation-nav'), title: $('operation-title'), group: $('operation-group'), description: $('operation-description'),
    badge: $('model-badge'), endpoint: $('endpoint-label'), fields: $('dynamic-fields'), form: $('operation-form'), message: $('form-message'),
    run: $('run-button'), runLabel: $('run-label'), imageInput: $('image-input'), maskInput: $('mask-input'), maskField: $('mask-field'),
    empty: $('empty-stage'), preview: $('preview-stage'), generation: $('generation-stage'), image: $('preview-image'), canvas: $('image-canvas'),
    pointLayer: $('point-layer'), canvasHint: $('canvas-hint'), fileName: $('file-name'), imageSize: $('image-size'), overlay: $('working-overlay'),
    workingTitle: $('working-title'), workingTime: $('working-time'), results: $('results'), gallery: $('result-gallery'), text: $('text-result'),
    json: $('json-result'), jsonDetails: $('json-details'), summary: $('result-summary'), download: $('download-result'), copy: $('copy-result'),
    server: $('server-state'), serverLabel: $('server-label'), version: $('version-label'), background: $('background-run'),
    jobsButton: $('jobs-button'), jobCount: $('job-count'), drawer: $('drawer-shell'), jobList: $('job-list'),
    cacheSummary: $('cache-summary'), presetSelect: $('preset-select'), deletePreset: $('delete-preset')
  };

  const escapeHtml = value => String(value).replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'}[char]));
  const groups = [...new Set(operations.map(operation => operation.group))];

  function renderNav() {
    el.nav.innerHTML = groups.map(group => `<div class="tool-group"><span class="tool-group-label">${group}</span>${operations.filter(op => op.group === group).map(op => {
      const available = isAvailable(op);
      return `<button type="button" class="tool-button ${op.id === state.operation.id ? 'active' : ''} ${available ? '' : 'unavailable'}" data-operation="${op.id}"><span class="tool-icon">${op.icon}</span><span>${op.title}</span><i class="availability"></i></button>`;
    }).join('')}</div>`).join('');
  }

  function isAvailable(operation) {
    return !operation.model || !state.health || Boolean(state.health.configured_models?.[operation.model]);
  }

  function selectOperation(id) {
    const next = operations.find(operation => operation.id === id);
    if (!next) return;
    state.operation = next;
    state.points = [];
    clearResult();
    renderNav();
    el.title.textContent = next.title;
    el.group.textContent = next.group;
    el.description.textContent = next.description;
    el.endpoint.textContent = `POST ${next.endpoint}`;
    el.runLabel.textContent = `Run ${next.title.toLowerCase()}`;
    const available = isAvailable(next);
    const warm = next.model === 'vlm' || state.health?.model_cache?.loaded_families?.includes(next.model);
    el.badge.textContent = next.model ? `${next.model.toUpperCase()} · ${available ? (warm ? 'warm' : 'ready') : 'not configured'}` : 'No model needed';
    el.badge.classList.toggle('unavailable', !available);
    el.run.disabled = !available;
    el.message.hidden = available;
    if (!available) el.message.textContent = `Start the server with a ${next.model} model to use this operation.`;
    el.maskField.hidden = !next.mask;
    renderFields();
    syncBackgroundMode();
    renderPresets();
    updateStage();
  }

  function renderFields() {
    const fields = state.operation.fields;
    let html = '';
    for (let index = 0; index < fields.length; index += 1) {
      const item = fields[index];
      const next = fields[index + 1];
      const pair = item.type === 'number' && next?.type === 'number';
      if (pair) {
        html += `<div class="field-row">${fieldHtml(item)}${fieldHtml(next)}</div>`;
        index += 1;
      } else {
        html += fieldHtml(item);
      }
    }
    el.fields.innerHTML = html || '<p class="shortcut">This operation has no parameters.</p>';
  }

  function fieldHtml(item) {
    const id = `field-${item.name}`;
    if (item.type === 'checkbox') {
      return `<div class="field check-field"><label for="${id}">${escapeHtml(item.label)}</label><input id="${id}" name="${item.name}" type="checkbox" ${item.value ? 'checked' : ''}></div>`;
    }
    const attrs = [`id="${id}"`, `name="${item.name}"`, item.required ? 'required' : '', item.min !== undefined ? `min="${item.min}"` : '', item.max !== undefined ? `max="${item.max}"` : '', item.step !== undefined ? `step="${item.step}"` : '', item.placeholder ? `placeholder="${escapeHtml(item.placeholder)}"` : ''].filter(Boolean).join(' ');
    if (item.type === 'textarea') return `<div class="field"><label for="${id}">${escapeHtml(item.label)}</label><textarea ${attrs}>${escapeHtml(item.value)}</textarea></div>`;
    if (item.type === 'select') return `<div class="field"><label for="${id}">${escapeHtml(item.label)}</label><select ${attrs}>${item.options.map(option => `<option value="${escapeHtml(option)}" ${String(item.value) === String(option) ? 'selected' : ''}>${escapeHtml(option)}</option>`).join('')}</select></div>`;
    return `<div class="field"><label for="${id}">${escapeHtml(item.label)}</label><input type="${item.type}" value="${escapeHtml(item.value)}" ${attrs}></div>`;
  }

  function setImage(file) {
    if (!file || !file.type.startsWith('image/')) {
      showMessage('Choose a supported image file.');
      return;
    }
    if (state.imageUrl) URL.revokeObjectURL(state.imageUrl);
    state.image = file;
    state.imageUrl = URL.createObjectURL(file);
    state.points = [];
    el.image.onload = () => {
      el.imageSize.textContent = `${el.image.naturalWidth} × ${el.image.naturalHeight}`;
      positionPointLayer();
    };
    el.image.src = state.imageUrl;
    el.image.alt = file.name || 'Pasted input';
    el.fileName.textContent = file.name || 'clipboard-image';
    el.message.hidden = true;
    updateStage();
  }

  function updateStage() {
    const noInput = state.operation.input === 'none' || state.operation.input === 'text';
    el.generation.hidden = !noInput || Boolean(state.outputUrl);
    el.empty.hidden = noInput || Boolean(state.image);
    el.preview.hidden = noInput ? !state.outputUrl : !state.image;
    el.canvasHint.hidden = !state.operation.points || !state.image;
    if (state.outputUrl && !noInput) setView('output'); else if (state.image) setView('input');
    renderPoints();
  }

  function setView(view) {
    document.querySelectorAll('[data-view]').forEach(button => button.classList.toggle('active', button.dataset.view === view));
    if (view === 'output' && state.outputUrl) {
      el.image.src = state.outputUrl;
      el.image.alt = `${state.operation.title} result`;
      el.fileName.textContent = `${state.operation.id}-result.png`;
      el.canvasHint.hidden = true;
    } else if (state.imageUrl) {
      el.image.src = state.imageUrl;
      el.image.alt = state.image?.name || 'Selected input';
      el.fileName.textContent = state.image?.name || 'clipboard-image';
      el.canvasHint.hidden = !state.operation.points;
    }
  }

  function positionPointLayer() {
    const imageRect = el.image.getBoundingClientRect();
    const canvasRect = el.canvas.getBoundingClientRect();
    Object.assign(el.pointLayer.style, { left: `${imageRect.left - canvasRect.left}px`, top: `${imageRect.top - canvasRect.top}px`, width: `${imageRect.width}px`, height: `${imageRect.height}px` });
    renderPoints();
  }

  function addPoint(event) {
    if (!state.operation.points || !state.image || el.image.src !== state.imageUrl) return;
    const rect = el.image.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) return;
    const x = Math.round((event.clientX - rect.left) * el.image.naturalWidth / rect.width);
    const y = Math.round((event.clientY - rect.top) * el.image.naturalHeight / rect.height);
    state.points.push([x, y, !event.shiftKey]);
    const pointsField = $('field-points');
    if (pointsField) pointsField.value = JSON.stringify(state.points);
    renderPoints();
  }

  function renderPoints() {
    el.pointLayer.innerHTML = state.points.map(point => `<i class="prompt-point ${point[2] ? '' : 'negative'}" style="left:${point[0] / (el.image.naturalWidth || 1) * 100}%;top:${point[1] / (el.image.naturalHeight || 1) * 100}%"></i>`).join('');
  }

  function formValues() {
    const values = {};
    for (const item of state.operation.fields) {
      const input = $(`field-${item.name}`);
      if (!input) continue;
      if (item.type === 'checkbox') values[item.name] = input.checked ? 'true' : 'false';
      else if (input.value.trim() !== '') values[item.name] = input.value.trim();
    }
    return values;
  }

  async function runOperation(event) {
    event?.preventDefault();
    const operation = state.operation;
    if (operation.input !== 'none' && operation.input !== 'text' && !state.image) {
      showMessage('Add an input image first.');
      return;
    }
    if (!isAvailable(operation)) return;
    const values = formValues();
    if ((operation.points && !values.points && !values.box)) {
      showMessage('Click the subject in the image or enter a point or box prompt.');
      return;
    }
    const background = el.background.checked;
    if (background && values.stream === 'true') values.stream = 'false';
    showMessage('');
    if (!background) {
      setWorking(true);
      clearResult();
    }
    try {
      const options = { method: 'POST', headers: {} };
      if (background) options.headers.Prefer = 'respond-async';
      if (operation.id === 'generate' || operation.id === 'embed-text') {
        if (operation.id === 'generate') values.response = 'image';
        options.headers['Content-Type'] = 'application/json';
        options.body = JSON.stringify(values);
      } else {
        const body = new FormData();
        body.append('image', state.image, state.image.name || 'input.png');
        for (const [key, value] of Object.entries(values)) body.append(key, value);
        if (operation.output === 'image') body.set('response', 'image');
        if (operation.mask && el.maskInput.files[0]) body.append('mask', el.maskInput.files[0]);
        options.body = body;
        if (values.stream === 'true') options.headers.Accept = 'text/event-stream';
      }
      const response = await fetch(operation.endpoint, options);
      if (!response.ok) throw await responseError(response);
      if (background) {
        const job = await response.json();
        state.jobs.set(job.id, job);
        state.autoShowJobs.add(job.id);
        renderJobs();
        openJobs();
        refreshJobs();
      } else {
        await consumeResponse(response);
      }
    } catch (error) {
      showMessage(error.message || String(error));
    } finally {
      if (!background) setWorking(false);
    }
  }

  async function consumeResponse(response) {
    const contentType = response.headers.get('content-type') || '';
    if (contentType.startsWith('text/event-stream')) await consumeStream(response);
    else if (contentType.startsWith('image/')) showImageBlob(await response.blob(), contentType);
    else showJson(await response.json());
  }

  async function responseError(response) {
    try {
      const body = await response.json();
      return new Error(body.error?.message || `${response.status} ${response.statusText}`);
    } catch (_) {
      return new Error(`${response.status} ${response.statusText}`);
    }
  }

  async function consumeStream(response) {
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '', accumulated = '', final = null;
    el.text.hidden = false;
    el.results.hidden = false;
    el.text.textContent = '';
    while (true) {
      const { value, done } = await reader.read();
      buffer += decoder.decode(value || new Uint8Array(), { stream: !done });
      const events = buffer.split('\n\n');
      buffer = events.pop() || '';
      for (const raw of events) {
        const eventName = raw.match(/^event:\s*(.+)$/m)?.[1];
        const dataLine = raw.match(/^data:\s*(.+)$/m)?.[1];
        if (!dataLine) continue;
        const data = JSON.parse(dataLine);
        if (eventName === 'delta') { accumulated += data.delta || ''; el.text.textContent = accumulated; }
        if (eventName === 'done') final = data;
        if (eventName === 'error') throw new Error(data.error?.message || 'Streaming request failed');
      }
      if (done) break;
    }
    showJson(final || { text: accumulated }, true);
  }

  function showImageBlob(blob, mime) {
    if (state.outputUrl) URL.revokeObjectURL(state.outputUrl);
    state.outputUrl = URL.createObjectURL(blob);
    state.result = { kind: 'blob', blob, mime };
    el.results.hidden = false;
    el.gallery.innerHTML = resultCard(state.outputUrl, 0);
    el.text.hidden = true;
    el.jsonDetails.hidden = true;
    el.copy.hidden = true;
    el.download.hidden = false;
    el.summary.textContent = `${state.operation.title} completed · ${formatBytes(blob.size)}`;
    updateStage();
    setView('output');
  }

  function showJson(data, preserveText = false) {
    state.result = { kind: 'json', data };
    const images = findImages(data);
    el.results.hidden = false;
    el.gallery.innerHTML = images.map((image, index) => resultCard(`data:${image.mime_type || 'image/png'};base64,${image.base64}`, index)).join('');
    el.gallery.hidden = images.length === 0;
    const text = data?.text || (state.operation.id === 'ocr' ? data?.text : '');
    if (!preserveText) { el.text.textContent = text || summarize(data); el.text.hidden = !(text || summarize(data)); }
    el.json.textContent = JSON.stringify(data, null, 2);
    el.jsonDetails.hidden = false;
    el.copy.hidden = false;
    el.download.hidden = false;
    el.summary.textContent = summarize(data) || `${state.operation.title} completed`;
    if (images[0]) {
      if (state.outputUrl) URL.revokeObjectURL(state.outputUrl);
      state.outputUrl = `data:${images[0].mime_type || 'image/png'};base64,${images[0].base64}`;
      if (state.image) setView('output');
    }
  }

  function findImages(value, found = [], seen = new Set()) {
    if (!value || typeof value !== 'object' || seen.has(value)) return found;
    seen.add(value);
    if (typeof value.base64 === 'string' && (value.mime_type || value.format)) found.push(value);
    for (const nested of Object.values(value)) findImages(nested, found, seen);
    return found;
  }

  function summarize(data) {
    if (data?.text) return data.text.length > 90 ? `${data.text.slice(0, 90)}…` : data.text;
    if (Array.isArray(data?.embedding)) return `${data.embedding.length}-dimensional embedding`;
    if (Array.isArray(data?.classifications)) return `${data.classifications.length} labels ranked`;
    if (Array.isArray(data?.segments)) return `${data.segments.length} mask${data.segments.length === 1 ? '' : 's'} found`;
    if (Array.isArray(data?.detections)) return `${data.detections.length} detection${data.detections.length === 1 ? '' : 's'} found`;
    if (Array.isArray(data?.images)) return `${data.images.length} image${data.images.length === 1 ? '' : 's'} created`;
    if (data?.depth) return 'Depth map ready';
    return '';
  }

  function resultCard(url, index) {
    return `<div class="result-card"><img src="${url}" alt="Result ${index + 1}"><a href="${url}" download="imagecpp-${state.operation.id}-${index + 1}.png">Save PNG</a></div>`;
  }

  function clearResult() {
    el.results.hidden = true;
    el.gallery.innerHTML = '';
    el.text.textContent = '';
    el.json.textContent = '';
    state.result = null;
    if (state.outputUrl?.startsWith('blob:')) URL.revokeObjectURL(state.outputUrl);
    state.outputUrl = '';
    if (state.image) setView('input');
  }

  function showMessage(message) {
    el.message.textContent = message;
    el.message.hidden = !message;
  }

  function setWorking(working) {
    el.overlay.hidden = !working;
    el.run.disabled = working || !isAvailable(state.operation);
    if (working) {
      state.started = performance.now();
      el.workingTitle.textContent = `${state.operation.title} running`;
      state.timer = window.setInterval(() => { el.workingTime.textContent = `${((performance.now() - state.started) / 1000).toFixed(1)}s`; }, 100);
    } else {
      window.clearInterval(state.timer);
    }
  }

  function openJobs() {
    el.drawer.hidden = false;
    refreshJobs();
  }

  function closeJobs() {
    el.drawer.hidden = true;
  }

  function renderJobs() {
    const jobs = [...state.jobs.values()].sort((left, right) => right.created_at_ms - left.created_at_ms);
    el.jobCount.textContent = jobs.length > 99 ? '99+' : String(jobs.length);
    if (!jobs.length) {
      el.jobList.innerHTML = '<p>No background jobs yet.</p>';
      return;
    }
    el.jobList.innerHTML = jobs.map(job => {
      const operation = operations.find(item => item.id === job.operation);
      const active = job.status === 'queued' || job.status === 'running';
      const progress = Math.max(0, Math.min(100, Math.round(Number(job.progress || 0) * 100)));
      const stage = job.queue_position ? `queue position ${job.queue_position}` : (job.stage || job.status);
      const action = active
        ? `<button class="quiet-button" type="button" data-cancel-job="${escapeHtml(job.id)}">Cancel</button>`
        : job.status === 'completed' || job.status === 'failed'
          ? `<button class="quiet-button" type="button" data-view-job="${escapeHtml(job.id)}">${job.status === 'failed' ? 'Error' : 'View'}</button>`
          : '';
      return `<article class="job-item">
        <div class="job-item-top"><div class="job-item-title"><strong>${escapeHtml(operation?.title || job.operation)}</strong><span>${escapeHtml(job.id)}</span></div><i class="job-status ${escapeHtml(job.status)}">${escapeHtml(job.status)}</i></div>
        <div class="job-progress"><i style="width:${progress}%"></i></div>
        <div class="job-item-bottom"><span>${escapeHtml(stage)} · ${progress}% · ${timeAgo(job.created_at_ms)}</span><div class="job-actions">${action}</div></div>
      </article>`;
    }).join('');
  }

  async function refreshJobs() {
    if (state.pollingJobs) return;
    state.pollingJobs = true;
    try {
      const response = await fetch('/v1/jobs?limit=50');
      if (!response.ok) throw await responseError(response);
      const data = await response.json();
      state.jobs = new Map((data.jobs || []).map(job => [job.id, job]));
      renderJobs();
      for (const id of [...state.autoShowJobs]) {
        const job = state.jobs.get(id);
        if (!job) continue;
        if (job.status === 'completed' && job.operation === state.operation.id) {
          state.autoShowJobs.delete(id);
          loadJobResult(job);
          break;
        }
        if (job.status === 'failed' || job.status === 'cancelled') state.autoShowJobs.delete(id);
      }
    } catch (_) {
      // The server indicator owns connectivity errors; retain the last known job list.
    } finally {
      state.pollingJobs = false;
    }
  }

  async function loadJobResult(job) {
    try {
      const operation = operations.find(item => item.id === job.operation);
      if (operation) selectOperation(operation.id);
      const response = await fetch(job.result_url || `/v1/jobs/${job.id}/result`);
      if (!response.ok) throw await responseError(response);
      await consumeResponse(response);
      closeJobs();
    } catch (error) {
      showMessage(error.message || String(error));
    }
  }

  async function cancelJob(job) {
    try {
      const response = await fetch(job.status_url || `/v1/jobs/${job.id}`, { method: 'DELETE' });
      if (!response.ok) throw await responseError(response);
      state.jobs.set(job.id, await response.json());
      state.autoShowJobs.delete(job.id);
      renderJobs();
      refreshJobs();
    } catch (error) {
      showMessage(error.message || String(error));
    }
  }

  function updateRuntime() {
    const cache = state.health?.model_cache;
    if (!cache) {
      el.cacheSummary.textContent = 'Unavailable';
      return;
    }
    const families = cache.loaded_families || [];
    const capacity = Number(cache.capacity || 0);
    el.cacheSummary.textContent = capacity === 0
      ? `Disabled · ${cache.misses || 0} loads`
      : `${families.length}/${capacity} warm · ${cache.hits || 0} hits${families.length ? ` · ${families.join(', ')}` : ''}`;
  }

  async function clearModelCache() {
    const button = $('clear-cache');
    button.disabled = true;
    try {
      const response = await fetch('/v1/models/cache', { method: 'DELETE' });
      if (!response.ok) throw await responseError(response);
      const data = await response.json();
      if (state.health) state.health.model_cache = data.cache;
      updateRuntime();
      selectOperation(state.operation.id);
    } catch (error) {
      showMessage(error.message || String(error));
    } finally {
      button.disabled = false;
    }
  }

  function loadPresets() {
    try {
      const parsed = JSON.parse(localStorage.getItem('imagecpp.presets.v1') || '[]');
      state.presets = Array.isArray(parsed) ? parsed : [];
    } catch (_) {
      state.presets = [];
    }
  }

  function storePresets() {
    try { localStorage.setItem('imagecpp.presets.v1', JSON.stringify(state.presets)); } catch (_) { /* Storage is optional. */ }
  }

  function renderPresets(selected = '') {
    const presets = state.presets.filter(preset => preset.operation === state.operation.id);
    el.presetSelect.innerHTML = `<option value="">Saved presets</option>${presets.map(preset => `<option value="${escapeHtml(preset.id)}">${escapeHtml(preset.name)}</option>`).join('')}`;
    if (selected && presets.some(preset => preset.id === selected)) el.presetSelect.value = selected;
    el.deletePreset.disabled = !el.presetSelect.value;
  }

  function applyPreset(id) {
    const preset = state.presets.find(item => item.id === id);
    if (!preset) return;
    if (preset.operation !== state.operation.id) selectOperation(preset.operation);
    for (const [name, value] of Object.entries(preset.values || {})) {
      const input = $(`field-${name}`);
      if (!input) continue;
      if (input.type === 'checkbox') input.checked = String(value) === 'true';
      else input.value = value;
    }
    const points = preset.values?.points;
    if (points) {
      try { state.points = JSON.parse(points); renderPoints(); } catch (_) { state.points = []; }
    }
    el.background.checked = Boolean(preset.background);
    syncBackgroundMode();
    renderPresets(id);
  }

  function savePreset() {
    const name = window.prompt('Name this preset');
    if (!name?.trim()) return;
    const id = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
    state.presets.push({ id, name: name.trim(), operation: state.operation.id, values: formValues(), background: el.background.checked });
    storePresets();
    renderPresets(id);
  }

  function deletePreset() {
    const id = el.presetSelect.value;
    if (!id) return;
    state.presets = state.presets.filter(preset => preset.id !== id);
    storePresets();
    renderPresets();
  }

  function syncBackgroundMode() {
    const stream = $('field-stream');
    if (!stream) return;
    if (el.background.checked) stream.checked = false;
    stream.disabled = el.background.checked;
  }

  function timeAgo(timestamp) {
    const seconds = Math.max(0, Math.round((Date.now() - Number(timestamp || Date.now())) / 1000));
    if (seconds < 60) return `${seconds}s ago`;
    const minutes = Math.round(seconds / 60);
    if (minutes < 60) return `${minutes}m ago`;
    return `${Math.round(minutes / 60)}h ago`;
  }

  async function refreshHealth() {
    el.server.className = 'server-state';
    el.serverLabel.textContent = 'Connecting';
    try {
      const response = await fetch('/healthz');
      if (!response.ok) throw new Error('health request failed');
      state.health = await response.json();
      el.server.classList.add('ready');
      const count = Object.values(state.health.configured_models || {}).filter(Boolean).length;
      el.serverLabel.textContent = `Ready · ${count} models`;
      el.version.textContent = `image.cpp ${state.health.version || ''}`;
      updateRuntime();
      renderNav();
      selectOperation(state.operation.id);
    } catch (_) {
      el.server.classList.add('error');
      el.serverLabel.textContent = 'Server unavailable';
    }
  }

  function downloadResult() {
    if (!state.result) return;
    if (state.result.kind === 'blob') {
      const link = document.createElement('a');
      link.href = state.outputUrl; link.download = `imagecpp-${state.operation.id}.png`; link.click();
    } else {
      const blob = new Blob([JSON.stringify(state.result.data, null, 2)], { type: 'application/json' });
      const link = document.createElement('a'); const url = URL.createObjectURL(blob);
      link.href = url; link.download = `imagecpp-${state.operation.id}.json`; link.click();
      window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
  }

  const formatBytes = bytes => bytes < 1024 * 1024 ? `${(bytes / 1024).toFixed(1)} KB` : `${(bytes / 1024 / 1024).toFixed(1)} MB`;

  el.nav.addEventListener('click', event => { const button = event.target.closest('[data-operation]'); if (button) selectOperation(button.dataset.operation); });
  $('choose-image').addEventListener('click', () => el.imageInput.click());
  $('replace-image').addEventListener('click', () => el.imageInput.click());
  el.imageInput.addEventListener('change', () => setImage(el.imageInput.files[0]));
  el.form.addEventListener('submit', runOperation);
  $('reset-form').addEventListener('click', () => selectOperation(state.operation.id));
  $('refresh-health').addEventListener('click', () => refreshHealth());
  el.jobsButton.addEventListener('click', openJobs);
  $('close-jobs').addEventListener('click', closeJobs);
  $('drawer-backdrop').addEventListener('click', closeJobs);
  $('refresh-jobs').addEventListener('click', refreshJobs);
  $('clear-cache').addEventListener('click', clearModelCache);
  el.jobList.addEventListener('click', event => {
    const cancel = event.target.closest('[data-cancel-job]');
    const view = event.target.closest('[data-view-job]');
    if (cancel) cancelJob(state.jobs.get(cancel.dataset.cancelJob));
    if (view) loadJobResult(state.jobs.get(view.dataset.viewJob));
  });
  el.background.addEventListener('change', syncBackgroundMode);
  el.presetSelect.addEventListener('change', () => {
    el.deletePreset.disabled = !el.presetSelect.value;
    if (el.presetSelect.value) applyPreset(el.presetSelect.value);
  });
  $('save-preset').addEventListener('click', savePreset);
  $('delete-preset').addEventListener('click', deletePreset);
  el.canvas.addEventListener('click', addPoint);
  window.addEventListener('resize', positionPointLayer);
  document.querySelector('.view-toggle').addEventListener('click', event => { if (event.target.dataset.view) setView(event.target.dataset.view); });
  el.download.addEventListener('click', downloadResult);
  el.copy.addEventListener('click', async () => { if (state.result?.kind === 'json') { await navigator.clipboard.writeText(JSON.stringify(state.result.data, null, 2)); el.copy.textContent = 'Copied'; window.setTimeout(() => { el.copy.textContent = 'Copy JSON'; }, 1200); } });
  document.addEventListener('keydown', event => {
    if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') runOperation(event);
    if (event.key === 'Escape') {
      if (!el.drawer.hidden) closeJobs(); else clearResult();
    }
  });
  document.addEventListener('paste', event => { const file = [...(event.clipboardData?.files || [])].find(item => item.type.startsWith('image/')); if (file) setImage(file); });
  for (const eventName of ['dragenter', 'dragover']) $('stage').addEventListener(eventName, event => { event.preventDefault(); $('stage').classList.add('dragging'); });
  for (const eventName of ['dragleave', 'drop']) $('stage').addEventListener(eventName, event => { event.preventDefault(); $('stage').classList.remove('dragging'); });
  $('stage').addEventListener('drop', event => setImage([...event.dataTransfer.files].find(file => file.type.startsWith('image/'))));

  loadPresets();
  renderNav();
  selectOperation('resize');
  refreshHealth();
  refreshJobs();
  window.setInterval(refreshJobs, 1500);
})();)IMAGECPP_JS";

} // namespace

std::string_view playground_html() noexcept { return kHtml; }
std::string_view playground_css() noexcept { return kCss; }
std::string_view playground_javascript() noexcept { return kJavascript; }

} // namespace imagecpp::server
