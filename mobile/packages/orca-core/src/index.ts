import { NitroModules } from 'react-native-nitro-modules'
import type { OrcaCore } from './specs/OrcaCore.nitro'

export { OrcaViewport, type OrcaViewportRef } from './views/OrcaViewport'
export type { ObjectTool, OrcaViewportMethods, OrcaViewportProps, PreviewColorMode, ViewportMode } from './specs/OrcaViewport.nitro'

export type {
  BedInfo,
  CloudApplyResult,
  CloudPresetState,
  ImportPresetsResult,
  MirrorAxis,
  ObjectInfo,
  OrcaCore,
  OrcaSession,
  PickHit,
  PendingDelete,
  PlateInfo,
  PresetInfo,
  PresetKind,
  PresetRef,
  SkipReason,
  SkippedPreset,
  Progress,
  SliceOutcome,
  SliceResult,
  SliceStatistics,
  SliceWarning,
  SnapshotInfo,
  Vec2,
  Vec3,
} from './specs/OrcaCore.nitro'

let core: OrcaCore | undefined

/** The slicing core. Created on first use; there is one per app. */
export function getOrcaCore(): OrcaCore {
  if (core === undefined) {
    core = NitroModules.createHybridObject<OrcaCore>('OrcaCore')
  }
  return core
}

/** Option definition as produced by OrcaCore.optionDefinitionsJson(). */
export interface OptionDefinition {
  key: string
  kind: 'process' | 'filament' | 'printer' | 'other'
  type: 'float' | 'int' | 'string' | 'percent' | 'float_or_percent' | 'point' | 'bool' | 'enum' | 'other'
  vector: boolean
  nullable: boolean
  readonly: boolean
  label: string
  fullLabel: string
  category: string
  tooltip: string
  unit: string
  /** 0 simple, 1 advanced, 2 expert, 3 develop. */
  mode: number
  default?: string
  min?: number
  max?: number
  enumValues?: string[]
  enumLabels?: string[]
}

let definitions: OptionDefinition[] | undefined

/** Parsed option definitions, cached for the life of the app. */
export function getOptionDefinitions(): OptionDefinition[] {
  if (definitions === undefined) {
    definitions = JSON.parse(getOrcaCore().optionDefinitionsJson()) as OptionDefinition[]
  }
  return definitions
}
