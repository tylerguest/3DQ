// What's behind the sphere: graphite, lit softly behind the shape, darker toward
// the corners, with a soft shadow under it. The haze puts lows, mids and highs at
// the heights they have on the sphere. Matches the shader in Source/SpriteRenderer.cpp.
//
// A canvas of its own under the particles, so it's the same whichever way they draw.

export interface Backdrop {
  // All in device pixels, y down.
  centreX: number; lightY: number; // the middle of the shape as it looks
  sphereY: number; radius: number; // the sphere's own centre and size
  shadowY: number; shadowHalfWidth: number;
  haze: number[]; // 0..1 lows, mids, highs
}

type RGB = readonly [number, number, number];

const GAUSSIAN_REACH = 2.6; // in sigmas; past this it's under 1/1000
const GAUSSIAN_STOPS = Array.from({ length: 25 }, (_, i) => {
  const t = i / 24;
  return { t, value: Math.exp(-(t * GAUSSIAN_REACH) * (t * GAUSSIAN_REACH)) };
});
const HAZE_COLOURS: RGB[] = [[89, 150, 209], [120, 194, 140], [235, 222, 171]];
const HAZE_AMOUNT = 0.075;
const HAZE_HEIGHT = [0.7, 0, -0.7]; // in sphere radii, y down

const smoothstep = (a: number, b: number, x: number) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};

export class BackdropLayer {
  private readonly ctx: CanvasRenderingContext2D;
  private readonly grain: CanvasPattern;
  private drawnKey = "";

  constructor(private readonly canvas: HTMLCanvasElement) {
    this.ctx = canvas.getContext("2d", { alpha: false })!;

    // A step of 1/255 is visible on a dark gradient; added noise hides it.
    const tile = document.createElement("canvas");
    tile.width = tile.height = 256;
    const tileCtx = tile.getContext("2d")!;
    const image = tileCtx.createImageData(256, 256);
    for (let i = 0; i < image.data.length; i += 4) {
      const v = Math.floor(Math.random() * 3);
      image.data[i] = image.data[i + 1] = image.data[i + 2] = v;
      image.data[i + 3] = 255;
    }
    tileCtx.putImageData(image, 0, 0);
    this.grain = this.ctx.createPattern(tile, "repeat")!;
  }

  private gaussian(x: number, y: number, sigmaX: number, sigmaY: number, rgb: RGB, amount: number) {
    const ctx = this.ctx;
    ctx.setTransform(sigmaX, 0, 0, sigmaY, x, y);
    const gradient = ctx.createRadialGradient(0, 0, 0, 0, 0, GAUSSIAN_REACH);
    for (const stop of GAUSSIAN_STOPS) gradient.addColorStop(stop.t, `rgba(${rgb[0]},${rgb[1]},${rgb[2]},${amount * stop.value})`);
    ctx.fillStyle = gradient;
    ctx.fillRect(-GAUSSIAN_REACH, -GAUSSIAN_REACH, 2 * GAUSSIAN_REACH, 2 * GAUSSIAN_REACH);
  }

  draw(width: number, height: number, b: Backdrop) {
    // Costs a few full-window fills, so only when something visibly moved.
    const key = [width, height, b.centreX, b.lightY, b.sphereY, b.radius, b.shadowY, b.shadowHalfWidth, ...b.haze.map((h) => h * 100)]
      .map(Math.round).join();
    if (key === this.drawnKey) return;
    this.drawnKey = key;

    if (this.canvas.width !== width) this.canvas.width = width;
    if (this.canvas.height !== height) this.canvas.height = height;

    const ctx = this.ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalCompositeOperation = "source-over";
    ctx.fillStyle = "rgb(16,16,15)"; // graphite, less the grain's average
    ctx.fillRect(0, 0, width, height);

    ctx.globalCompositeOperation = "lighter";
    this.gaussian(b.centreX, b.lightY, 1.7 * b.radius, 1.7 * b.radius, [255, 250, 235], 0.042);
    b.haze.forEach((level, i) => {
      if (level > 0.005) this.gaussian(b.centreX, b.sphereY + HAZE_HEIGHT[i] * b.radius, 1.5 * b.radius, 0.85 * b.radius, HAZE_COLOURS[i], HAZE_AMOUNT * level);
    });

    ctx.globalCompositeOperation = "source-over";
    ctx.setTransform(width / 2, 0, 0, height / 2, width / 2, height / 2);
    const corners = ctx.createRadialGradient(0, 0, 0, 0, 0, 1.45);
    for (let i = 0; i <= 24; ++i) corners.addColorStop(i / 24, `rgba(0,0,0,${0.3 * smoothstep(0.75, 1.45, (i / 24) * 1.45)})`);
    ctx.fillStyle = corners;
    ctx.fillRect(-1, -1, 2, 2);

    this.gaussian(b.centreX, b.shadowY, Math.max(b.shadowHalfWidth, 1), 0.13 * b.radius, [0, 0, 0], 0.65);

    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalCompositeOperation = "lighter";
    ctx.fillStyle = this.grain;
    ctx.fillRect(0, 0, width, height);
  }
}
