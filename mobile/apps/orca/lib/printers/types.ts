// Printer hosts the app can send G-code to. Both speak plain HTTP, so they live in the
// app rather than in the native core.

export type PrinterHostKind = 'moonraker' | 'octoprint'

export interface PrinterHost {
  /** Stable id, generated when the printer is added. */
  id: string
  name: string
  kind: PrinterHostKind
  /** Base URL including scheme, e.g. http://voron.local or http://192.168.1.20:7125. */
  url: string
  /** Moonraker: optional API key. OctoPrint: required application key. */
  apiKey?: string
}

export type SensorKind = 'nozzle' | 'bed' | 'chamber' | 'toolhead' | 'mcu' | 'fan' | 'other'

/** One temperature-carrying object read from the host. */
export interface Sensor {
  /** Display name, e.g. "Extruder", "Heater Bed", "Chamber", "Toolhead RP2040". */
  name: string
  kind: SensorKind
  /** Current reading in celsius. */
  current: number
  /** Optional target when the sensor is a heater; undefined for passive sensors. */
  target?: number
  /** Sensor's advertised safe range, when the host exposes it. */
  min?: number
  max?: number
  /** For a fan-driven sensor: fan speed 0..1. */
  fanSpeed?: number
}

export interface PrinterStatus {
  /** Free text from the host: printing, paused, ready, error, offline. */
  state: string
  /** 0 to 1 while a job runs. */
  progress?: number
  /** Current job file name. */
  file?: string
  nozzleTemperature?: number
  nozzleTarget?: number
  bedTemperature?: number
  bedTarget?: number
  /** Every temperature-carrying object the host exposes; nozzle and bed are duplicated
   * here for parity with the fields above. Empty when the host reports none. */
  sensors: Sensor[]
}

export interface UploadOptions {
  /** Absolute path of the G-code on device. */
  path: string
  /** Name the host stores the file under. */
  filename: string
  /** Start printing after the upload. */
  startPrint: boolean
}

export type HeatedTool = 'nozzle' | 'bed'

export interface PrinterClient {
  /** Resolves to the host's own name or version text; rejects when unreachable. */
  info(host: PrinterHost): Promise<string>
  status(host: PrinterHost): Promise<PrinterStatus>
  upload(host: PrinterHost, options: UploadOptions): Promise<void>
  /** Print-job control. Idempotent — the host returns success even if the command doesn't apply. */
  pause(host: PrinterHost): Promise<void>
  resume(host: PrinterHost): Promise<void>
  cancel(host: PrinterHost): Promise<void>
  /** Sets the target temperature in celsius. Pass 0 to turn a heater off. */
  setTemperature(host: PrinterHost, tool: HeatedTool, target: number): Promise<void>
  /** Absolute URL of the printer's MJPEG stream, when it exposes one. */
  webcamUrl(host: PrinterHost): Promise<string | null>
}

/** Base URL with no trailing slash. */
export function normalizeUrl(url: string): string {
  let out = url.trim()
  if (!/^https?:\/\//i.test(out)) {
    out = 'http://' + out
  }
  return out.replace(/\/+$/, '')
}

import { File as FsFile } from 'expo-file-system'

/**
 * A FormData body part for a file on disk. On the New Architecture,
 * React Native's FormData rejects the legacy `{ uri, name, type }` shape
 * with "Unsupported FormDataPart implementation"; it only accepts real
 * Blobs now. expo-file-system's `File` implements Blob, so we hand it
 * that directly. The caller is expected to pass `filename` as the third
 * argument to `FormData.append` so the multipart part carries the right
 * name.
 */
export function filePart(path: string, _filename: string, _type = 'text/x-gcode'): Blob {
  return new FsFile(path) as unknown as Blob
}
