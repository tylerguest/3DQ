import { useCallback, useEffect, useState } from "react";
import Sphere from "./Sphere";
import PreviewBar from "./PreviewBar";
import { Band } from "./bands";
import { fetchBands, getPluginDynamics, getPluginSpectrum, inPlugin, onHostBandsChanged, sendBands } from "./pluginBridge";
import { peekPreview } from "./audioPreview";

const getSpectrum = inPlugin ? getPluginSpectrum : () => peekPreview()?.readSpectrum() ?? null;
const getDynamics = inPlugin ? getPluginDynamics : () => peekPreview()?.readDynamics() ?? null;

export default function App() {
  const [bands, setBands] = useState<Band[]>([]);
  const [externalBands, setExternalBands] = useState<{ bands: Band[]; serial: number }>();

  // Inside the plugin: load the saved bands, and follow host-side changes.
  useEffect(() => {
    if (!inPlugin) return;
    let serial = 0;
    const apply = (bs: Band[]) => {
      setBands(bs);
      setExternalBands({ bands: bs, serial: ++serial });
    };
    void fetchBands().then(apply);
    return onHostBandsChanged(apply);
  }, []);

  const onBandsChange = useCallback((bs: Band[]) => {
    setBands(bs);
    if (inPlugin) sendBands(bs);
  }, []);

  return (
    <div className="app">
      <header className="header">
        <span className="title">3DQ</span>
      </header>
      <Sphere onBandsChange={onBandsChange} externalBands={externalBands} getSpectrum={getSpectrum} getDynamics={getDynamics} />
      {!inPlugin && <PreviewBar bands={bands} />}
      {process.env.NODE_ENV !== "production" && new URLSearchParams(location.search).has("stats") && <div id="dev-stats" className="dev-stats" />}
    </div>
  );
}
