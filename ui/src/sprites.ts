// Sprite sinks: where the sphere's glowing dots get drawn.
//
// The sphere computes every dot's screen position, size, colour and alpha on
// the CPU (so picking and the 2D overlay share one set of maths), then hands
// the dots to a sink:
//   - GlSpriteSink: three.js / WebGL. All dots go to the GPU as one point
//     cloud and draw in a single call. Used whenever WebGL is available.
//   - CanvasSpriteSink: Canvas 2D drawImage per dot. The fallback for web
//     views without WebGL (e.g. GPU-less environments).
// Both draw additively ("lighter") with the same glow profiles, so they look
// the same.

import * as THREE from "three";

// Two glow shapes, matching the original Canvas 2D sprites:
//   PARTICLE: white-hot centre -> colour -> faint halo
//   TINT:     colour centre -> soft falloff (vortex rings, Mid seam)
export const SpriteProfile = { Particle: 0, Tint: 1 } as const;
export type SpriteProfile = (typeof SpriteProfile)[keyof typeof SpriteProfile];

export interface SpriteStyle {
  rgb: [number, number, number]; // 0..255
  profile: SpriteProfile;
  image: HTMLCanvasElement; // pre-rendered sprite for the Canvas 2D fallback
}

export interface SpriteSink {
  // width / height: drawing-buffer size in device pixels.
  begin(width: number, height: number): void;
  // x, y: sprite centre in device pixels; size: full width in device pixels.
  add(style: number, x: number, y: number, size: number, alpha: number): void;
  end(): void;
}

export class CanvasSpriteSink implements SpriteSink {
  constructor(private readonly ctx: CanvasRenderingContext2D, private readonly styles: SpriteStyle[]) {}

  begin() {
    this.ctx.globalCompositeOperation = "lighter";
  }

  add(style: number, x: number, y: number, size: number, alpha: number) {
    if (alpha <= 0.003) return;
    this.ctx.globalAlpha = alpha > 1 ? 1 : alpha;
    this.ctx.drawImage(this.styles[style].image, x - size / 2, y - size / 2, size, size);
  }

  end() {
    this.ctx.globalAlpha = 1;
  }
}

const vertexShader = /* glsl */ `
  attribute float aSize;
  attribute vec3 aColour;
  attribute float aAlpha;
  attribute float aProfile;
  uniform vec2 uViewport;
  varying vec3 vColour;
  varying float vAlpha;
  varying float vProfile;
  varying float vSize;

  void main() {
    vSize = aSize;
    vColour = aColour;
    vAlpha = aAlpha;
    vProfile = aProfile;
    // position.xy is already in device pixels (y down).
    gl_Position = vec4(position.x / uViewport.x * 2.0 - 1.0, 1.0 - position.y / uViewport.y * 2.0, 0.0, 1.0);
    gl_PointSize = aSize;
  }
`;

// Glow profiles (t = 0 centre .. 1 edge), output as premultiplied colour for
// additive ONE/ONE blending == "lighter".
const fragmentShader = /* glsl */ `
  varying vec3 vColour;
  varying float vAlpha;
  varying float vProfile;
  varying float vSize;

  void main() {
    float t = length(gl_PointCoord - 0.5) * 2.0;
    if (t >= 1.0) discard;

    vec3 rgb;
    float a;

    if (vProfile < 0.5) {
      // Particle: a crisp, anti-aliased bright core (about one pixel of
      // edge softness whatever the sprite size) inside a faint halo.
      float px = 2.0 / max(vSize, 1.0); // one pixel in t units
      float coreR = 0.3;
      float core = 1.0 - smoothstep(coreR - px, coreR + px, t);
      float halo = 0.22 * pow(1.0 - t, 2.0);
      rgb = mix(vColour, vec3(1.0), 0.35 * (1.0 - smoothstep(0.0, coreR, t)));
      a = max(core, halo);
    } else {
      // Tint: 0 colour, 0.4 colour @ 0.35, 1 colour @ 0
      rgb = vColour;
      a = t < 0.4 ? mix(1.0, 0.35, t / 0.4) : mix(0.35, 0.0, (t - 0.4) / 0.6);
    }

    a *= vAlpha;
    gl_FragColor = vec4(rgb * a, a);
  }
`;

export class GlSpriteSink implements SpriteSink {
  private readonly renderer: THREE.WebGLRenderer;
  private readonly scene = new THREE.Scene();
  private readonly camera = new THREE.Camera(); // unused: the shader works in pixels
  private readonly geometry = new THREE.BufferGeometry();
  private readonly material: THREE.ShaderMaterial;
  private capacity = 0;
  private count = 0;
  private position!: THREE.BufferAttribute;
  private size!: THREE.BufferAttribute;
  private colour!: THREE.BufferAttribute;
  private alpha!: THREE.BufferAttribute;
  private profile!: THREE.BufferAttribute;
  private width = 1;
  private height = 1;
  private contextLost = false;

  // Returns null when WebGL isn't available.
  static create(canvas: HTMLCanvasElement, styles: SpriteStyle[]): GlSpriteSink | null {
    try {
      // three.js needs WebGL 2. Probe on a scratch canvas so a failed probe
      // doesn't tie up the real one.
      if (!document.createElement("canvas").getContext("webgl2")) return null;
      return new GlSpriteSink(canvas, styles);
    } catch {
      return null;
    }
  }

  private constructor(private readonly canvas: HTMLCanvasElement, private readonly styles: SpriteStyle[]) {
    this.renderer = new THREE.WebGLRenderer({
      canvas, alpha: true, antialias: false, premultipliedAlpha: true, powerPreference: "high-performance",
      // The plugin's web view paints on its own schedule rather than right
      // after each frame; without this it can paint an already-cleared buffer.
      preserveDrawingBuffer: true,
    });
    this.renderer.setPixelRatio(1); // we size the drawing buffer in device pixels ourselves
    // Opaque graphite, so glow adds to the background exactly as it does in the plugin.
    this.renderer.setClearColor(0x111110, 1);

    this.material = new THREE.ShaderMaterial({
      vertexShader,
      fragmentShader,
      uniforms: { uViewport: { value: new THREE.Vector2(1, 1) } },
      transparent: true,
      depthTest: false,
      depthWrite: false,
      blending: THREE.CustomBlending,
      blendEquation: THREE.AddEquation,
      blendSrc: THREE.OneFactor,
      blendDst: THREE.OneFactor,
      blendSrcAlpha: THREE.OneFactor,
      blendDstAlpha: THREE.OneFactor,
    });

    const points = new THREE.Points(this.geometry, this.material);
    points.frustumCulled = false;
    this.scene.add(points);
    this.reserve(16384);

    canvas.addEventListener("webglcontextlost", (e) => { e.preventDefault(); this.contextLost = true; });
    canvas.addEventListener("webglcontextrestored", () => { this.contextLost = false; });
  }

  // While the GPU context is lost (driver reset, sleep, ...) callers should
  // fall back to the Canvas 2D sink.
  get available() {
    return !this.contextLost;
  }

  private reserve(capacity: number) {
    this.capacity = capacity;
    this.position = new THREE.BufferAttribute(new Float32Array(capacity * 3), 3);
    this.size = new THREE.BufferAttribute(new Float32Array(capacity), 1);
    this.colour = new THREE.BufferAttribute(new Float32Array(capacity * 3), 3);
    this.alpha = new THREE.BufferAttribute(new Float32Array(capacity), 1);
    this.profile = new THREE.BufferAttribute(new Float32Array(capacity), 1);
    for (const a of [this.position, this.size, this.colour, this.alpha, this.profile]) a.setUsage(THREE.DynamicDrawUsage);
    this.geometry.setAttribute("position", this.position);
    this.geometry.setAttribute("aSize", this.size);
    this.geometry.setAttribute("aColour", this.colour);
    this.geometry.setAttribute("aAlpha", this.alpha);
    this.geometry.setAttribute("aProfile", this.profile);
  }

  begin(width: number, height: number) {
    if (width !== this.width || height !== this.height) {
      this.width = width;
      this.height = height;
      this.renderer.setSize(width, height, false);
      (this.material.uniforms.uViewport.value as THREE.Vector2).set(width, height);
    }
    this.count = 0;
  }

  add(style: number, x: number, y: number, size: number, alpha: number) {
    if (alpha <= 0.003) return;
    if (this.count >= this.capacity) {
      // Grow, keeping what's been added this frame.
      const old = [this.position, this.size, this.colour, this.alpha, this.profile].map((a) => a.array as Float32Array);
      this.reserve(this.capacity * 2);
      [this.position, this.size, this.colour, this.alpha, this.profile].forEach((a, i) => (a.array as Float32Array).set(old[i]));
    }

    const s = this.styles[style];
    const i = this.count++;
    const pos = this.position.array as Float32Array;
    const col = this.colour.array as Float32Array;
    pos[i * 3] = x;
    pos[i * 3 + 1] = y;
    pos[i * 3 + 2] = 0;
    col[i * 3] = s.rgb[0] / 255;
    col[i * 3 + 1] = s.rgb[1] / 255;
    col[i * 3 + 2] = s.rgb[2] / 255;
    (this.size.array as Float32Array)[i] = size;
    (this.alpha.array as Float32Array)[i] = alpha > 1 ? 1 : alpha;
    (this.profile.array as Float32Array)[i] = s.profile;
  }

  end() {
    for (const a of [this.position, this.size, this.colour, this.alpha, this.profile]) {
      a.clearUpdateRanges();
      a.addUpdateRange(0, this.count * a.itemSize);
      a.needsUpdate = true;
    }
    this.geometry.setDrawRange(0, this.count);
    this.renderer.render(this.scene, this.camera);
  }

  // Clears the GPU layer (used when switching to the fallback).
  clear() {
    this.renderer.clear();
  }

  dispose() {
    this.geometry.dispose();
    this.material.dispose();
    this.renderer.dispose();
  }
}
