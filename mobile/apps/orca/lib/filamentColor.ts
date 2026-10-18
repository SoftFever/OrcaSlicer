import type { OrcaSession } from 'react-native-orca-core'

// Reads the current filament_colour option from the session and returns one hex
// color per extruder slot. libslic3r stores this as a coStrings (vector<string>)
// and its serialized form is a semicolon-separated list, one value per slot.
// Missing / malformed entries fall back to a neutral gray so a row can still
// render a swatch instead of nothing.

const FALLBACK = '#999999'

function normalise(raw: string): string {
  const s = raw.trim().toLowerCase()
  if (s === '') return FALLBACK
  // Both "#rrggbb" and "rrggbb" are common — prepend the hash when missing.
  return s.startsWith('#') ? s : `#${s}`
}

export function filamentColors(session: OrcaSession): string[] {
  try {
    const raw = session.option('filament_colour')
    if (raw === '') return []
    return raw.split(';').map(normalise)
  } catch { /* mutex briefly held — caller usually re-reads on the next render */ }
  return []
}

/** One color for a specific slot (1-based to match libslic3r extruder ids); 0
 * falls back to slot 1. Returns the gray fallback rather than undefined so
 * <View backgroundColor> is always valid. */
export function filamentColorAt(session: OrcaSession, slot: number): string {
  const list = filamentColors(session)
  const idx = Math.max(0, slot - 1)
  return list[idx] ?? list[0] ?? FALLBACK
}
