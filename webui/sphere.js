// A stand-in sphere behind the controls: particles with frequency up and
// stereo around, pushed out or in by the bands, and a node per band to pick
// it. Drag to rotate. The plugin's real sphere stays native; this is only so
// the controls have something to sit over while they are designed.
"use strict";

(() => {
  const canvas = document.getElementById("sphere");
  const ctx = canvas.getContext("2d");
  const count = 5200;
  const stops = [[107, 91, 214], [70, 128, 230], [64, 184, 168], [120, 206, 92], [231, 211, 74]];
  const boost = [255, 138, 42], cut = [229, 72, 77];

  const freqColour = (t) => {
    const x = clamp(t, 0, 0.9999) * (stops.length - 1), i = Math.floor(x), f = x - i;
    return stops[i].map((c, k) => c + (stops[i + 1][k] - c) * f);
  };
  const latToFreq = (lat) => EQ.minFreq * Math.pow(EQ.maxFreq / EQ.minFreq, (lat / (Math.PI / 2) + 1) / 2);
  const freqToLat = (f) => (toLog(f, EQ.minFreq, EQ.maxFreq) * 2 - 1) * (Math.PI / 2);
  const shape = (db) => Math.sign(db) * Math.pow(Math.min(1, Math.abs(db) / EQ.maxGain), 0.7);
  const displacement = (db) => shape(db) * (db > 0 ? 0.55 : 0.82);

  const points = [];
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < count; i++) {
    const y = 1 - (2 * (i + 0.5)) / count, ring = Math.sqrt(1 - y * y), a = i * golden;
    const x = ring * Math.sin(a), z = ring * Math.cos(a);
    points.push({ x, y, z, lat: Math.asin(y), lon: Math.atan2(x, z), freq: latToFreq(Math.asin(y)),
                  colour: freqColour((y + 1) / 2), size: 0.7 + Math.random() * 0.8, offset: 0 });
  }

  let yaw = 0.5, pitch = -0.28, width = 0, height = 0, dpr = 1;
  let dragging = null, idleSince = performance.now();
  let nodes = []; // screen positions of band nodes this frame

  const resize = () => {
    const r = canvas.getBoundingClientRect();
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    width = r.width; height = r.height;
    canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
  };
  new ResizeObserver(resize).observe(canvas);
  resize();

  // How much a band reaches a point: a mono band only near its stereo position.
  const overlap = (b, lon) => (b.stereo ? 1 : (1 + Math.cos(lon - b.pos * Math.PI / 180)) / 2);

  const surface = (p, bands) => {
    let d = 0;
    for (const b of bands) {
      if (b.muted) continue;
      if (isCut(b) || isShelf(b)) { d += displacement(response.of(b, p.freq)) * overlap(b, p.lon); continue; }
      // A bell: its response along frequency, round in every direction.
      const blat = freqToLat(b.freq), blon = b.pos * Math.PI / 180;
      const dot = b.stereo ? Math.cos(p.lat - blat)
        : Math.cos(p.lat) * Math.cos(blat) * Math.cos(p.lon - blon) + Math.sin(p.lat) * Math.sin(blat);
      const angle = Math.acos(clamp(dot, -1, 1));
      d += displacement(response.peak(b.gain, Math.min(b.q, 8), Math.exp(angle * Math.log(1000) / Math.PI)));
    }
    return d;
  };

  const project = (x, y, z, r) => {
    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    const x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
    const vy = y * cp - z1 * sp, vz = y * sp + z1 * cp;
    const radius = Math.min(width, height) * 0.3;
    const persp = 3.2 / (3.2 - vz * r);
    return { sx: width / 2 + x1 * r * radius * persp, sy: height * 0.42 - vy * r * radius * persp, z: vz, persp };
  };

  const draw = (time) => {
    if (!dragging && time - idleSince > 1500) yaw += 0.0012;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.globalCompositeOperation = "lighter";

    const bands = store.bands;
    for (const p of points) {
      const target = surface(p, bands);
      p.offset += (target - p.offset) * 0.18;
      const s = project(p.x, p.y, p.z, 1 + p.offset);
      const front = clamp((s.z + 0.7) / 1.4, 0, 1);
      const tint = clamp(Math.abs(p.offset) / 0.25, 0, 1);
      const to = p.offset > 0 ? boost : cut;
      const c = p.colour.map((v, k) => v + (to[k] - v) * tint * 0.9);
      ctx.fillStyle = `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${(0.1 + 0.6 * front) * 0.9})`;
      const size = p.size * s.persp * (0.6 + 0.6 * front) * Math.min(width, height) / 900;
      ctx.fillRect(s.sx - size, s.sy - size, size * 2, size * 2);
    }

    ctx.globalCompositeOperation = "source-over";
    nodes = [];
    for (const b of bands) {
      const lat = freqToLat(b.freq), lon = b.stereo ? -yaw : b.pos * Math.PI / 180;
      const x = Math.cos(lat) * Math.sin(lon), y = Math.sin(lat), z = Math.cos(lat) * Math.cos(lon);
      const s = project(x, y, z, 1 + surface({ lat, lon, freq: b.freq }, bands));
      const selected = b.id === store.selected;
      const colour = b.muted ? "#6b6862" : isCut(b) || b.gain < 0 ? EQ.cut : EQ.boost;
      const r = selected ? 8 : 6;
      ctx.globalAlpha = s.z < -0.2 ? 0.35 : 1;
      ctx.beginPath(); ctx.arc(s.sx, s.sy, r, 0, Math.PI * 2);
      ctx.fillStyle = "#151513"; ctx.fill();
      ctx.lineWidth = selected ? 2.5 : 1.5; ctx.strokeStyle = colour; ctx.stroke();
      ctx.fillStyle = "#e8e4da"; ctx.font = "600 9px Inter, system-ui, sans-serif";
      ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.fillText(String(b.id + 1), s.sx, s.sy + 0.5);
      ctx.globalAlpha = 1;
      nodes.push({ id: b.id, x: s.sx, y: s.sy, z: s.z });
    }
    requestAnimationFrame(draw);
  };
  requestAnimationFrame(draw);

  canvas.addEventListener("pointerdown", (e) => {
    const r = canvas.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
    const hit = nodes.filter((n) => Math.hypot(n.x - x, n.y - y) < 12).sort((a, b) => b.z - a.z)[0];
    if (hit) { store.selected = hit.id; store.panelHidden = false; store.changed(); return; }
    dragging = { x: e.clientX, y: e.clientY, moved: false };
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    const dx = e.clientX - dragging.x, dy = e.clientY - dragging.y;
    if (Math.abs(dx) + Math.abs(dy) > 2) dragging.moved = true;
    yaw += dx * 0.006; pitch = clamp(pitch + dy * 0.006, -1.3, 1.3);
    dragging.x = e.clientX; dragging.y = e.clientY;
  });
  canvas.addEventListener("pointerup", () => {
    // A click on empty space puts the panel away, as in the plugin.
    if (dragging && !dragging.moved) { store.panelHidden = true; store.changed(); }
    dragging = null; idleSince = performance.now();
  });
})();
