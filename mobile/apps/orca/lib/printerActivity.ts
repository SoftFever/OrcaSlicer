import { createLiveActivity, createWidget, type LiveActivity } from 'expo-widgets'
import { Platform } from 'react-native'

import { loadPrinters } from './printers'
import { queryClient } from './queries'
import type { PrinterStatus } from './printers'
import {
  PrintProgressActivity,
  type PrintActivityProps,
} from '@/widgets/PrintProgress.liveactivity'
import { PrinterStatusWidget, type PrinterWidgetProps } from '@/widgets/PrinterStatus.widget'

// Drives the home screen widget and the print-progress Live Activity.
//
// It reads the status the app is already polling rather than polling again:
// DevicePanel runs a `printers/<id>/status` query per host, so we subscribe to
// the query cache and react to whatever lands there. That keeps this file out
// of the screens entirely — nothing here has to be wired into a component, and
// no extra network traffic is created.

const widget =
  Platform.OS === 'ios'
    ? createWidget<PrinterWidgetProps>('PrinterStatus', PrinterStatusWidget)
    : null
const activities =
  Platform.OS === 'ios'
    ? createLiveActivity<PrintActivityProps>('PrintProgress', PrintProgressActivity)
    : null

/** Hosts report free text; Moonraker says "printing", OctoPrint "Printing". */
function isPrinting(state: string): boolean {
  return /printing/i.test(state)
}

type Snapshot = { id: string; name: string; status: PrinterStatus }

/** The printer worth showing: a running print wins, otherwise the first known host. */
function pick(): Snapshot | null {
  const hosts = loadPrinters()
  if (hosts.length === 0) return null

  const snapshots: Snapshot[] = []
  for (const host of hosts) {
    const status = queryClient.getQueryData<PrinterStatus>(['printers', host.id, 'status'])
    if (status !== undefined) snapshots.push({ id: host.id, name: host.name, status })
  }
  if (snapshots.length === 0) return null
  return snapshots.find((s) => isPrinting(s.status.state)) ?? snapshots[0] ?? null
}

let live: LiveActivity<PrintActivityProps> | null = null
let liveHostId: string | null = null

async function sync(): Promise<void> {
  const picked = pick()
  if (picked === null) return
  const { status } = picked

  widget?.updateSnapshot({
    name: picked.name,
    state: status.state,
    progress: isPrinting(status.state) ? (status.progress ?? 0) : null,
    file: status.file ?? null,
    nozzle: status.nozzleTemperature ?? null,
    bed: status.bedTemperature ?? null,
  })

  if (activities === null) return

  if (isPrinting(status.state)) {
    const props: PrintActivityProps = {
      printer: picked.name,
      file: status.file ?? '',
      progress: status.progress ?? 0,
      state: status.state,
      nozzle: status.nozzleTemperature ?? null,
      bed: status.bedTemperature ?? null,
    }
    if (live === null || liveHostId !== picked.id) {
      // A different printer took over: end the old activity before starting one
      // for the new job, so the Dynamic Island never shows two prints at once.
      await live?.end('immediate')
      live = activities.start(props, `orcaslicer://host/${picked.id}`)
      liveHostId = picked.id
    } else {
      await live.update(props)
    }
    return
  }

  if (live !== null) {
    await live.end('default', {
      printer: picked.name,
      file: status.file ?? '',
      progress: 1,
      state: status.state,
      nozzle: status.nozzleTemperature ?? null,
      bed: status.bedTemperature ?? null,
    })
    live = null
    liveHostId = null
  }
}

/**
 * Starts mirroring printer status into the widget and Live Activity.
 * Returns an unsubscribe function. Safe to call on any platform.
 */
export function startPrinterActivityBridge(): () => void {
  if (Platform.OS !== 'ios') return () => {}

  // Recover activities this process did not start — the app may have been
  // killed and relaunched while a print was still running.
  const existing = activities?.getInstances() ?? []
  live = existing[0] ?? null

  return queryClient.getQueryCache().subscribe((event) => {
    if (event.type !== 'updated') return
    const key = event.query.queryKey
    if (!Array.isArray(key) || key[0] !== 'printers' || key[2] !== 'status') return
    void sync()
  })
}
