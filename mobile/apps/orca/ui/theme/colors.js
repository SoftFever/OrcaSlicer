// The app's colour tokens, in CommonJS so tailwind.config.js and the TypeScript
// side (lib/theme.ts) read the same values. Tailwind's neutral scale carries the
// surfaces; these are the few colours that need a name of their own.

/** One accent for links, selected states, progress and the header tint. */
const accent = {
  50: '#effcf6',
  100: '#c6f7e2',
  200: '#8eedc7',
  300: '#65d6ad',
  400: '#3ebd93',
  500: '#27ab83',
  600: '#199473',
  700: '#147d64',
  800: '#0c6b58',
  900: '#014d40',
}

module.exports = {
  accent,
  /** TextInput placeholder, both schemes (neutral-400). */
  placeholder: '#9ca3af',
  /** Navigation header background and the grouped canvas behind cards. */
  header: { light: '#ffffff', dark: '#1c1c1e' },
  canvas: { light: '#f2f2f7', dark: '#000000' },
  /** Primary text, for the few native props that cannot take a class. */
  ink: { light: '#0a0a0a', dark: '#fafafa' },
  /** Default icon tint: neutral-500 / neutral-400. */
  iconMuted: { light: '#737373', dark: '#a3a3a3' },
  /** Spinner on a filled (dark) button vs. on a surface. */
  onFill: '#ffffff',
  /** Letter-spacing for the small-caps section labels. */
  capsTracking: '0.14em',
}
