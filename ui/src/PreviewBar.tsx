import { useEffect, useRef, useState } from "react";
import { Band } from "./bands";
import { AudioPreview, getPreview, makePinkNoise } from "./audioPreview";
import { renderDemoLoop } from "./demoLoop";

// Browser-only transport for hearing the EQ: play the built-in demo loop, pink
// noise, or a dropped-in audio file through a Web Audio copy of the EQ, with a
// bypass for A/B.

interface Props {
  bands: Band[];
}

type SourceKind = "demo" | "pink" | "file";

export default function PreviewBar({ bands }: Props) {
  const previewRef = useRef<AudioPreview | null>(null);
  const buffersRef = useRef<Partial<Record<SourceKind, AudioBuffer>>>({});
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [source, setSource] = useState<SourceKind>("demo");
  const [fileName, setFileName] = useState<string | null>(null);
  const [playing, setPlaying] = useState(false);
  const [bypassed, setBypassed] = useState(false);
  const [dragOver, setDragOver] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const preview = () => {
    if (!previewRef.current) previewRef.current = getPreview();
    return previewRef.current;
  };

  // Latest bands, for async paths (play starts after the demo loop renders,
  // and the bands may have changed meanwhile).
  const bandsRef = useRef(bands);
  bandsRef.current = bands;

  useEffect(() => {
    previewRef.current?.eq.setBands(bands);
  }, [bands]);

  useEffect(() => {
    previewRef.current?.eq.setBypassed(bypassed);
  }, [bypassed]);

  useEffect(() => () => previewRef.current?.stop(), []);

  const bufferFor = async (kind: SourceKind): Promise<AudioBuffer | null> => {
    const cached = buffersRef.current[kind];
    if (cached) return cached;
    const ctx = preview().ctx;
    let buffer: AudioBuffer | null = null;
    if (kind === "demo") {
      setBusy(true);
      try {
        buffer = await renderDemoLoop(ctx.sampleRate);
      } finally {
        setBusy(false);
      }
    } else if (kind === "pink") {
      buffer = makePinkNoise(ctx);
    }
    if (buffer) buffersRef.current[kind] = buffer;
    return buffer;
  };

  const playSource = async (kind: SourceKind) => {
    const buffer = await bufferFor(kind);
    if (!buffer) return;
    const p = preview();
    p.eq.setBands(bandsRef.current);
    p.eq.setBypassed(bypassed);
    await p.play(buffer);
    setSource(kind);
    setPlaying(true);
  };

  const togglePlay = async () => {
    const p = preview();
    if (p.playing) {
      p.stop();
      setPlaying(false);
      return;
    }
    await playSource(source);
  };

  const chooseSource = async (kind: SourceKind) => {
    if (kind === "file" && !buffersRef.current.file) {
      fileInputRef.current?.click();
      return;
    }
    setSource(kind);
    if (preview().playing) await playSource(kind);
  };

  const loadFile = async (file: File) => {
    try {
      setError(null);
      buffersRef.current.file = await preview().decode(file);
      setFileName(file.name);
      await playSource("file");
    } catch {
      setError(`Couldn't decode ${file.name}`);
    }
  };

  // Drop an audio file anywhere on the page.
  useEffect(() => {
    const over = (e: DragEvent) => { e.preventDefault(); setDragOver(true); };
    const leave = () => setDragOver(false);
    const drop = (e: DragEvent) => {
      e.preventDefault();
      setDragOver(false);
      const file = e.dataTransfer?.files[0];
      if (file) void loadFile(file);
    };
    window.addEventListener("dragover", over);
    window.addEventListener("dragleave", leave);
    window.addEventListener("drop", drop);
    return () => {
      window.removeEventListener("dragover", over);
      window.removeEventListener("dragleave", leave);
      window.removeEventListener("drop", drop);
    };
  });

  const sourceButton = (kind: SourceKind, label: string, title?: string) => (
    <button className={source === kind ? "selected" : ""} title={title} onClick={() => void chooseSource(kind)}>
      {label}
    </button>
  );

  return (
    <>
      <div className="preview-bar">
        <span className="preview-tag">Browser preview</span>
        <button className={playing ? "active" : ""} disabled={busy} onClick={() => void togglePlay()}>
          {busy ? "Rendering…" : playing ? "■ Stop" : "▶ Play"}
        </button>
        <div className="source-group">
          {sourceButton("demo", "Demo loop")}
          {sourceButton("pink", "Pink noise")}
          {sourceButton("file", fileName ?? "Audio file…", "Pick a file, or drop one on the page")}
        </div>
        {fileName && (
          <button title="Load a different file" onClick={() => fileInputRef.current?.click()}>…</button>
        )}
        <button className={bypassed ? "active" : ""} onClick={() => setBypassed((b) => !b)}>
          Bypass
        </button>
        {error && <span className="preview-error">{error}</span>}
        <input
          ref={fileInputRef}
          type="file"
          accept="audio/*"
          hidden
          onChange={(e) => {
            const file = e.target.files?.[0];
            if (file) void loadFile(file);
            e.target.value = "";
          }}
        />
      </div>
      {dragOver && <div className="drop-overlay">Drop audio file to play it through the EQ</div>}
    </>
  );
}
