import type { OrcaSession } from 'react-native-orca-core'

// Mobile v1 calibrations. Each wizard configures the current session's process
// preset for a known-good calibration and hands the user a description of what
// to do next (usually import a specific model and slice). Bundling the desktop's
// .drc calibration models is a follow-up — the mobile core does not decode
// Draco today — so v1 works with any model the user provides.
//
// Every wizard reads/writes via session.setOption, which routes to the correct
// preset collection (print / filament / printer). Changes persist as edits on
// the currently-selected preset; the user is expected to Save-as if they want to
// keep them beyond the calibration slice.

export interface Calibration {
  id: string
  name: string
  description: string
  suggestedModel: string
  /** How many discrete "bands" the tower / plate is expected to have. Used for
   * the layer_change_gcode template that steps through parameter values. */
  bands: number
  /** Numeric parameter with a sensible default. UI lets user tune bounds. */
  param: { key: 'start' | 'end' | 'step'; label: string; unit: string; default: number }[]
  /** Path inside the OrcaSlicer repo (resources/calib/...) — the wizard fetches
   * it from GitHub raw so the user does not have to bring their own STL. Empty
   * when no .3mf is available (temperature tower ships as .drc which mobile
   * cannot decode). */
  modelPath: string
  /** Applies the config edits for this wizard. Returns any warnings for the UI
   * to surface. Never throws — misconfigured presets just report their gap. */
  apply(session: OrcaSession, values: Record<string, number>): { warnings: string[] }
}

/** Absolute raw-file URL for a resources/calib/... path. */
export function modelUrlFor(calibration: Calibration): string | null {
  if (calibration.modelPath === '') return null
  return `https://raw.githubusercontent.com/OrcaSlicer/OrcaSlicer/main/${calibration.modelPath.split('/').map(encodeURIComponent).join('/')}`
}

// Emit a per-layer G-code block that ramps a parameter from `start` to `end` over
// `bands` steps, injected via layer_change_gcode. Each band spans equal layer
// count so the boundary between them is a single M command.
function stepGcode(command: string, start: number, end: number, bands: number, unit = ''): string {
  const step = (end - start) / Math.max(1, bands - 1)
  const lines: string[] = ['; Mobile calibration ramp — one M command per band']
  for (let i = 0; i < bands; i++) {
    const v = start + step * i
    lines.push(`{if layer_num == ${i * 5}}${command} ${v.toFixed(3)}${unit}{endif}`)
  }
  return lines.join('\n')
}

const TEMPERATURE_TOWER: Calibration = {
  id: 'temp-tower',
  name: 'Temperature tower',
  description:
    'Prints a tower whose bands change temperature every ~5 layers so you can spot the cleanest at a glance. Recommended for a new filament roll.',
  suggestedModel: 'Import your own tower model (mobile core does not decode Draco yet, so the desktop temperature_tower.drc is not available)',
  modelPath: '',
  bands: 6,
  param: [
    { key: 'start', label: 'Start °C', unit: '°C', default: 220 },
    { key: 'end',   label: 'End °C',   unit: '°C', default: 180 },
    { key: 'step',  label: 'Bands',    unit: '',   default: 6   },
  ],
  apply(session, values) {
    const warnings: string[] = []
    const start = values.start ?? 220
    const end   = values.end ?? 180
    const bands = Math.max(2, Math.round(values.step ?? 6))
    const gcode = stepGcode('M104 S', start, end, bands)
    if (!session.setOption('layer_change_gcode', gcode)) warnings.push('Could not write layer_change_gcode.')
    // Slow the print so temperature transitions actually take effect.
    if (!session.setOption('outer_wall_speed', '30')) warnings.push('Could not set outer_wall_speed.')
    return { warnings }
  },
}

const FLOW_RATE: Calibration = {
  id: 'flow-rate',
  name: 'Flow rate',
  description:
    'Adjusts the filament flow ratio while printing a series of thin walls. Choose the wall thickness closest to your nozzle diameter for readable results.',
  suggestedModel: 'Orca Linear Flow test plate (auto-downloaded)',
  modelPath: 'resources/calib/filament_flow/Orca-LinearFlow.3mf',
  bands: 5,
  param: [
    { key: 'start', label: 'Start flow', unit: '%',  default: 95  },
    { key: 'end',   label: 'End flow',   unit: '%',  default: 105 },
    { key: 'step',  label: 'Bands',      unit: '',   default: 5   },
  ],
  apply(session, values) {
    const warnings: string[] = []
    const start = (values.start ?? 95) / 100
    const end   = (values.end ?? 105) / 100
    const bands = Math.max(2, Math.round(values.step ?? 5))
    // M221 sets the flow-rate multiplier globally; step by band.
    const gcode = stepGcode('M221 S', start * 100, end * 100, bands)
    if (!session.setOption('layer_change_gcode', gcode)) warnings.push('Could not write layer_change_gcode.')
    if (!session.setOption('wall_loops', '1')) warnings.push('Could not set wall_loops.')
    return { warnings }
  },
}

const PRESSURE_ADVANCE: Calibration = {
  id: 'pressure-advance',
  name: 'Pressure Advance',
  description:
    'Ramps M900 K (or Marlin Linear Advance K) across bands, printing lines that reveal the best K for your setup. Best for Klipper or Marlin LA machines.',
  suggestedModel: 'Orca PA-pattern plate (auto-downloaded)',
  modelPath: 'resources/calib/pressure_advance/pa_pattern.3mf',
  bands: 10,
  param: [
    { key: 'start', label: 'Start K', unit: '', default: 0.00 },
    { key: 'end',   label: 'End K',   unit: '', default: 0.10 },
    { key: 'step',  label: 'Bands',   unit: '', default: 10   },
  ],
  apply(session, values) {
    const warnings: string[] = []
    const start = values.start ?? 0
    const end   = values.end ?? 0.1
    const bands = Math.max(2, Math.round(values.step ?? 10))
    // M900 K<value> — Klipper accepts SET_PRESSURE_ADVANCE ADVANCE=<v>, but M900
    // works on both Klipper (as a passthrough) and Marlin LA. Use M900.
    const gcode = stepGcode('M900 K', start, end, bands)
    if (!session.setOption('layer_change_gcode', gcode)) warnings.push('Could not write layer_change_gcode.')
    return { warnings }
  },
}

export const CALIBRATIONS: Calibration[] = [TEMPERATURE_TOWER, FLOW_RATE, PRESSURE_ADVANCE]
