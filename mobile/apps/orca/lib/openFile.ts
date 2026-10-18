import { PRESET_FILE_PATTERN } from './presetFiles'

// Files handed to the app from Files.app, a share sheet or a deep link. Models
// go to the plate, preset files to the importer; anything else is ignored.

export const MODEL_FILE_PATTERN = /\.(3mf|stl|obj|step|stp|amf)$/i

export type OpenedFile = { kind: 'model'; path: string } | { kind: 'presets'; path: string } | null

/** Decodes a file:// URL into a path and says what to do with it. */
export function classifyOpenedUrl(raw: string): OpenedFile {
  let path: string
  try {
    path = decodeURIComponent(raw.replace(/^file:\/\//, ''))
  } catch {
    return null
  }
  if (MODEL_FILE_PATTERN.test(path)) return { kind: 'model', path }
  if (PRESET_FILE_PATTERN.test(path)) return { kind: 'presets', path }
  return null
}
