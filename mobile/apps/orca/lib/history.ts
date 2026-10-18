import type { SliceStatistics } from 'react-native-orca-core'

import { storage } from './storage'

// Small MMKV-backed list of successful slices. Version the key so a future
// shape change can be busted without decoding stale entries into new types.
const KEY = 'history:v1'
const MAX_ENTRIES = 20

export interface HistoryEntry {
  id: string
  at: number
  printer: string
  filament: string
  process: string
  objectNames: string[]
  sourcePaths: string[]
  stats: SliceStatistics
  gcodePath: string | null
}

function read(): HistoryEntry[] {
  const raw = storage.getString(KEY)
  if (raw === undefined) return []
  try {
    const parsed = JSON.parse(raw)
    return Array.isArray(parsed) ? (parsed as HistoryEntry[]) : []
  } catch {
    return []
  }
}

function write(entries: HistoryEntry[]): void {
  storage.set(KEY, JSON.stringify(entries.slice(0, MAX_ENTRIES)))
}

export function history(): HistoryEntry[] {
  return read()
}

export function pushHistory(entry: Omit<HistoryEntry, 'id' | 'at'>): HistoryEntry {
  const full: HistoryEntry = {
    ...entry,
    id: `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`,
    at: Date.now(),
  }
  write([full, ...read()])
  return full
}

export function removeHistory(id: string): void {
  write(read().filter((e) => e.id !== id))
}

export function clearHistory(): void {
  storage.remove(KEY)
}

// Saved projects (.3mf) are tracked separately from slice runs. A user may save
// a plate without slicing, and the project entry has to survive a "clear slice
// history" tap without collateral damage.
const PROJECTS_KEY = 'projects:v1'
const MAX_PROJECTS = 20

export interface ProjectEntry {
  id: string
  at: number
  /** Absolute path or URI to the .3mf file. Cached copies live under the app's
   * documents directory; a share sheet may hand back an iCloud / Files URI. */
  path: string
  /** Filename without extension. */
  name: string
  objectCount: number
  objectNames: string[]
  /** Printer preset name at save time, so the row can carry the same context
   * the slice history rows do. */
  printer: string
}

function readProjects(): ProjectEntry[] {
  const raw = storage.getString(PROJECTS_KEY)
  if (raw === undefined) return []
  try {
    const parsed = JSON.parse(raw)
    return Array.isArray(parsed) ? (parsed as ProjectEntry[]) : []
  } catch {
    return []
  }
}

function writeProjects(entries: ProjectEntry[]): void {
  storage.set(PROJECTS_KEY, JSON.stringify(entries.slice(0, MAX_PROJECTS)))
}

export function projectHistory(): ProjectEntry[] {
  return readProjects()
}

/** Dedupes on `path` so re-saving the same file bumps its timestamp rather than
 * duplicating the row. */
export function pushProject(entry: Omit<ProjectEntry, 'id' | 'at'>): ProjectEntry {
  const now = Date.now()
  const others = readProjects().filter((p) => p.path !== entry.path)
  const full: ProjectEntry = {
    ...entry,
    id: `p-${now.toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
    at: now,
  }
  writeProjects([full, ...others])
  return full
}

export function removeProject(id: string): void {
  writeProjects(readProjects().filter((p) => p.id !== id))
}

// A row per gcode delivery — one entry when the user hits Upload / Print. Kept
// separate from slice history because a plate may be sent to several printers
// or re-sent to the same one after a job cancellation.
const PRINTS_KEY = 'prints:v1'
const MAX_PRINTS = 40

export interface PrintEntry {
  id: string
  at: number
  hostId: string
  hostName: string
  filename: string
  /** True when the send started the print immediately vs. a queued upload. */
  started: boolean
}

function readPrints(): PrintEntry[] {
  const raw = storage.getString(PRINTS_KEY)
  if (raw === undefined) return []
  try {
    const parsed = JSON.parse(raw)
    return Array.isArray(parsed) ? (parsed as PrintEntry[]) : []
  } catch {
    return []
  }
}

function writePrints(entries: PrintEntry[]): void {
  storage.set(PRINTS_KEY, JSON.stringify(entries.slice(0, MAX_PRINTS)))
}

export function printHistory(): PrintEntry[] {
  return readPrints()
}

export function pushPrint(entry: Omit<PrintEntry, 'id' | 'at'>): PrintEntry {
  const now = Date.now()
  const full: PrintEntry = {
    ...entry,
    id: `pr-${now.toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
    at: now,
  }
  writePrints([full, ...readPrints()])
  return full
}

export function removePrint(id: string): void {
  writePrints(readPrints().filter((p) => p.id !== id))
}
