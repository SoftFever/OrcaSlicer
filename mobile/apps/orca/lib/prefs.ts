import { storage } from './storage'

// Small key/value bag for cross-launch preferences: the last-selected printer,
// filament and process presets; the last opened pane; the last selected object
// id in the plate. Kept flat in MMKV so a wipe (Settings → App → Clear cache)
// only nukes one namespace at a time. Version any shape change via the KEYS
// map so an old key with new type does not decode wrong.

const KEYS = {
  lastPrinter:  'prefs:v1:lastPrinter',
  lastFilament: 'prefs:v1:lastFilament',
  lastProcess:  'prefs:v1:lastProcess',
  lastPane:     'prefs:v1:lastPane',
} as const

function getString(key: string): string | null {
  return storage.getString(key) ?? null
}

function setString(key: string, value: string | null): void {
  if (value === null || value === '') storage.remove(key)
  else storage.set(key, value)
}

export interface RememberedPresets {
  printer: string | null
  filament: string | null
  process: string | null
}

export function readRememberedPresets(): RememberedPresets {
  return {
    printer:  getString(KEYS.lastPrinter),
    filament: getString(KEYS.lastFilament),
    process:  getString(KEYS.lastProcess),
  }
}

export function writeRememberedPresets(next: Partial<RememberedPresets>): void {
  if (next.printer  !== undefined) setString(KEYS.lastPrinter,  next.printer)
  if (next.filament !== undefined) setString(KEYS.lastFilament, next.filament)
  if (next.process  !== undefined) setString(KEYS.lastProcess,  next.process)
}

export type LastPane = 'prepare' | 'preview' | 'device'

export function readLastPane(): LastPane | null {
  const v = getString(KEYS.lastPane)
  return v === 'prepare' || v === 'preview' || v === 'device' ? v : null
}

export function writeLastPane(pane: LastPane): void {
  setString(KEYS.lastPane, pane)
}

/** Autosave path — a single .3mf on disk under the app's documents that the
 * app rewrites on background / app-quit and reopens on next launch. Location is
 * a plain constant so both the writer (background handler) and the reader (core
 * boot in CoreProvider) reference the same file without threading a value. */
export const AUTOSAVE_FILE = 'autosave.3mf'
