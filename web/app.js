const API = "/api/state";
const POLL_INTERVAL_MS = 2000;
const SEND_DEBOUNCE_MS = 80;

let sendTimer = null;
let knownVideos = null;
let pickerDragging = false;
let pickerHue = 0; // 0-360
let pickerSat = 0; // 0-100
let pickerVal = 0; // 0-100
let currentSolidRgb = { r: 0, g: 0, b: 0 };
let knownPreviewVideo = null;

const els = {
  status: document.getElementById("status"),
  video: document.getElementById("video"),
  started: document.getElementById("started"),
  startedLabel: document.getElementById("startedLabel"),
  paused: document.getElementById("paused"),
  pausedLabel: document.getElementById("pausedLabel"),
  hue: document.getElementById("hue"),
  sat: document.getElementById("sat"),
  val: document.getElementById("val"),
  fps: document.getElementById("fps"),
  svArea: document.getElementById("svArea"),
  svCursor: document.getElementById("svCursor"),
  hueArea: document.getElementById("hueArea"),
  hueCursor: document.getElementById("hueCursor"),
  solid_w: document.getElementById("solid_w"),
  solidControls: document.getElementById("solidControls"),
  videoControls: document.getElementById("videoControls"),
  swatch: document.getElementById("swatch"),
  preview: document.getElementById("preview"),
  presets: document.getElementById("presets"),
  presetName: document.getElementById("presetName"),
  savePreset: document.getElementById("savePreset"),
};
const previewCtx = els.preview.getContext("2d");

function setStatus(ok) {
  els.status.classList.toggle("status--ok", ok);
  els.status.classList.toggle("status--bad", !ok);
}

function setSliderValue(input, labelId, value) {
  // Don't clobber a slider the user is actively dragging.
  if (document.activeElement !== input)
    input.value = value;
  document.getElementById(labelId).textContent = value;
}

function hsvToRgb(h, s, v) {
  s /= 100;
  v /= 100;
  const c = v * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = v - c;
  let r1, g1, b1;
  if (h < 60) [r1, g1, b1] = [c, x, 0];
  else if (h < 120) [r1, g1, b1] = [x, c, 0];
  else if (h < 180) [r1, g1, b1] = [0, c, x];
  else if (h < 240) [r1, g1, b1] = [0, x, c];
  else if (h < 300) [r1, g1, b1] = [x, 0, c];
  else [r1, g1, b1] = [c, 0, x];
  return {
    r: Math.round((r1 + m) * 255),
    g: Math.round((g1 + m) * 255),
    b: Math.round((b1 + m) * 255),
  };
}

function rgbToHsv(r, g, b) {
  r /= 255;
  g /= 255;
  b /= 255;
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  const d = max - min;
  let h = 0;
  if (d !== 0) {
    if (max === r) h = 60 * (((g - b) / d) % 6);
    else if (max === g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
  }
  if (h < 0) h += 360;
  return { h, s: max === 0 ? 0 : (d / max) * 100, v: max * 100 };
}

// Approximates what an RGBW LED looks like once the white channel is
// added on top of the picked color, so the preview isn't just the raw
// hue -- adding white washes it out toward white, not toward gray.
function blendWhite(r, g, b, w) {
  const f = w / 255;
  return {
    r: Math.round(r + (255 - r) * f),
    g: Math.round(g + (255 - g) * f),
    b: Math.round(b + (255 - b) * f),
  };
}

function renderPickerUI() {
  els.svArea.style.backgroundColor = `hsl(${pickerHue}, 100%, 50%)`;
  els.svCursor.style.left = `${pickerSat}%`;
  els.svCursor.style.top = `${100 - pickerVal}%`;
  els.hueCursor.style.top = `${100 - (pickerHue / 360) * 100}%`;
}

function updateSwatch() {
  const { r, g, b } = currentSolidRgb;
  const blended = blendWhite(r, g, b, Number(els.solid_w.value));
  els.swatch.style.background = `rgb(${blended.r}, ${blended.g}, ${blended.b})`;
}

function sendSolid() {
  debouncedSend({
    solid_r: currentSolidRgb.r,
    solid_g: currentSolidRgb.g,
    solid_b: currentSolidRgb.b,
    solid_w: els.solid_w.value,
  });
}

function pickerChanged() {
  currentSolidRgb = hsvToRgb(pickerHue, pickerSat, pickerVal);
  renderPickerUI();
  updateSwatch();
  sendSolid();
}

function bindDrag(area, onMove) {
  area.addEventListener("pointerdown", (e) => {
    pickerDragging = true;
    area.setPointerCapture(e.pointerId);
    onMove(e);
  });
  area.addEventListener("pointermove", (e) => {
    if (pickerDragging)
      onMove(e);
  });
  area.addEventListener("pointerup", () => {
    pickerDragging = false;
  });
  area.addEventListener("pointercancel", () => {
    pickerDragging = false;
  });
}

bindDrag(els.svArea, (e) => {
  const rect = els.svArea.getBoundingClientRect();
  const x = Math.min(Math.max(e.clientX - rect.left, 0), rect.width);
  const y = Math.min(Math.max(e.clientY - rect.top, 0), rect.height);
  pickerSat = (x / rect.width) * 100;
  pickerVal = 100 - (y / rect.height) * 100;
  pickerChanged();
});

bindDrag(els.hueArea, (e) => {
  const rect = els.hueArea.getBoundingClientRect();
  const y = Math.min(Math.max(e.clientY - rect.top, 0), rect.height);
  pickerHue = (1 - y / rect.height) * 360;
  pickerChanged();
});

function populateVideoOptions(videos) {
  const key = JSON.stringify(videos);
  if (key === knownVideos)
    return;
  knownVideos = key;

  els.video.innerHTML = "";
  for (const name of videos) {
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    els.video.appendChild(opt);
  }
  const solid = document.createElement("option");
  solid.value = "solid";
  solid.textContent = "Solid Color";
  els.video.appendChild(solid);
}

async function updatePreview(videoName) {
  if (videoName === knownPreviewVideo || videoName === "solid")
    return;
  knownPreviewVideo = videoName;

  try {
    const res = await fetch(`/api/preview/${encodeURIComponent(videoName)}`);
    const data = await res.json();
    const img = previewCtx.createImageData(data.width, data.height);
    for (let i = 0; i < data.width * data.height; i++) {
      img.data[i * 4 + 0] = data.pixels[i * 3 + 0];
      img.data[i * 4 + 1] = data.pixels[i * 3 + 1];
      img.data[i * 4 + 2] = data.pixels[i * 3 + 2];
      img.data[i * 4 + 3] = 255;
    }
    previewCtx.putImageData(img, 0, 0);
  } catch (e) {
    knownPreviewVideo = null; // retry next time this video is selected
  }
}

function applyState(state) {
  populateVideoOptions(state.videos);
  updatePreview(state.video);
  if (document.activeElement !== els.video)
    els.video.value = state.video;

  els.started.checked = state.started;
  els.startedLabel.textContent = state.started ? "On" : "Off";

  els.paused.checked = state.paused;
  els.pausedLabel.textContent = state.paused ? "Paused" : "Playing";

  setSliderValue(els.hue, "hue_val", state.hue);
  setSliderValue(els.sat, "sat_val", state.sat);
  setSliderValue(els.val, "val_val", state.val);
  setSliderValue(els.fps, "fps_val", state.fps);

  if (!pickerDragging) {
    const hsv = rgbToHsv(state.solid.r, state.solid.g, state.solid.b);
    pickerHue = hsv.h;
    pickerSat = hsv.s;
    pickerVal = hsv.v;
    currentSolidRgb = { r: state.solid.r, g: state.solid.g, b: state.solid.b };
    renderPickerUI();
  }
  setSliderValue(els.solid_w, "solid_w_val", state.solid.w);
  updateSwatch();

  const isSolid = state.video === "solid";
  els.solidControls.classList.toggle("hidden", !isSolid);
  els.videoControls.classList.toggle("hidden", isSolid);
}

function renderPresets(presets) {
  els.presets.innerHTML = "";
  for (const preset of presets) {
    const chip = document.createElement("div");
    chip.className = "preset-chip" + (preset.default ? " preset-chip--default" : "");

    const label = document.createElement("span");
    label.textContent = preset.name;
    label.addEventListener("click", () => applyPreset(preset.name));
    chip.appendChild(label);

    const star = document.createElement("button");
    star.type = "button";
    star.className = "preset-chip__star";
    star.textContent = preset.default ? "★" : "☆";
    star.title = preset.default
      ? "Loads on startup -- click to unset"
      : "Set as the preset that loads on startup";
    star.addEventListener("click", (e) => {
      e.stopPropagation();
      setDefaultPreset(preset.default ? "" : preset.name);
    });
    chip.appendChild(star);

    const del = document.createElement("button");
    del.type = "button";
    del.className = "preset-chip__delete";
    del.textContent = "×";
    del.title = `Delete "${preset.name}"`;
    del.addEventListener("click", (e) => {
      e.stopPropagation();
      deletePreset(preset.name);
    });
    chip.appendChild(del);

    els.presets.appendChild(chip);
  }
}

async function refreshPresets() {
  try {
    const res = await fetch("/api/presets");
    renderPresets(await res.json());
  } catch (e) {
    // leave the last-known list in place
  }
}

async function applyPreset(name) {
  try {
    const res = await fetch("/api/presets/apply", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ name }).toString(),
    });
    applyState(await res.json());
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
}

async function savePresetFromCurrentState() {
  const name = els.presetName.value.trim();
  if (!name)
    return;
  try {
    const res = await fetch("/api/presets", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ name }).toString(),
    });
    renderPresets(await res.json());
    els.presetName.value = "";
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
}

async function deletePreset(name) {
  try {
    const res = await fetch("/api/presets/delete", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ name }).toString(),
    });
    renderPresets(await res.json());
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
}

async function setDefaultPreset(name) {
  try {
    const res = await fetch("/api/presets/default", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ name }).toString(),
    });
    renderPresets(await res.json());
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
}

async function refresh() {
  try {
    const res = await fetch(API);
    applyState(await res.json());
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
  await refreshPresets();
}

async function send(fields) {
  try {
    const body = new URLSearchParams(fields).toString();
    const res = await fetch(API, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    applyState(await res.json());
    setStatus(true);
  } catch (e) {
    setStatus(false);
  }
}

function debouncedSend(fields) {
  clearTimeout(sendTimer);
  sendTimer = setTimeout(() => send(fields), SEND_DEBOUNCE_MS);
}

function bindSlider(input, labelId, field) {
  input.addEventListener("input", () => {
    document.getElementById(labelId).textContent = input.value;
    debouncedSend({ [field]: input.value });
  });
}

els.video.addEventListener("change", () => send({ video: els.video.value }));
els.started.addEventListener("change", () => send({ started: els.started.checked ? "1" : "0" }));
els.paused.addEventListener("change", () => send({ paused: els.paused.checked ? "1" : "0" }));

bindSlider(els.hue, "hue_val", "hue");
bindSlider(els.sat, "sat_val", "sat");
bindSlider(els.val, "val_val", "val");
bindSlider(els.fps, "fps_val", "fps");

els.solid_w.addEventListener("input", () => {
  document.getElementById("solid_w_val").textContent = els.solid_w.value;
  updateSwatch();
  sendSolid();
});

els.savePreset.addEventListener("click", savePresetFromCurrentState);
els.presetName.addEventListener("keydown", (e) => {
  if (e.key === "Enter")
    savePresetFromCurrentState();
});

refresh();
setInterval(refresh, POLL_INTERVAL_MS);
