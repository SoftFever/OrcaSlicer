import { moonraker } from './moonraker'
import { octoprint } from './octoprint'
import type { PrinterClient, PrinterHost, PrinterHostKind } from './types'

export type { HeatedTool, PrinterClient, PrinterHost, PrinterHostKind, PrinterStatus, Sensor, SensorKind, UploadOptions } from './types'
export { normalizeUrl } from './types'
export { loadPrinters, savePrinters } from './store'

export const HOST_KINDS: Array<{ kind: PrinterHostKind; label: string; hint: string }> = [
  { kind: 'moonraker', label: 'Klipper (Moonraker)', hint: 'http://printer.local or http://192.168.1.20:7125' },
  { kind: 'octoprint', label: 'OctoPrint', hint: 'http://octopi.local, application key required' },
]

export function clientFor(host: PrinterHost): PrinterClient {
  return host.kind === 'moonraker' ? moonraker : octoprint
}
