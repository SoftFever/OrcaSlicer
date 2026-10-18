import { Directory, File, Paths } from 'expo-file-system'

// Printer, filament and process profiles are not bundled with the app. They are fetched
// from the OrcaSlicer repository into the core's resources directory: the vendor index
// files first, then one vendor folder at a time when the user installs a vendor.

/** Repository and ref the profiles are fetched from. */
export interface ProfileSource {
  owner: string
  repo: string
  ref: string
}

export const DEFAULT_PROFILE_SOURCE: ProfileSource = {
  owner: 'OrcaSlicer',
  repo: 'OrcaSlicer',
  ref: 'main',
}

interface GitHubEntry {
  name: string
  path: string
  type: 'file' | 'dir'
  download_url: string | null
}

async function listDirectory(source: ProfileSource, path: string): Promise<GitHubEntry[]> {
  const url = `https://api.github.com/repos/${source.owner}/${source.repo}/contents/${path}?ref=${encodeURIComponent(source.ref)}`
  const response = await fetch(url, { headers: { Accept: 'application/vnd.github+json' } })
  if (!response.ok) {
    throw new Error(`GitHub ${response.status} listing ${path}`)
  }
  return (await response.json()) as GitHubEntry[]
}

/** Retry policy for a single-file download. Small enough to keep the total
 * install time reasonable on a spotty network. */
const MAX_RETRIES = 4
const BASE_BACKOFF_MS = 500

/**
 * Download one file with byte-level retry. GitHub raw supports Range, so on
 * a mid-stream network drop we resume from where we left off rather than
 * starting over. Fails hard on non-partial-content or empty bodies.
 *
 * @param entry     GitHub file entry.
 * @param into      Destination directory.
 * @param onBytes   Optional per-file progress callback.
 * @param signal    Optional abort signal.
 */
async function download(
  entry: GitHubEntry,
  into: Directory,
  onBytes?: (received: number, total: number) => void,
  signal?: AbortSignal,
): Promise<void> {
  if (entry.download_url === null) {
    throw new Error(`No download URL for ${entry.path}`)
  }
  const target = new File(into, entry.name)
  if (target.exists) target.delete()

  const bailIfAborted = (): void => {
    if (signal?.aborted === true) throw new Error('cancelled')
  }

  let received = 0
  const chunks: Uint8Array[] = []
  let total = 0
  let attempt = 0
  while (true) {
    bailIfAborted()
    try {
      const headers: Record<string, string> = {}
      if (received > 0) headers['Range'] = `bytes=${received}-`
      const response = await fetch(entry.download_url, { headers, signal })
      const acceptable = received > 0 ? response.status === 206 : response.ok
      if (!acceptable) throw new Error(`HTTP ${response.status} downloading ${entry.path}`)
      const contentLengthHeader = response.headers.get('Content-Length')
      const contentLength = contentLengthHeader !== null ? Number.parseInt(contentLengthHeader, 10) : 0
      if (total === 0 && Number.isFinite(contentLength) && contentLength > 0) total = received + contentLength
      // fetch on iOS does not expose a stream reader in every RN release;
      // read as an ArrayBuffer per attempt and treat any error as
      // recoverable via a fresh Range request from `received`.
      const bytes = new Uint8Array(await response.arrayBuffer())
      chunks.push(bytes)
      received += bytes.length
      onBytes?.(received, total || received)
      break
    } catch (e) {
      const message = String(e)
      if (message.includes('cancelled')) throw e
      attempt += 1
      if (attempt > MAX_RETRIES) throw new Error(`Gave up on ${entry.path} after ${attempt} attempts: ${message}`)
      await new Promise<void>((r) => setTimeout(r, BASE_BACKOFF_MS * 2 ** (attempt - 1)))
    }
  }

  if (received === 0) throw new Error(`Empty response for ${entry.path}`)

  // Concatenate every chunk (usually one, sometimes more if we resumed) and
  // validate JSON before we let it near the native slicer's parser.
  const buffer = new Uint8Array(received)
  let offset = 0
  for (const c of chunks) {
    buffer.set(c, offset)
    offset += c.length
  }
  if (entry.name.toLowerCase().endsWith('.json')) {
    const text = new TextDecoder().decode(buffer)
    try {
      JSON.parse(text)
    } catch (e) {
      throw new Error(`Malformed JSON for ${entry.path}: ${String(e)}`)
    }
  }
  target.write(buffer)
}

/** The core's resources directory. Profiles live under resources/profiles. */
export function resourcesDirectory(): Directory {
  return new Directory(Paths.document, 'OrcaSlicer', 'resources')
}

/** The core's data directory: app config, user presets, logs. */
export function dataDirectory(): Directory {
  return new Directory(Paths.document, 'OrcaSlicer', 'data')
}

export function profilesDirectory(): Directory {
  return new Directory(resourcesDirectory(), 'profiles')
}

/** True when the on-disk vendor tree looks like a scoped install for `model` —
 * the machine file exists AND the manifest lists exactly one machine model,
 * which is what `pruneVendorManifest` leaves behind after a scoped install.
 * A previous unscoped install (or a scoped install for a different model of
 * the same vendor) fails this check and triggers a wipe + reinstall so the
 * user actually gets the printer they asked for. */
export function hasPrinterModelFiles(vendor: string, model: string): boolean {
  try {
    const machineDir = new Directory(profilesDirectory(), vendor, 'machine')
    if (!machineDir.exists) return false
    const file = new File(machineDir, `${model}.json`)
    if (!file.exists) return false
    const manifestFile = new File(profilesDirectory(), `${vendor}.json`)
    if (!manifestFile.exists) return false
    const manifest = JSON.parse(manifestFile.textSync()) as {
      machine_model_list?: Array<{ name?: string }>
    }
    const list = manifest.machine_model_list ?? []
    // A properly-scoped install has exactly the selected model.
    return list.length === 1 && list[0]?.name === model
  } catch {
    return false
  }
}

/** Absolute filesystem path of a directory, as the native core expects it. */
export function nativePath(directory: Directory): string {
  return decodeURIComponent(directory.uri.replace(/^file:\/\//, ''))
}

export function ensureDirectories(): void {
  for (const dir of [resourcesDirectory(), dataDirectory(), profilesDirectory()]) {
    if (!dir.exists) {
      dir.create({ intermediates: true })
    }
  }
}

/** Vendor ids that have an index file on disk. */
export function installedVendors(): string[] {
  const dir = profilesDirectory()
  if (!dir.exists) {
    return []
  }
  return dir
    .list()
    .filter((entry): entry is File => entry instanceof File && entry.name.endsWith('.json'))
    .map((file) => file.name.replace(/\.json$/, ''))
    .sort()
}

/** Vendor ids available at the source. One GitHub API call. */
export async function availableVendors(source: ProfileSource = DEFAULT_PROFILE_SOURCE): Promise<string[]> {
  const entries = await listDirectory(source, 'resources/profiles')
  return entries
    .filter((entry) => entry.type === 'file' && entry.name.endsWith('.json'))
    .map((entry) => entry.name.replace(/\.json$/, ''))
    .sort()
}

/** A printer available at the source, keyed by the vendor bundle that owns it. */
export interface AvailablePrinter {
  vendor: string
  vendorName: string
  model: string
}

/**
 * List every printer offered across every vendor bundle. Reads each vendor's
 * top-level manifest for its machine_model_list; one small HTTP fetch per
 * vendor, results cached by the caller. Empty or bad manifests are skipped.
 */
export async function availablePrinters(source: ProfileSource = DEFAULT_PROFILE_SOURCE): Promise<AvailablePrinter[]> {
  const vendors = await availableVendors(source)
  const results = await Promise.all(
    vendors.map(async (vendor) => {
      try {
        const url = `https://raw.githubusercontent.com/${source.owner}/${source.repo}/${source.ref}/resources/profiles/${encodeURIComponent(vendor)}.json`
        const response = await fetch(url)
        if (!response.ok) return [] as AvailablePrinter[]
        const manifest = (await response.json()) as { name?: string; machine_model_list?: Array<{ name?: string }> }
        const models = manifest.machine_model_list ?? []
        const vendorName = manifest.name ?? vendor
        return models
          .map((m) => m.name)
          .filter((n): n is string => typeof n === 'string' && n.length > 0)
          .map((model) => ({ vendor, vendorName, model }))
      } catch {
        return [] as AvailablePrinter[]
      }
    }),
  )
  return results.flat().sort((a, b) => a.model.localeCompare(b.model))
}

/**
 * The shared library of `@base` filament profiles that every other vendor's
 * filament presets inherit from (COEX PCTG PRIME @base, Generic PLA @base,
 * etc.). The C++ loader loads it first and treats it as the parent bundle
 * for cross-vendor inheritance (`PresetBundle::ORCA_FILAMENT_LIBRARY`, see
 * src/libslic3r/PresetBundle.cpp). If it is missing when a vendor bundle
 * that inherits from it is loaded, the vendor fails to parse. Every
 * `installVendor` call ensures this library is installed too.
 */
export const ORCA_FILAMENT_LIBRARY = 'OrcaFilamentLibrary'

interface VendorManifest {
  name?: string
  machine_model_list?: Array<{ name?: string; sub_path?: string }>
}

interface MachineModel {
  bed_model?: string
  bed_texture?: string
  hotend_model?: string
}

/**
 * Build a predicate that returns true when a filename belongs to another model's
 * scope, meaning it should be dropped from a scoped install. A file "belongs" to
 * a model M when either the file's stem (machine list: `Qidi X-Max 4 0.4 nozzle`)
 * or its `@`-tag (process/filament: `Bambu ABS @Qidi X-Max 4 0.4 nozzle`) begins
 * with M's full name or M's trailing form (`X-Max 4`), followed by a boundary.
 *
 * Selection wins on ties. `Qidi X-Max 3 0.4 nozzle` also starts with the "Qidi
 * X-Max" model, but the selected `Qidi X-Max 3` matches first and the file is
 * kept. Shared entries (`fdm_process_common`, `Bambu ABS.json`, or files whose
 * @-tag matches no known model at all) are always kept.
 *
 * Beats the previous regex-with-prefix-drop approach that leaked large families
 * of files whenever the selected model shared a stem with another one (X-Max
 * / X-Max 3, X-Plus / X-Plus 3, Q2 / Q2C).
 */
function makeOtherModelFilter(selectedModel: string, allModels: string[]): (filename: string) => boolean {
  const otherModels = allModels.filter((m) => m !== selectedModel)
  if (otherModels.length === 0) return () => false
  const trailing = (m: string): string => m.replace(/^\S+\s+/, '')
  const matches = (tag: string, m: string): boolean => {
    const forms = m === trailing(m) ? [m] : [m, trailing(m)]
    for (const form of forms) {
      if (tag === form) return true
      if (tag.startsWith(form + ' ')) return true
      if (tag.startsWith(form + '.')) return true
    }
    return false
  }
  return (filename) => {
    const stem = filename.replace(/\.json$/i, '')
    const atIdx = stem.lastIndexOf('@')
    // `@` tag wins when present — `Bambu ABS @Qidi X-Max 3` is scoped by the
    // trailing "@Qidi X-Max 3", not by "Bambu ABS". Fallback to the whole stem
    // for entries without a tag (machine variants).
    const tag = atIdx >= 0 ? stem.slice(atIdx + 1).replace(/^\s+/, '') : stem
    if (matches(tag, selectedModel)) return false
    for (const other of otherModels) {
      if (matches(tag, other)) return true
    }
    return false
  }
}

async function fetchModelAssets(
  source: ProfileSource,
  vendor: string,
  modelSubPath: string,
): Promise<MachineModel> {
  const url = `https://raw.githubusercontent.com/${source.owner}/${source.repo}/${source.ref}/resources/profiles/${encodeURIComponent(vendor)}/${modelSubPath.split('/').map(encodeURIComponent).join('/')}`
  const response = await fetch(url)
  if (!response.ok) return {}
  try {
    return (await response.json()) as MachineModel
  } catch {
    return {}
  }
}

async function downloadVendorTree(
  vendor: string,
  onProgress: ((done: number, total: number) => void) | undefined,
  source: ProfileSource,
  signal: AbortSignal | undefined,
  scopeToModel: string | undefined,
): Promise<void> {
  const root = profilesDirectory()
  const index = (await listDirectory(source, 'resources/profiles')).find((entry) => entry.name === `${vendor}.json`)
  if (index === undefined) {
    throw new Error(`Vendor ${vendor} not found at ${source.owner}/${source.repo}@${source.ref}`)
  }

  // When we are installing for a specific printer, read the vendor's manifest so
  // we know every other model's name (to filter machine / process / filament files
  // to just the selected one) and the STL / SVG assets the selected model
  // references so we skip every other model's bed art.
  let excludesOthers: ((filename: string) => boolean) | undefined
  let allowedRootAssets: Set<string> | undefined
  if (scopeToModel !== undefined && scopeToModel !== '') {
    try {
      const url = `https://raw.githubusercontent.com/${source.owner}/${source.repo}/${source.ref}/resources/profiles/${encodeURIComponent(vendor)}.json`
      const manifest = (await (await fetch(url)).json()) as VendorManifest
      const allModels = (manifest.machine_model_list ?? [])
        .map((m) => m.name)
        .filter((n): n is string => typeof n === 'string' && n.length > 0)
      excludesOthers = makeOtherModelFilter(scopeToModel, allModels)
      const selected = (manifest.machine_model_list ?? []).find((m) => m.name === scopeToModel)
      if (selected?.sub_path !== undefined) {
        const model = await fetchModelAssets(source, vendor, selected.sub_path)
        allowedRootAssets = new Set([model.bed_model, model.bed_texture, model.hotend_model].filter(
          (v): v is string => typeof v === 'string' && v.length > 0,
        ))
      }
    } catch {
      /* If the manifest read fails, fall back to the full-vendor install. */
      excludesOthers = undefined
      allowedRootAssets = undefined
    }
  }

  // Which folders inside the vendor tree carry model-scoped presets. Files
  // outside these folders (SLAs, README, etc.) are always kept.
  const SCOPED_FOLDERS = new Set(['machine', 'filament', 'process'])
  const isBinaryAsset = (name: string): boolean => /\.(stl|svg|png|jpg|jpeg|glb|obj|dxf)$/i.test(name)

  const files: Array<{ entry: GitHubEntry; into: Directory }> = [{ entry: index, into: root }]
  const walk = async (path: string, into: Directory, folder: string | null): Promise<void> => {
    for (const entry of await listDirectory(source, path)) {
      if (entry.type === 'dir') {
        await walk(entry.path, new Directory(into, entry.name), entry.name)
      } else {
        // Files at the vendor root that are binary bed / hotend assets: keep only
        // the ones the selected model actually references, when scoping.
        if (folder === null && isBinaryAsset(entry.name) && allowedRootAssets !== undefined) {
          if (!allowedRootAssets.has(entry.name)) continue
        }
        // Files under machine / filament / process: drop entries scoped to any
        // other printer model. Shared entries (fdm_process_common, vendor-generic
        // "Generic PLA @Qidi") stay.
        if (folder !== null && SCOPED_FOLDERS.has(folder) && excludesOthers !== undefined) {
          if (excludesOthers(entry.name)) continue
        }
        files.push({ entry, into })
      }
    }
  }
  await walk(`resources/profiles/${vendor}`, new Directory(root, vendor), null)

  let done = 0
  for (const { entry, into } of files) {
    if (signal?.aborted === true) throw new Error('cancelled')
    if (!into.exists) into.create({ intermediates: true })
    // The signal is forwarded to `download` so a cancellation aborts the
    // in-flight fetch cleanly instead of waiting for the current file to end.
    await download(entry, into, undefined, signal)
    done += 1
    onProgress?.(done, files.length)
  }

  // Prune the manifest to only entries whose `sub_path` file was actually kept.
  // Without this, C++'s PresetBundle walks the manifest's machine_model_list /
  // machine_list / process_list / filament_list and tries to load every entry —
  // so a scoped install that only kept the Max 3 files still gets a parse /
  // file-not-found error on the first missing entry (e.g. Qidi Q1 Pro). This
  // step is a no-op for a full install: every entry's file is present.
  if (scopeToModel !== undefined && scopeToModel !== '') {
    try {
      pruneVendorManifest(root, vendor)
    } catch {
      /* best-effort — if the rewrite fails the install still works, just with
         the same load errors this step is meant to prevent. */
    }
  }
}

// Removes entries from a downloaded vendor manifest whose `sub_path` no longer
// exists on disk. Runs after a scoped install; called from `downloadVendorTree`.
function pruneVendorManifest(root: Directory, vendor: string): void {
  const manifestFile = new File(root, `${vendor}.json`)
  if (!manifestFile.exists) return
  const raw = manifestFile.textSync()
  const manifest = JSON.parse(raw) as {
    machine_model_list?: Array<{ sub_path?: string }>
    machine_list?: Array<{ sub_path?: string }>
    process_list?: Array<{ sub_path?: string }>
    filament_list?: Array<{ sub_path?: string }>
    [k: string]: unknown
  }
  const vendorDir = new Directory(root, vendor)
  const keep = (list?: Array<{ sub_path?: string }>): Array<{ sub_path?: string }> => {
    if (list === undefined) return []
    return list.filter((entry) => {
      if (typeof entry.sub_path !== 'string' || entry.sub_path.length === 0) return true
      // sub_path is like "machine/Qidi X-Max 3.json" — split into segments so
      // we can address `<vendor>/<segment>/<segment>` under `profiles/`.
      const parts = entry.sub_path.split('/').filter((s) => s.length > 0)
      if (parts.length === 0) return false
      const filename = parts[parts.length - 1]
      if (filename === undefined) return false
      const dir = parts.length > 1 ? new Directory(vendorDir, ...parts.slice(0, -1)) : vendorDir
      const file = new File(dir, filename)
      return file.exists
    })
  }
  const next = {
    ...manifest,
    machine_model_list: keep(manifest.machine_model_list),
    machine_list:       keep(manifest.machine_list),
    process_list:       keep(manifest.process_list),
    filament_list:      keep(manifest.filament_list),
  }
  manifestFile.write(JSON.stringify(next, null, 2))
}

/**
 * Downloads a vendor's index file and its profile folder. Roughly one API call
 * per subfolder plus one download per file; a vendor is 1 to 10 MB.
 *
 * Also ensures `OrcaFilamentLibrary` is installed — vendor filament presets
 * inherit `@base` profiles from it, so loading a vendor without it always
 * fails at parse time.
 */
export async function installVendor(
  vendor: string,
  onProgress?: (done: number, total: number) => void,
  source: ProfileSource = DEFAULT_PROFILE_SOURCE,
  signal?: AbortSignal,
  /** When set, only download files for this printer model — machine variants
   * whose filenames start with the model name, filaments/processes marked with
   * `@<model>` or unmarked (shared / vendor-generic), and STL/SVG assets the
   * model's machine_model JSON references. */
  scopeToModel?: string,
): Promise<void> {
  ensureDirectories()
  if (vendor !== ORCA_FILAMENT_LIBRARY) {
    await ensureFilamentLibrary(source, signal, onProgress)
  }
  await downloadVendorTree(vendor, onProgress, source, signal, scopeToModel)
}

/**
 * Installs `OrcaFilamentLibrary` if it is not already on disk. Safe to call
 * on every startup; a fast no-op when present.
 */
export async function ensureFilamentLibrary(
  source: ProfileSource = DEFAULT_PROFILE_SOURCE,
  signal?: AbortSignal,
  onProgress?: (done: number, total: number) => void,
): Promise<void> {
  ensureDirectories()
  if (installedVendors().includes(ORCA_FILAMENT_LIBRARY)) return
  await downloadVendorTree(ORCA_FILAMENT_LIBRARY, onProgress, source, signal, undefined)
}

export function removeVendor(vendor: string): void {
  const root = profilesDirectory()
  const index = new File(root, `${vendor}.json`)
  if (index.exists) {
    index.delete()
  }
  const folder = new Directory(root, vendor)
  if (folder.exists) {
    folder.delete()
  }
}

/** Delete a single preset file, given an absolute path the core reported in a
 * "Failed loading configuration file …" error. No-ops when the path is missing
 * or falls outside the app's profiles directory — the check protects against a
 * malformed error string steering us at somebody else's data. Returns true when
 * a file was actually removed. */
export function removePresetFile(absolutePath: string): boolean {
  const root = profilesDirectory()
  const rootPath = nativePath(root)
  // absolutePath comes from a native error message. Normalize both to compare.
  const normalised = absolutePath.replace(/^file:\/\//, '')
  if (!normalised.startsWith(rootPath)) return false
  const relative = normalised.slice(rootPath.length).replace(/^\/+/, '').split('/')
  const filename = relative.pop()
  if (filename === undefined || filename === '') return false
  const dir = relative.length === 0 ? root : new Directory(root, ...relative)
  const target = new File(dir, filename)
  if (!target.exists) return false
  target.delete()
  return true
}
