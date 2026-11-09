// The small window for naming a preset, confirming a replace, or reporting a
// failure. Looks like the plugin's dialog in PluginEditor.cpp.

let openDialog: HTMLElement | null = null;

export function closePresetDialog() {
  openDialog?.remove();
  openDialog = null;
}

export function showPresetDialog(options: { title: string; message?: string; initialText?: string; confirmLabel: string; onConfirm: (text: string) => void }) {
  closePresetDialog();

  const scrim = document.createElement("div");
  scrim.className = "preset-dialog-scrim";
  const box = document.createElement("div");
  box.className = "preset-dialog";
  scrim.appendChild(box);

  const heading = document.createElement("div");
  heading.className = "preset-dialog-title";
  heading.textContent = options.title;
  box.appendChild(heading);

  if (options.message) {
    const message = document.createElement("div");
    message.className = "preset-dialog-message";
    message.textContent = options.message;
    box.appendChild(message);
  }

  let input: HTMLInputElement | null = null;
  if (options.initialText !== undefined) {
    input = document.createElement("input");
    input.value = options.initialText;
    box.appendChild(input);
  }

  const confirm = () => {
    const text = input?.value ?? "";
    closePresetDialog();
    options.onConfirm(text);
  };

  const buttons = document.createElement("div");
  buttons.className = "preset-dialog-buttons";
  const button = (label: string, onClick: () => void) => {
    const b = document.createElement("button");
    b.textContent = label;
    b.onclick = onClick;
    buttons.appendChild(b);
    return b;
  };
  const confirmButton = button(options.confirmLabel, confirm);
  button("Cancel", closePresetDialog);
  box.appendChild(buttons);

  scrim.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); confirm(); }
    if (e.key === "Escape") { e.preventDefault(); closePresetDialog(); }
    e.stopPropagation();
  });

  document.body.appendChild(scrim);
  openDialog = scrim;

  if (input) { input.focus(); input.select(); }
  else confirmButton.focus();
}
