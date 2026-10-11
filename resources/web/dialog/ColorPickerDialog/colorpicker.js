"use strict";
(() => {
  if (window.ORCA_COLOR_PICKER_LANGUAGE) document.documentElement.lang = window.ORCA_COLOR_PICKER_LANGUAGE;
  const T = (key) => window.ORCA_UI_STRINGS?.[key] || key;
  document.title = T("Color Picker");
  for (const element of document.querySelectorAll("[data-i18n]")) element.textContent = T(element.dataset.i18n);
  for (const attribute of ["aria-label", "title"])
    for (const element of document.querySelectorAll(`[data-i18n-${attribute}]`))
      element.setAttribute(attribute, T(element.getAttribute(`data-i18n-${attribute}`)));
  function paletteName(name) {
    const ral = name.match(/^(\d{4}) (.+)$/);
    return ral ? `${ral[1]} ${T(ral[2])}` : /^%\d+$/.test(name) ? name : T(name);
  }

  const FAVORITE_SLOTS              = 16;
  const SPECTRUM_FOLLOWS_SATURATION = true; // true: the spectrum is drawn at the current saturation, false: it always shows full saturation

  const model = window.ColorPickerModel;
  const palettes = window.ColorPickerPalettes;
  const byId = (id) => document.getElementById(id);
  const canvas = byId("spectrum"), context = canvas.getContext("2d");
  const paletteCombo = byId("paletteCombo"), palette = byId("palette"), preview = byId("colorPreview"), confirmBtn = byId("confirmBtn");
  const modeSwitch = byId("modeSwitch"), satSlider = byId("satSlider"), opacitySlider = byId("opacitySlider");
  const gradientBar = byId("gradientCtrlBar"), gradientThumbs = [...gradientBar.querySelectorAll(".gradient-thumb")];
  const originalPreview = byId("originalPreview"), palettePanel = byId("palettePanel"), paletteToggle = byId("paletteToggle");
  const rgbInputs = ["r", "g", "b"].map((name) => byId(name + "Value"));
  const hslInputs = ["h", "s", "l"].map((name) => byId(name + "Value"));
  const hexInput = byId("hexInput"), opacityInput = byId("oValue");
  const pageId = Date.now().toString(36) + Math.random().toString(36).slice(2);
  let state = model.createState(), favorites = [], initialized = false, favoritesWritable = true;
  // The selection the dialog opened with; the original swatch shows it and restores it on click.
  let originalSelection = model.exportSelection(state);
  let lastContentHeight = 0, lastContentWidth = 0, resizePending = false;
  // Last HSL shown or set. Kept while it still produces the current color: hue 360 and 0 are
  // both red, and greys, black and white have no hue or saturation in RGB.
  let hslMemory = [0, 0, 50];
  function displayHsl(color) {
    const rgb = model.hslToRgb(...hslMemory);
    if (!rgb.every((value, index) => value === color[index])) hslMemory = model.rgbToHsl(...color.slice(0, 3));
    return hslMemory;
  }
  // Copy of `values` with one entry replaced.
  const replaced = (values, index, value) => values.map((current, i) => i === index ? value : current);
  function setHslChannel(index, value) {
    hslMemory = replaced(displayHsl(model.activeColor(state)), index, value);
    model.setRgb(state, model.hslToRgb(...hslMemory));
  }
  const round1 = (value) => Math.round(value * 10) / 10;

  function emit(command, data = {}) {
    window.dispatchEvent(new CustomEvent("color-picker-message", {detail: {command, ...data}}));
    if (window.wx && typeof window.wx.postMessage === "function")
      window.wx.postMessage(JSON.stringify({command, page_id: pageId, ...data}));
  }

  function scheduleResize() {
    if (!initialized || resizePending) return;
    resizePending = true;
    requestAnimationFrame(() => {
      resizePending = false;
      const rect = document.body.getBoundingClientRect();
      const height = Math.min(900 , Math.max(300, Math.ceil(rect.height)));
      const width  = Math.min(1200, Math.max(300, Math.ceil(rect.width )));
      if (height !== lastContentHeight || width !== lastContentWidth) {
        lastContentHeight = height; lastContentWidth = width;
        emit("resize", {width, height});
      }
    });
  }
  new ResizeObserver(scheduleResize).observe(document.body);

  function setSwatch(element, selection) {
    element.style.setProperty("--swatch-color", model.selectionBackground(selection));
  }

  // `editing` is the control the user is typing in. Its text is left alone so the caret,
  // partial input like "05" and fine steps that round-trip through bytes aren't overwritten.
  function render(resetEditors = false, editing = null) {
    const color = model.activeColor(state), hsl = displayHsl(color);
    const show = (input, value) => {
      if (input === editing) return;
      if (resetEditors) input.setCustomValidity("");
      if (resetEditors || input.checkValidity()) input.value = value;
    };
    show(hexInput, model.toHex(color).slice(1, state.options.allow_alpha ? 9 : 7));
    rgbInputs.forEach((input, index) => show(input, color[index]));
    // Shown in whole numbers; decimals can still be typed, and hslMemory keeps exact values.
    hslInputs.forEach((input, index) => show(input, Math.round(hsl[index])));
    // Percentage is a display only until explicitly edited. Never round it back
    // into the alpha byte on endpoint switches, RGB edits, or confirmation.
    show(opacityInput, Math.round(color[3] / 255 * 100));
    if (opacitySlider !== editing) opacitySlider.value = color[3] / 255 * 100;
    const rgb = color.slice(0, 3).join(",");
    opacitySlider.style.setProperty("--track-gradient", `linear-gradient(to right, rgba(${rgb},0), rgb(${rgb}))`);
    opacitySlider.style.setProperty("--thumb-color", `rgba(${rgb},${color[3] / 255})`);
    if (satSlider !== editing) satSlider.value = hsl[1];
    satSlider.style.setProperty("--track-gradient", `linear-gradient(to right, hsl(${hsl[0]},0%,${hsl[2]}%), hsl(${hsl[0]},100%,${hsl[2]}%))`);
    satSlider.style.setProperty("--thumb-color", `hsl(${hsl[0]},${hsl[1]}%,${hsl[2]}%)`);
    modeSwitch.checked = state.mode === "gradient";
    const selection = model.exportSelection(state);
    // Kept in the layout (visibility only) so the grid doesn't shift between Solid and Gradient.
    gradientBar.classList.toggle("invisible", state.mode !== "gradient");
    if (state.mode === "gradient") {
      setSwatch(gradientBar.querySelector(".gradient-track"), selection);
      gradientThumbs.forEach((thumb, index) => {
        thumb.style.setProperty("--thumb-color", selection.colors[index]);
        thumb.setAttribute("aria-pressed", String(state.active === index));
      });
    }
    setSwatch(preview, selection);
    preview.title = `${T("Save current color")}\n${selection.colors.join(" → ")}`;
    if (SPECTRUM_FOLLOWS_SATURATION && hsl[1] !== spectrumSaturation) { spectrumSaturation = hsl[1]; paintSpectrum(); }
    updateIndicator(hsl);
    updateConfirm();
  }

  // Every editor the user can see; alpha only when this caller allows it.
  function editors() {
    const inputs = [hexInput, ...rgbInputs, ...hslInputs];
    if (state.options.allow_alpha) inputs.push(opacityInput);
    return inputs;
  }
  // OK stays disabled while any visible editor holds an invalid or incomplete value.
  function updateConfirm() {
    confirmBtn.disabled = !editors().every((input) => input.checkValidity());
  }

  function populatePalette() {
    palette.replaceChildren();
    for (const entry of palettes[paletteCombo.value] || []) {
      const selection = {type: entry.grad ? "gradient" : "solid", colors: entry.grad ? entry.grad.split("-") : [entry.hex]};
      if (!model.normalizeSelection(selection, state.options)) continue;
      const row    = document.createElement("button"); row.type = "button"; row.className = "color-item";
      const swatch = document.createElement("span"); swatch.className = "color-swatch";
      const name   = document.createElement("span"); name.className = "color-name"; name.textContent = paletteName(entry.name);
      setSwatch(swatch, model.normalizeSelection(selection, {allow_gradient: true, allow_alpha: true}));
      row.append(swatch, name); row.title = paletteName(entry.name);
      row.addEventListener("click", () => {
        // Solid palette colors edit the active endpoint while gradient mode is on.
        const changed = entry.grad ? model.applySelection(state, selection) : model.setHex(state, entry.hex);
        if (changed) render(true);
      });
      palette.appendChild(row);
    }
  }

  function drawFavorites() {
    const grid = byId("userColors"); grid.replaceChildren();
    const visible = favorites.filter((favorite) => state.options.allow_gradient || favorite.type === "solid");
    for (let index = 0; index < FAVORITE_SLOTS; ++index) {
      const swatch = document.createElement("button"); swatch.type = "button"; swatch.className = "saved-swatch";
      const favorite = visible[index];
      if (favorite) {
        setSwatch(swatch, favorite); swatch.title = favorite.colors.join(" → ");
        swatch.addEventListener("click", () => { if (model.applySelection(state, favorite)) render(true); });
      } else { swatch.classList.add("empty"); swatch.disabled = true; }
      grid.appendChild(swatch);
    }
  }

  function init(payload) {
    if (!payload || typeof payload !== "object" || (payload.options !== undefined && (!payload.options || typeof payload.options !== "object"))) return false;
    const next = model.createState(payload.options, payload.selection);
    if (!next) return false;
    byId("selectionNotice").hidden = payload.preserve_multi_color !== true;
    state = next; favorites = model.normalizeFavorites(payload.favorites || []);
    originalSelection = model.exportSelection(state);
    setSwatch(originalPreview, originalSelection);
    originalPreview.title = `${T("Original color")}\n${originalSelection.colors.join(" → ")}`;
    favoritesWritable = payload.favorites_writable !== false;
    preview.disabled = !favoritesWritable;
    byId("modeControls").classList.toggle("hidden", !state.options.allow_gradient);
    for (const option of paletteCombo.options) option.hidden = option.value === "gradient" && !state.options.allow_gradient;
    if (!state.options.allow_gradient && paletteCombo.value === "gradient") paletteCombo.value = "basic";
    byId("alphaControls").classList.toggle("hidden", !state.options.allow_alpha);
    hexInput.maxLength = state.options.allow_alpha ? 9 : 7;
    populatePalette(); drawFavorites(); render(true);
    initialized = true;
    byId("pickerEditors").disabled = false;
    return true;
  }

  function focusInput() {
    if (!initialized) return;
    // Restore initial keyboard focus without replacing an editor's in-progress input.
    const active = document.activeElement;
    if (!active || active === document.body || active === document.documentElement)
      hexInput.focus({preventScroll: true});
  }

  function handleMessage(payload) {
    if (!payload || typeof payload !== "object" || payload.page_id !== pageId) return false;
    if (payload.command === "init") {
      const options = payload.options;
      if (!options || typeof options.allow_gradient !== "boolean" || typeof options.allow_alpha !== "boolean" ||
          !model.normalizeSelection(payload.selection, options) || !validFavorites(payload.favorites) ||
          (payload.favorites_writable !== undefined && typeof payload.favorites_writable !== "boolean") ||
          (payload.preserve_multi_color !== undefined && typeof payload.preserve_multi_color !== "boolean")) return false;
      if (initialized) return true;
      if (!init(payload)) return false;
      emit("initialized");
      scheduleResize();
      return true;
    }
    if (payload.command === "favorites" && initialized && validFavorites(payload.favorites)) {
      favorites = model.normalizeFavorites(payload.favorites); drawFavorites(); return true;
    }
    return false;
  }

  function validFavorites(values) {
    return Array.isArray(values) && values.length <= 24 && values.every((value) => model.normalizeSelection(value, {allow_gradient: true, allow_alpha: true}));
  }

  // Saturation the spectrum is currently painted at (always 100 when the option is off).
  let spectrumSaturation = 100;
  function drawSpectrum() {
    const rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, Math.round(rect.width)); canvas.height = Math.max(1, Math.round(rect.height));
    paintSpectrum();
  }
  function paintSpectrum() {
    const hue = context.createLinearGradient(0, 0, canvas.width, 0);
    for (let i = 0; i <= 6; ++i) hue.addColorStop(i / 6, `hsl(${i * 60},100%,50%)`);
    context.fillStyle = hue; context.fillRect(0, 0, canvas.width, canvas.height);
    const shade = context.createLinearGradient(0, 0, 0, canvas.height);
    shade.addColorStop(0, "#FFFFFF"); shade.addColorStop(0.5, "#FFFFFF00"); shade.addColorStop(0.5, "#00000000"); shade.addColorStop(1, "#000000");
    context.fillStyle = shade; context.fillRect(0, 0, canvas.width, canvas.height);
    // HSL desaturation blends each color toward the grey of the same lightness, and on this
    // canvas that grey only depends on the row (white at the top, black at the bottom).
    if (spectrumSaturation < 100) {
      const grey = context.createLinearGradient(0, 0, 0, canvas.height);
      grey.addColorStop(0, "#FFFFFF"); grey.addColorStop(1, "#000000");
      context.globalAlpha = 1 - spectrumSaturation / 100;
      context.fillStyle = grey; context.fillRect(0, 0, canvas.width, canvas.height);
      context.globalAlpha = 1;
    }
  }

  const indicator = byId("colorIndicator");
  function updateIndicator(hsl) {
    indicator.style.left = hsl[0] / 360 * (canvas.width - 1) + "px";
    indicator.style.top = (1 - hsl[2] / 100) * (canvas.height - 1) + "px";
  }

  // Spectrum position maps straight to hue (x, 0..360) and lightness (y, 100..0); saturation is
  // the third dimension and comes from the slider below. hslMemory keeps these exact values,
  // including hue 360 at the right edge and the hue of the white and black corners.
  function sampleSpectrum(event) {
    const rect = canvas.getBoundingClientRect();
    const fraction = (client, start, size, pixels) =>
      Math.max(0, Math.min(pixels - 1, Math.floor((client - start) * pixels / size))) / Math.max(1, pixels - 1);
    const saturation = displayHsl(model.activeColor(state))[1];
    hslMemory = [round1(fraction(event.clientX, rect.left, rect.width, canvas.width) * 360), saturation,
                 round1((1 - fraction(event.clientY, rect.top, rect.height, canvas.height)) * 100)];
    model.setRgb(state, model.hslToRgb(...hslMemory));
    // A pick replaces every editor's value, so clear any invalid state left from typing.
    render(true);
  }

  const clamp = (control, value) => Math.min(Number(control.max), Math.max(Number(control.min), value));
  function bindValue(control, apply) {
    // Number fields are required, so an empty field is invalid; a range input always has a value.
    if (control.type === "number") control.required = true;
    const commit = (value) => { apply(value); render(false, control); };
    control.addEventListener("input", () => {
      control.setCustomValidity("");
      // Out-of-range numbers snap to the field's min/max instead of becoming invalid.
      if (control.value !== "") {
        const clamped = clamp(control, Number(control.value));
        if (clamped !== Number(control.value)) control.value = clamped;
      }
      if (control.checkValidity()) commit(Number(control.value));
    });
    control.addEventListener("wheel", (event) => {
      event.preventDefault();
      const step = control.type === "range" ? 5 : 1;
      control.value = clamp(control, (Number(control.value) || 0) + (event.deltaY > 0 ? -step : step));
      control.setCustomValidity("");
      commit(Number(control.value));
    }, {passive: false});
    // Normalize the text (e.g. "05" → "5") once the user leaves the field.
    control.addEventListener("blur", () => { if (control.checkValidity()) render(); });
  }

  rgbInputs.forEach((input, index) => bindValue(input, (value) => model.setRgb(state, replaced(model.activeColor(state).slice(0, 3), index, value))));
  hslInputs.forEach((input, index) => bindValue(input, (value) => setHslChannel(index, value)));
  bindValue(satSlider, (value) => setHslChannel(1, value));
  bindValue(opacityInput, (value) => model.setAlphaPercent(state, value));
  bindValue(opacitySlider, (value) => model.setAlphaPercent(state, value));
  // The opacity slider takes the gradient bar's row while the opacity field or the slider is in
  // use: it opens when the field is clicked or focused, and closes on a click or focus elsewhere.
  const thirdRow = byId("thirdRow"), alphaControls = byId("alphaControls");
  const inOpacityControls = (target) => target instanceof Element && target.closest("#alphaControls, #opacitySliderBar");
  const setOpacitySliderOpen = (open) => thirdRow.classList.toggle("opacity-open", open);
  for (const type of ["focusin", "click"]) alphaControls.addEventListener(type, () => setOpacitySliderOpen(true));
  for (const type of ["pointerdown", "focusin"])
    document.addEventListener(type, (event) => { if (!inOpacityControls(event.target)) setOpacitySliderOpen(false); });
  hexInput.addEventListener("input", () => {
    const text = hexInput.value;
    const valid = model.setHex(state, text.startsWith("#") ? text : "#" + text);
    hexInput.setCustomValidity(valid ? "" : T("Enter a complete hexadecimal color"));
    // render() leaves the typed text alone, so alpha isn't appended after the sixth digit.
    if (valid) render(false, hexInput);
  });
  hexInput.addEventListener("blur", () => { if (hexInput.checkValidity()) render(); });
  modeSwitch.addEventListener("change", () => { model.setMode(state, modeSwitch.checked ? "gradient" : "solid"); render(true); });
  // Thumbs can't be dragged: a click on a thumb selects it, a click on the track selects the
  // nearer end. Keyboard users tab to a thumb and press Enter or Space.
  gradientBar.addEventListener("click", (event) => {
    if (state.mode !== "gradient") return;
    // A thumb is read from its index: a keyboard-triggered click has no pointer position.
    const thumb = event.target.closest(".gradient-thumb"), rect = gradientBar.getBoundingClientRect();
    const index = thumb ? Number(thumb.dataset.index) : event.clientX < rect.left + rect.width / 2 ? 0 : 1;
    if (index !== state.active) { state.active = index; render(true); }
  });
  originalPreview.addEventListener("click", () => { if (model.applySelection(state, originalSelection)) render(true); });
  paletteToggle.addEventListener("click", () => paletteToggle.setAttribute("aria-expanded", palettePanel.classList.toggle("open")));
  paletteCombo.addEventListener("change", populatePalette);
  // Pointer capture ends by itself on pointerup/pointercancel, so it doubles as the drag state.
  canvas.addEventListener("pointerdown", (event) => { canvas.setPointerCapture(event.pointerId); sampleSpectrum(event); });
  canvas.addEventListener("pointermove", (event) => { if (canvas.hasPointerCapture(event.pointerId)) sampleSpectrum(event); });
  // The spectrum takes the panel's free height, which also changes with the notice, alpha and
  // gradient controls, so redraw on any size change rather than on window resize only.
  new ResizeObserver(() => { drawSpectrum(); updateIndicator(displayHsl(model.activeColor(state))); }).observe(canvas.parentElement);
  // Bubble phase on document runs after each editor's own handler has set its validity.
  document.addEventListener("input", updateConfirm);
  // Hex keeps its own message from its input handler; number fields get a translated one.
  function validateEditors() {
    const inputs = editors();
    for (const input of inputs.filter((input) => input !== hexInput)) {
      input.setCustomValidity("");
      const {valid, rangeUnderflow, rangeOverflow} = input.validity;
      if (!valid) input.setCustomValidity(T(rangeUnderflow || rangeOverflow ? "Value is out of range." : "Invalid input"));
    }
    return inputs.every((input) => input.reportValidity());
  }
  function confirm() {
    if (initialized && validateEditors()) emit("confirm", {selection: model.exportSelection(state)});
  }
  preview.addEventListener("click", () => {
    if (!initialized || !favoritesWritable || !validateEditors()) return;
    const selection = model.exportSelection(state);
    const key = JSON.stringify(selection);
    favorites = [selection, ...favorites.filter((favorite) => JSON.stringify(favorite) !== key)].slice(0, FAVORITE_SLOTS);
    drawFavorites(); emit("update_favorites", {favorites});
  });
  confirmBtn.addEventListener("click", confirm);
  byId("cancelBtn").addEventListener("click", () => emit("cancel"));
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") { event.preventDefault(); emit("cancel"); }
    else if (event.key === "Enter" && event.target instanceof HTMLInputElement && ["text", "number"].includes(event.target.type)) {
      event.preventDefault(); confirm();
    }
  });

  window.ColorPickerDialog = {
    init, handleMessage, focusInput, exportSelection: () => model.exportSelection(state),
    setFavorites: (values) => { favorites = model.normalizeFavorites(values); drawFavorites(); }
  };
  setSwatch(originalPreview, originalSelection);
  drawSpectrum(); populatePalette(); drawFavorites(); render(true); emit("ready");
})();
