// Value helpers for the serialized option format the core reads and writes: the same
// text libslic3r stores in its config files. Pure functions, no React Native imports,
// so they run under Node's test runner (options.test.ts).

import type { OptionDefinition } from 'react-native-orca-core'

/** Editor modes, matching ConfigOptionMode in libslic3r. */
export type OptionMode = 'simple' | 'advanced' | 'expert'

export const MODE_LEVEL: Record<OptionMode, number> = { simple: 0, advanced: 1, expert: 2 }

/** True when a definition should be shown at the given mode. Develop-mode options never show. */
export function visibleInMode(def: OptionDefinition, mode: OptionMode): boolean {
  return def.mode <= MODE_LEVEL[mode]
}

/** Splits a serialized vector of numbers or booleans ("0.4,0.4") into its elements. */
export function splitScalars(value: string): string[] {
  return value === '' ? [] : value.split(',')
}

export function joinScalars(values: string[]): string {
  return values.join(',')
}

/**
 * Decodes one element of a serialized string vector. libslic3r writes string vectors as
 * `"a";"b"` with C-style escapes inside the quotes; a single unquoted element is legal too.
 */
export function unescapeCStyle(value: string): string {
  let text = value
  if (text.length >= 2 && text.startsWith('"') && text.endsWith('"')) {
    text = text.slice(1, -1)
  }
  let out = ''
  for (let i = 0; i < text.length; i++) {
    const c = text[i]
    if (c === '\\' && i + 1 < text.length) {
      const n = text[i + 1]
      i++
      switch (n) {
        case 'n':
          out += '\n'
          break
        case 'r':
          out += '\r'
          break
        case 't':
          out += '\t'
          break
        default:
          out += n
      }
    } else {
      out += c
    }
  }
  return out
}

/** Encodes one string vector element the way libslic3r's escape_string_cstyle does. */
export function escapeCStyle(value: string): string {
  let out = '"'
  for (const c of value) {
    switch (c) {
      case '\\':
        out += '\\\\'
        break
      case '"':
        out += '\\"'
        break
      case '\n':
        out += '\\n'
        break
      case '\r':
        out += '\\r'
        break
      case '\t':
        out += '\\t'
        break
      default:
        out += c
    }
  }
  return out + '"'
}

/** Splits a serialized string vector on the `;` separators outside quotes. */
export function splitStrings(value: string): string[] {
  if (value === '') return []
  const out: string[] = []
  let current = ''
  let quoted = false
  for (let i = 0; i < value.length; i++) {
    const c = value[i]
    if (c === '\\' && quoted && i + 1 < value.length) {
      current += c + value[i + 1]
      i++
    } else if (c === '"') {
      quoted = !quoted
      current += c
    } else if (c === ';' && !quoted) {
      out.push(current)
      current = ''
    } else {
      current += c
    }
  }
  out.push(current)
  return out
}

/** What the editor shows for a definition: scalar editors for single-element vectors. */
export type EditorKind =
  | 'bool'
  | 'number'
  | 'percent'
  | 'float_or_percent'
  | 'enum'
  | 'text'
  | 'code'
  | 'raw'

export function editorKind(def: OptionDefinition, serialized: string): EditorKind {
  const multi =
    def.vector &&
    (def.type === 'string' ? splitStrings(serialized).length : splitScalars(serialized).length) > 1
  if (multi || def.type === 'point' || def.type === 'other') return 'raw'
  switch (def.type) {
    case 'bool':
      return 'bool'
    case 'int':
    case 'float':
      return 'number'
    case 'percent':
      return 'percent'
    case 'float_or_percent':
      return 'float_or_percent'
    case 'enum':
      return 'enum'
    case 'string':
      return def.key.includes('gcode') || def.tooltip.includes('G-code') ? 'code' : 'text'
    default:
      return 'raw'
  }
}

/** The single value shown by a scalar editor, decoded from the serialized form. */
export function decodeScalar(def: OptionDefinition, serialized: string): string {
  if (def.type === 'string') {
    if (!def.vector) return serialized
    const parts = splitStrings(serialized)
    return parts.length === 0 ? '' : unescapeCStyle(parts[0] ?? '')
  }
  if (def.vector) {
    const parts = splitScalars(serialized)
    return parts[0] ?? ''
  }
  return serialized
}

/** The serialized form of a scalar editor's value, keeping the vector shape of the original. */
export function encodeScalar(def: OptionDefinition, serialized: string, value: string): string {
  if (def.type === 'string') {
    return def.vector ? escapeCStyle(value) : value
  }
  if (def.vector) {
    const parts = splitScalars(serialized)
    if (parts.length <= 1) return value
    parts[0] = value
    return joinScalars(parts)
  }
  return value
}

export function decodeBool(value: string): boolean {
  return value === '1' || value === 'true'
}

export function encodeBool(value: boolean): string {
  return value ? '1' : '0'
}

/** Percent values serialize as "15%"; this strips the sign for the editor. */
export function decodePercent(value: string): string {
  return value.endsWith('%') ? value.slice(0, -1) : value
}

export function encodePercent(value: string): string {
  return value.trim() === '' ? '' : `${value.trim()}%`
}

/** Groups definitions by category, keeping the definition order within each group. */
export function groupByCategory(
  defs: OptionDefinition[],
): Array<{ category: string; options: OptionDefinition[] }> {
  const groups = new Map<string, OptionDefinition[]>()
  for (const def of defs) {
    const category = def.category === '' ? 'Other' : def.category
    const list = groups.get(category)
    if (list === undefined) {
      groups.set(category, [def])
    } else {
      list.push(def)
    }
  }
  return [...groups.entries()].map(([category, options]) => ({ category, options }))
}

/** Case-insensitive match on key, label and tooltip. */
export function matchesQuery(def: OptionDefinition, query: string): boolean {
  const needle = query.trim().toLowerCase()
  if (needle === '') return true
  return (
    def.key.includes(needle) ||
    def.label.toLowerCase().includes(needle) ||
    def.fullLabel.toLowerCase().includes(needle) ||
    def.tooltip.toLowerCase().includes(needle)
  )
}
