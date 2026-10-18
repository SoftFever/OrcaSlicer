import { Directory, File, Paths } from 'expo-file-system'

// Aggregate sizes for the buckets of on-device state the app writes: vendor
// profiles (under data/system/), autosave.3mf, calibration models cache, and
// the downloads scratch dir the URL-import feature uses. Each bucket has a
// human-readable label + a clear() the settings screen can call.
//
// Directory.size() walks recursively, so per-bucket cost is proportional to the
// files inside — for a fully populated Documents/ it is usually under 50ms.
// Safe to call on the JS thread when a user opens the Storage page.

export interface Bucket {
  id: string
  label: string
  description: string
  size: number
  destructive: boolean
  clear: () => void
}

function sizeOf(entry: Directory | File | null): number {
  if (entry === null || !entry.exists) return 0
  try {
    return entry.size ?? 0
  } catch { return 0 }
}

function nuke(entry: Directory | File | null): void {
  if (entry === null || !entry.exists) return
  try { entry.delete() } catch { /* best-effort */ }
}

export function usageBuckets(): Bucket[] {
  const document = Paths.document
  const cache    = Paths.cache

  const dataSystem = new Directory(document, 'OrcaSlicer', 'data', 'system')
  const dataUser   = new Directory(document, 'OrcaSlicer', 'data', 'user')
  const autosave   = new File(document, 'autosave.3mf')
  const calib      = new Directory(document, 'calib')
  const downloads  = new Directory(cache, 'downloads')

  return [
    {
      id: 'system-profiles',
      label: 'Vendor profiles',
      description: 'Printer / filament / process presets fetched when you pick a printer. Removing forces a re-download next time you pick.',
      size: sizeOf(dataSystem),
      destructive: true,
      clear: () => nuke(dataSystem),
    },
    {
      id: 'user-profiles',
      label: 'User presets',
      description: 'Your saved-as filaments and processes. Deleting is permanent.',
      size: sizeOf(dataUser),
      destructive: true,
      clear: () => nuke(dataUser),
    },
    {
      id: 'autosave',
      label: 'Autosaved plate',
      description: 'The plate that reopens on next launch. Removing starts you with a blank scene next time.',
      size: sizeOf(autosave),
      destructive: false,
      clear: () => nuke(autosave),
    },
    {
      id: 'calibration',
      label: 'Calibration models',
      description: 'Test models downloaded when you run a calibration wizard. Re-fetches on next run.',
      size: sizeOf(calib),
      destructive: false,
      clear: () => nuke(calib),
    },
    {
      id: 'downloads',
      label: 'URL download cache',
      description: 'Models fetched via the long-press Import URL flow. Safe to clear at any time.',
      size: sizeOf(downloads),
      destructive: false,
      clear: () => nuke(downloads),
    },
  ]
}

/** Human-readable "12.4 MB" from a byte count. Powers-of-1024 to match Files.app. */
export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  const kb = bytes / 1024
  if (kb < 1024) return `${kb.toFixed(1)} KB`
  const mb = kb / 1024
  if (mb < 1024) return `${mb.toFixed(1)} MB`
  return `${(mb / 1024).toFixed(2)} GB`
}
