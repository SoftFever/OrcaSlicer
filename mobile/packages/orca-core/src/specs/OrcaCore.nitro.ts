import type { HybridObject } from 'react-native-nitro-modules'

// The slicing core, one-to-one with src/mobile/core/Session.hpp. Every type here is a
// plain struct or a string union so nitrogen generates matching C++; the native side
// converts to and from the façade's types.

export type PresetKind = 'printer' | 'filament' | 'process'

export interface PresetInfo {
  name: string
  /** Vendor id for system presets, empty otherwise. */
  vendor: string
  isSystem: boolean
  isUser: boolean
  /** Installed in the desktop sense; the app may ignore this. */
  isVisible: boolean
  /** Compatible with the selected printer. */
  isCompatible: boolean
  isSelected: boolean
}

/** A preset named by kind, as the import / export and cloud calls address them. */
export interface PresetRef {
  kind: PresetKind
  name: string
}

/**
 * Why an import or a cloud pull left a preset out.
 * - `system_name`: a built-in preset of that name exists (desktop skips these too)
 * - `missing_parent`: `inherits` names a preset that is not installed; `vendorHint` is the
 *   vendor whose profile ships it when that vendor is present in resources, else empty
 * - `exists`: a user preset of that name exists and overwriting was off
 * - `invalid`: not a preset file, or unreadable
 */
export type SkipReason = 'system_name' | 'missing_parent' | 'exists' | 'invalid'

export interface SkippedPreset {
  name: string
  reason: SkipReason
  inherits: string
  vendorHint: string
}

export interface ImportPresetsResult {
  imported: PresetRef[]
  skipped: SkippedPreset[]
  /** User presets the archive would overwrite; only filled when `overwrite` was false. */
  conflicts: string[]
}

/** Cloud sync state of a user preset, mirroring the desktop's .info sidecar. */
export interface CloudPresetState {
  kind: PresetKind
  name: string
  /** Cloud id, empty before the first successful push. */
  settingId: string
  /** The parent's setting id when the parent is a cloud preset. */
  baseId: string
  /** 'create', 'update', 'hold', 'will_not_sync' or '' (in sync). */
  syncInfo: string
  updatedTime: number
}

export interface CloudApplyResult {
  /** Non-empty when the payload could not be read. */
  error: string
  /** Presets whose parent is not installed. */
  unresolved: SkippedPreset[]
}

/** A deletion the cloud has not confirmed yet. */
export interface PendingDelete {
  settingId: string
  infoPath: string
}

export interface Vec2 {
  x: number
  y: number
}

export interface Vec3 {
  x: number
  y: number
  z: number
}

export type MirrorAxis = 'x' | 'y' | 'z'

export interface PickHit {
  /** Object id of the closest hit, or 0 when the ray missed everything. */
  hitId: number
  /** World-space position of the ray's first intersection. */
  position: Vec3
}

export interface ObjectInfo {
  /** Stable for the life of the session. */
  id: number
  name: string
  /** Instance offset, mm. */
  position: Vec3
  /** Euler angles, radians. */
  rotation: Vec3
  scale: Vec3
  /** World-space bounding box, mm. */
  size: Vec3
  triangles: number
  /** Unconnected face edges over the object's model parts; 0 for a watertight mesh. */
  openEdges: number
}

/** What repairObject() did; counts are summed over the object's model parts. */
export interface MeshRepairResult {
  /** A part's mesh was replaced. */
  changed: boolean
  openEdgesBefore: number
  openEdgesAfter: number
  verticesMerged: number
  degenerateFaces: number
  facesRemoved: number
  facesReversed: number
  edgesFixed: number
}

export interface BedInfo {
  /** Printable area polygon, mm. */
  shape: Vec2[]
  /** Printable height, mm. */
  height: number
  /** Absolute path to the printer's bed texture (PNG/SVG), empty when unset. */
  customTexture: string
  /** Absolute path to the printer's bed model (STL), empty when unset. */
  customModel: string
}

export interface Progress {
  percent: number
  message: string
}

/**
 * 'outOfMemory' is a cancellation the session raised itself when the device ran low on
 * memory (or a caught allocation failure); offer to simplify or shrink the model.
 */
export type SliceOutcome = 'finished' | 'cancelled' | 'failed' | 'outOfMemory'

export interface SliceWarning {
  text: string
  critical: boolean
}

export interface SliceResult {
  outcome: SliceOutcome
  /** Set when the outcome is 'failed'. */
  error: string
  warnings: SliceWarning[]
}

export interface PlateInfo {
  /** Stable id across other-plate removals; the value the JS layer holds on to. */
  id: number
  objectCount: number
  isSliced: boolean
  isCurrent: boolean
}

export interface SnapshotInfo {
  /** Label of the action whose result this snapshot captured. */
  label: string
  /** True for the entry that matches the live model. */
  current: boolean
}

export interface SliceStatistics {
  /** Normal-mode estimate. */
  printTimeSeconds: number
  /** Total extruded volume. */
  filamentMm3: number
  /** By filament density where known. */
  filamentGrams: number
  filamentCost: number
  layerCount: number
}

/**
 * One project: presets, model and print. Calls that read or edit state are synchronous
 * and throw while a slice is running; the long operations return promises and run on
 * the session's own thread, one at a time.
 */
export interface OrcaSession extends HybridObject<{ ios: 'c++'; android: 'c++' }> {
  /** Identifies this session to native views such as OrcaViewport. */
  readonly id: number
  /** Loads vendor profiles from the resources directory and user presets from the data directory. Resolves to an error text, empty on success. */
  loadPresets(): Promise<string>
  /** Same as loadPresets, but first wipes the mirror at data/system so a same-version reinstall of a vendor really refreshes it (used after installing / removing a vendor). */
  reloadPresets(): Promise<string>
  presets(kind: PresetKind): PresetInfo[]
  selectedPreset(kind: PresetKind): string
  /** Select by name. For filaments this selects extruder 0. */
  selectPreset(kind: PresetKind, name: string): boolean
  selectFilament(extruder: number, name: string): boolean
  /** Fork the current edited preset of `kind` under a new user preset name and persist it. Returns false when the name collides with a system / project / external preset. */
  savePresetAs(kind: PresetKind, name: string): boolean
  /** Delete a user preset by name. Returns false if the preset is not user-owned or does not exist. */
  deletePreset(kind: PresetKind, name: string): boolean
  /** Absolute path of a preset's JSON file, empty when it has no on-disk backing. */
  presetFile(kind: PresetKind, name: string): string
  bed(): BedInfo

  /**
   * Imports a desktop preset export: a single preset .json, a .zip of presets, or an
   * .orca_printer / .orca_filament / .orca_bundle archive. Parent resolution and the
   * diff-against-parent save match the desktop. With `asBundle` false (the usual case)
   * archives land as ordinary user presets; true keeps the desktop's read-only bundle
   * semantics. With `overwrite` false, clashing user presets are reported in
   * `conflicts` and left alone. Call reloadPresets() afterwards.
   */
  importPresets(path: string, overwrite: boolean, asBundle: boolean): Promise<ImportPresetsResult>
  /**
   * Writes the named user presets as a zip at `outPath`. The extension picks the layout:
   * .orca_printer (exactly one printer plus its filaments and processes), .orca_bundle
   * (any mix) or .zip (flat). Printer presets are written without their print host and
   * API key fields. Resolves to the path written; rejects with a readable message.
   */
  exportPresets(presets: PresetRef[], outPath: string): Promise<string>

  /**
   * Orca Cloud sync, preset side only; the app does the HTTP. Picks the user preset
   * folder (data/user/<userId>, or data/user/default for '') that the next
   * loadPresets() reads and every save writes to. Call before loadPresets() at boot
   * and reloadPresets() after changing it.
   */
  setCloudUser(userId: string): void
  readonly cloudUser: string
  /** User presets that need a push: never synced, or marked create / update / hold. */
  cloudDirtyPresets(): CloudPresetState[]
  /**
   * JSON for one push: {"content":{...},"updated_time":N,"setting_id":"…","sync_info":"…"}.
   * `content` is the difference from the parent plus name / type / version / inherits,
   * exactly what the desktop uploads. Empty when the preset is not a user preset.
   */
  cloudPushPayload(kind: PresetKind, name: string): string
  /** Records a push outcome: the setting id, the server's updated_time (0 keeps the local one) and the new sync_info ('' = in sync). */
  cloudMarkSynced(kind: PresetKind, name: string, settingId: string, updatedTime: number, syncInfo: string): void
  /**
   * Applies a FULL pull: the JSON array of {id, name, updated_time, content} upserts.
   * Synced presets missing from the array are removed locally, so never pass a partial
   * list. Presets whose parent is not installed come back in `unresolved`.
   */
  cloudApplyPull(upsertsJson: string): Promise<CloudApplyResult>
  /** Deletions awaiting the cloud's confirmation. */
  cloudPendingDeletes(): PendingDelete[]
  /** Drops the sidecar of a deletion the server has confirmed. */
  cloudConfirmDeleted(settingId: string): void

  /** Serialized option value as libslic3r writes it in config files ("0.2", "1,2,3", "nil"). */
  option(key: string): string
  setOption(key: string, value: string): boolean
  modifiedOptions(kind: PresetKind): string[]
  discardModifiedOptions(kind: PresetKind): void

  /** Writes every plate as a BBS-style multi-plate .3mf project file — models + edited presets + selected printer config. Resolves to the path written. Rejects when the write fails. */
  saveProject(path: string): Promise<string>
  /**
   * Replaces the current project with the plates read from a .3mf. Single-plate
   * projects come back as a single plate; multi-plate projects rebuild each plate
   * from the file's plate_data mapping. Rejects on failure. Prefer this over
   * `importModel` for .3mf files since it restores the plate structure.
   */
  openProject(path: string): Promise<PlateInfo[]>
  /** Reads STL, OBJ, 3MF, STEP, AMF or any format the core accepts and places the objects on the CURRENT plate. Rejects on failure. */
  importModel(path: string): Promise<ObjectInfo[]>
  objects(): ObjectInfo[]
  removeObject(id: number): boolean
  setTransform(id: number, position: Vec3, rotation: Vec3, scale: Vec3): boolean
  /** Duplicates an object as a new independent instance offset next to the original. Returns the new object info; id is 0 when the source is missing. */
  duplicateObject(id: number): ObjectInfo
  /** Mirrors along the given axis by flipping the corresponding scale sign. */
  mirrorObject(id: number, axis: MirrorAxis): boolean
  /** Rotates the object so its flattest bottom face rests on the bed. */
  layFlat(id: number): boolean
  /** Runs the auto-orient algorithm (same routine as libslic3r's `orient`). */
  autoOrient(id: number): boolean
  /** Cuts an object horizontally at world-space Z, keeping both halves on the plate. Returns the new object ids in bed order (upper first). */
  cutObject(id: number, z: number): number[]
  /**
   * Reduces the object's model parts to about `targetTriangles` faces in total (quadric
   * edge collapse, as the desktop Simplify gizmo). Resolves false when the object is
   * unknown, already under the target, or cancel() was called while it ran; nothing
   * changes in those cases. Seconds on a million-face mesh; other calls throw busy meanwhile.
   */
  simplifyObject(id: number, targetTriangles: number): Promise<boolean>
  /**
   * Re-runs the repair libslic3r applies when it reads an STL: duplicate vertices merged,
   * degenerate faces dropped, near edges snapped, unconnected faces removed, normals fixed.
   * Holes are not filled, so openEdgesAfter may stay above zero.
   */
  repairObject(id: number): Promise<MeshRepairResult>
  /** Ray-cast against every visible instance. `hitId` is 0 when nothing was picked. */
  pickAt(rayOrigin: Vec3, rayDirection: Vec3): PickHit
  clear(): void
  arrange(): Promise<void>

  /**
   * Multi-plate. A project can hold several plates; model / print / undo methods
   * operate on the current one. Presets are shared across plates.
   */
  addPlate(): number
  /** Clone the current plate (objects + transforms). Returns the new plate's id. */
  duplicatePlate(): number
  /** Refuses to delete the last remaining plate. */
  removePlate(id: number): boolean
  selectPlate(id: number): boolean
  readonly currentPlate: number
  plateList(): PlateInfo[]

  /**
   * Multi-material. `extruderCount` matches the selected printer's nozzle list;
   * `selectedFilaments()[i]` names the filament in slot i. Per-object assignment
   * sets which extruder prints an object — 0 means inherit the process default.
   */
  readonly extruderCount: number
  selectedFilaments(): string[]
  objectExtruder(id: number): number
  setObjectExtruder(id: number, extruder: number): boolean

  /**
   * Record the current model as a new snapshot with `label`, dropping any redo tail.
   * The discrete mutations above snapshot themselves; call this after continuous
   * gestures — a move / rotate / scale drag — commit, using the same labels desktop
   * uses ("Move object", "Rotate", "Scale").
   */
  takeSnapshot(label: string): void
  /** Restore the previous snapshot. Returns false when there is nothing to undo. */
  undo(): boolean
  redo(): boolean
  readonly canUndo: boolean
  readonly canRedo: boolean
  /** Label of the action that would be undone / redone; empty when unavailable. */
  undoLabel(): string
  redoLabel(): string
  /** History, oldest first, with the current entry flagged. */
  snapshotHistory(): SnapshotInfo[]

  /** Slices the current model with the selected presets. onProgress is called on the JS thread. */
  slice(onProgress: (progress: Progress) => void): Promise<SliceResult>
  /** Safe to call from any point while slice() is pending. */
  cancel(): void
  readonly isSliced: boolean
  readonly isBusy: boolean

  /** Writes the G-code of the sliced print. Resolves to the path written. */
  exportGCode(path: string): Promise<string>
  /** Valid after exportGCode(). */
  statistics(): SliceStatistics
}

export interface OrcaCore extends HybridObject<{ ios: 'c++'; android: 'c++' }> {
  /** Version of the slicing core compiled into the app. */
  readonly version: string
  /** Points the core at the app's writable directories. Call once, before creating a session. */
  initialize(resourcesDir: string, dataDir: string): void
  /** Every print, filament and printer option definition as a JSON array; see Session::option_definitions_json. */
  optionDefinitionsJson(): string
  createSession(): OrcaSession
}
