/*
 *  Arstro Kitchen Sink — web UI controller.
 *
 *  Loads the WASM module (the real Arstro DSP), builds the chain/generator UI from
 *  the engine's self-describing node catalogue, renders audio through a Web Audio
 *  ScriptProcessorNode, and visualizes the output (waveform + spectrum).
 */
"use strict";

let M = null;          // wasm module
let engine = null;     // RackEngine instance
let audio = null;      // AudioContext
let analyser = null;
let started = false;
const BUF = 1024;      // ScriptProcessor block size (frames)
const CH = 2;

// Generator ADSR state (ms / 0..1), mirrored to the engine.
const env = { attack: 10, decay: 120, sustain: 0.7, release: 200 };

// ---------------------------------------------------------------- wasm load
createArstroModule().then((mod) => {
  M = mod;
  engine = new M.RackEngine();
  buildPalette(JSON.parse(M.nodeTypesJson()));
  pushEnvToEngine();
  renderChain();
  drawAdsr();
  setStatus("ready — click ▶ Start audio");
}).catch((e) => setStatus("wasm load failed: " + e));

function setStatus(t) { document.getElementById("status").textContent = t; }

// ---------------------------------------------------------------- audio start
document.getElementById("start").addEventListener("click", () => {
  if (started || !engine) return;
  audio = new (window.AudioContext || window.webkitAudioContext)();
  engine.setSampleRate(audio.sampleRate);

  const node = audio.createScriptProcessor(BUF, 0, CH);
  node.onaudioprocess = (e) => {
    const ptr = engine.renderBlock(BUF);     // interleaved stereo at this heap addr
    const heap = M.HEAPF32;
    const base = ptr >> 2;
    const L = e.outputBuffer.getChannelData(0);
    const R = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < BUF; i++) { L[i] = heap[base + i * 2]; R[i] = heap[base + i * 2 + 1]; }
  };

  analyser = audio.createAnalyser();
  analyser.fftSize = 2048;
  node.connect(analyser);
  analyser.connect(audio.destination);

  started = true;
  setStatus("playing — " + audio.sampleRate + " Hz");
  requestAnimationFrame(drawScopes);
});

// ---------------------------------------------------------------- generator controls
function bindRange(id, fn) {
  const el = document.getElementById(id);
  const out = document.getElementById(id + "Out");
  const update = () => { if (out) out.textContent = el.value; fn(parseFloat(el.value)); };
  el.addEventListener("input", update);
  update();
}
bindRange("freq", (v) => engine && engine.setFrequency(v));
bindRange("voices", (v) => engine && engine.setVoiceCount(v | 0));
bindRange("detune", (v) => engine && engine.setDetuneCents(v));
document.getElementById("waveform").addEventListener("change", (e) => engine && engine.setWaveform(parseInt(e.target.value)));

function pushEnvToEngine() {
  if (!engine) return;
  engine.setAttackMs(env.attack);
  engine.setDecayMs(env.decay);
  engine.setSustain(env.sustain);
  engine.setReleaseMs(env.release);
  const r = document.getElementById("adsrReadout");
  r.textContent = `A ${env.attack|0}ms · D ${env.decay|0}ms · S ${env.sustain.toFixed(2)} · R ${env.release|0}ms`;
}

// ---------------------------------------------------------------- chain palette + UI
let catalogue = [];
function buildPalette(types) {
  catalogue = types;
  const pal = document.getElementById("palette");
  pal.innerHTML = "";
  types.forEach((t, i) => {
    const b = document.createElement("button");
    b.textContent = "+ " + t.name;
    b.addEventListener("click", () => { engine.addNode(i); renderChain(); });
    pal.appendChild(b);
  });
}

function renderChain() {
  const list = document.getElementById("chain");
  list.innerHTML = "";
  const n = engine.nodeCount();
  if (n === 0) {
    const li = document.createElement("li");
    li.className = "empty";
    li.textContent = "empty — add processors from the palette above";
    list.appendChild(li);
    return;
  }
  for (let i = 0; i < n; i++) {
    const type = catalogue[engine.nodeTypeAt(i)];
    const li = document.createElement("li");
    li.className = "node";

    const head = document.createElement("div");
    head.className = "head";
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = (i + 1) + ". " + type.name;
    head.appendChild(name);
    head.appendChild(mkBtn("↑", () => { engine.moveNode(i, -1); renderChain(); }));
    head.appendChild(mkBtn("↓", () => { engine.moveNode(i, 1); renderChain(); }));
    head.appendChild(mkBtn("✕", () => { engine.removeNode(i); renderChain(); }));
    li.appendChild(head);

    const params = document.createElement("div");
    params.className = "params";
    type.params.forEach((pd, pi) => {
      const lab = document.createElement("label");
      const span = document.createElement("span");
      const out = document.createElement("output");
      out.textContent = (+pd.def).toFixed(2);
      span.textContent = pd.name + " ";
      span.appendChild(out);
      const r = document.createElement("input");
      r.type = "range"; r.min = pd.min; r.max = pd.max; r.value = pd.def;
      r.step = (pd.max - pd.min) / 200;
      r.addEventListener("input", () => {
        const v = parseFloat(r.value);
        out.textContent = v.toFixed(2);
        engine.setNodeParam(i, pi, v);
      });
      lab.appendChild(span);
      lab.appendChild(r);
      params.appendChild(lab);
    });
    li.appendChild(params);
    list.appendChild(li);
  }
}
function mkBtn(txt, fn) { const b = document.createElement("button"); b.textContent = txt; b.addEventListener("click", fn); return b; }

// ---------------------------------------------------------------- ADSR editor (draggable)
const adsr = document.getElementById("adsr");
const actx = adsr.getContext("2d");
const MS_SPAN = 600;                 // ms mapped across the attack/decay/release zones
const SUSTAIN_HOLD_PX = 90;          // flat sustain segment width
let drag = null;                     // 'a' | 'ds' | 'r'

function envPoints() {
  const W = adsr.width, H = adsr.height, pad = 12;
  const usable = W - SUSTAIN_HOLD_PX - 2 * pad;
  const pxPerMs = usable / (3 * MS_SPAN);   // attack + decay + release share the zone
  const x0 = pad, y0 = H - pad, top = pad;
  const ax = x0 + env.attack * pxPerMs;
  const dx = ax + env.decay * pxPerMs;
  const sy = top + (1 - env.sustain) * (y0 - top);
  const hx = dx + SUSTAIN_HOLD_PX;
  const rx = hx + env.release * pxPerMs;
  return { x0, y0, top, ax, dx, sy, hx, rx, pxPerMs };
}

function drawAdsr() {
  const W = adsr.width, H = adsr.height;
  actx.clearRect(0, 0, W, H);
  const p = envPoints();
  actx.strokeStyle = "#5cc8ff"; actx.lineWidth = 2;
  actx.beginPath();
  actx.moveTo(p.x0, p.y0);
  actx.lineTo(p.ax, p.top);   // attack -> peak
  actx.lineTo(p.dx, p.sy);    // decay -> sustain
  actx.lineTo(p.hx, p.sy);    // sustain hold
  actx.lineTo(p.rx, p.y0);    // release -> 0
  actx.stroke();
  for (const h of [[p.ax, p.top], [p.dx, p.sy], [p.rx, p.y0]]) {
    actx.fillStyle = "#ff8a5c";
    actx.beginPath(); actx.arc(h[0], h[1], 5, 0, 7); actx.fill();
  }
}

adsr.addEventListener("pointerdown", (e) => {
  const r = adsr.getBoundingClientRect();
  const x = (e.clientX - r.left) * adsr.width / r.width;
  const p = envPoints();
  const near = (hx) => Math.abs(x - hx) < 28;
  drag = near(p.ax) ? "a" : near(p.dx) ? "ds" : near(p.rx) ? "r" : null;
  if (drag) adsr.setPointerCapture(e.pointerId);
});
adsr.addEventListener("pointermove", (e) => {
  if (!drag) return;
  const r = adsr.getBoundingClientRect();
  const x = (e.clientX - r.left) * adsr.width / r.width;
  const y = (e.clientY - r.top) * adsr.height / r.height;
  const p = envPoints();
  const clampMs = (ms) => Math.max(1, Math.min(MS_SPAN, ms));
  if (drag === "a") env.attack = clampMs((x - p.x0) / p.pxPerMs);
  else if (drag === "ds") {
    env.decay = clampMs((x - p.ax) / p.pxPerMs);
    env.sustain = Math.max(0, Math.min(1, 1 - (y - p.top) / (p.y0 - p.top)));
  } else if (drag === "r") env.release = clampMs((x - p.hx) / p.pxPerMs);
  pushEnvToEngine();
  drawAdsr();
});
adsr.addEventListener("pointerup", () => { drag = null; });

// ---------------------------------------------------------------- visualizers
const scope = document.getElementById("scope"), sctx = scope.getContext("2d");
const spec = document.getElementById("spectrum"), pctx = spec.getContext("2d");
let timeBuf = null, freqBuf = null;

function drawScopes() {
  if (!analyser) return;
  if (!timeBuf) { timeBuf = new Float32Array(analyser.fftSize); freqBuf = new Uint8Array(analyser.frequencyBinCount); }
  analyser.getFloatTimeDomainData(timeBuf);
  analyser.getByteFrequencyData(freqBuf);

  // waveform
  sctx.fillStyle = "#0c0e13"; sctx.fillRect(0, 0, scope.width, scope.height);
  sctx.strokeStyle = "#5cc8ff"; sctx.lineWidth = 2; sctx.beginPath();
  for (let i = 0; i < timeBuf.length; i++) {
    const x = i / timeBuf.length * scope.width;
    const y = (0.5 - timeBuf[i] * 0.5) * scope.height;
    i ? sctx.lineTo(x, y) : sctx.moveTo(x, y);
  }
  sctx.stroke();

  // spectrum
  pctx.fillStyle = "#0c0e13"; pctx.fillRect(0, 0, spec.width, spec.height);
  const bins = freqBuf.length, bw = spec.width / bins;
  for (let i = 0; i < bins; i++) {
    const h = freqBuf[i] / 255 * spec.height;
    pctx.fillStyle = "hsl(" + (200 - i / bins * 160) + ",80%,60%)";
    pctx.fillRect(i * bw, spec.height - h, bw + 1, h);
  }
  requestAnimationFrame(drawScopes);
}

// ---------------------------------------------------------------- keyboard
const KEYS = "awsedftgyhujk";       // C4..C5 chromatic
const heldKeys = {};
window.addEventListener("keydown", (e) => {
  if (!engine || e.repeat) return;
  const i = KEYS.indexOf(e.key.toLowerCase());
  if (i < 0 || heldKeys[e.key]) return;
  heldKeys[e.key] = 60 + i;
  engine.noteOnMidi(60 + i, 0.85);
});
window.addEventListener("keyup", (e) => {
  if (!engine) return;
  const note = heldKeys[e.key];
  if (note != null) { engine.noteOff(note); delete heldKeys[e.key]; }
});
