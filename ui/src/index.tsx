import "./index.css";
import { createRoot } from "react-dom/client";
import App from "./App";
import { EqChain, peekPreview, stereoAxis } from "./audioPreview";

// Dev-only hook for checking the EQ maths from the browser console.
if (process.env.NODE_ENV !== "production") (window as unknown as Record<string, unknown>).__3dq = { EqChain, stereoAxis, peekPreview };

createRoot(document.getElementById("root")!).render(<App />);
