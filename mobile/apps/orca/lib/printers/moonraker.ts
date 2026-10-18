import {
  filePart,
  normalizeUrl,
  type PrinterClient,
  type PrinterHost,
  type PrinterStatus,
  type Sensor,
  type SensorKind,
  type UploadOptions,
} from './types'

// Moonraker (Klipper). API reference: https://moonraker.readthedocs.io/en/latest/web_api/

function headers(host: PrinterHost): Record<string, string> {
  return host.apiKey !== undefined && host.apiKey !== '' ? { 'X-Api-Key': host.apiKey } : {}
}

async function json<T>(response: Response): Promise<T> {
  if (!response.ok) {
    throw new Error(`Moonraker ${response.status} ${response.statusText}`)
  }
  return (await response.json()) as T
}

interface ServerInfo {
  result: { klippy_state: string; moonraker_version?: string }
}

interface ObjectsList {
  result: { objects: string[] }
}

interface StatusMap {
  [key: string]:
    | {
        temperature?: number
        target?: number
        speed?: number
        min_temp?: number
        max_temp?: number
        state?: string
        filename?: string
        progress?: number
      }
    | undefined
}

interface ObjectsQuery {
  result: { status: StatusMap }
}

/** Which Klipper object prefixes carry temperatures Mainsail plots. */
const TEMPERATURE_PREFIXES = [
  'extruder',
  'heater_bed',
  'heater_generic',
  'temperature_sensor',
  'temperature_fan',
  'mcu',
]

/** Best-guess display kind from the object's key. */
function sensorKindOf(objectKey: string): SensorKind {
  if (objectKey === 'extruder' || objectKey.startsWith('extruder')) return 'nozzle'
  if (objectKey === 'heater_bed') return 'bed'
  const lower = objectKey.toLowerCase()
  if (lower.includes('chamber')) return 'chamber'
  if (lower.includes('toolhead')) return 'toolhead'
  if (lower.includes('mcu') || lower.includes('mainboard')) return 'mcu'
  if (objectKey.startsWith('temperature_fan')) return 'fan'
  return 'other'
}

/** Format the object key ("temperature_sensor Chamber") into a display label. */
function labelOf(objectKey: string): string {
  if (objectKey === 'extruder') return 'Extruder'
  if (objectKey === 'heater_bed') return 'Heater Bed'
  const parts = objectKey.split(/\s+/, 2)
  return parts.length === 2 ? parts[1]! : parts[0]!
}

export const moonraker: PrinterClient = {
  async info(host) {
    const base = normalizeUrl(host.url)
    const info = await json<ServerInfo>(
      await fetch(`${base}/server/info`, { headers: headers(host) }),
    )
    return `Moonraker ${info.result.moonraker_version ?? ''} (Klipper ${info.result.klippy_state})`.trim()
  },

  async status(host): Promise<PrinterStatus> {
    const base = normalizeUrl(host.url)
    // Two-step query: enumerate the printer's exposed objects so we can subscribe to
    // every temperature-carrying one (chamber, mcu, toolhead sensors, extra fans),
    // then fetch the readings in a single round-trip. Falls back to just extruder +
    // heater_bed if the objects/list endpoint is unavailable — matches the previous
    // behavior on older Moonraker builds.
    let tempObjects: string[] = []
    try {
      const list = await json<ObjectsList>(
        await fetch(`${base}/printer/objects/list`, { headers: headers(host) }),
      )
      tempObjects = list.result.objects.filter((o) =>
        TEMPERATURE_PREFIXES.some((p) => o === p || o.startsWith(p + ' ')),
      )
    } catch {
      tempObjects = ['extruder', 'heater_bed']
    }
    const parts = ['print_stats=state,filename', 'display_status=progress']
    for (const o of tempObjects)
      parts.push(`${encodeURIComponent(o)}=temperature,target,speed,min_temp,max_temp`)
    const data = await json<ObjectsQuery>(
      await fetch(`${base}/printer/objects/query?${parts.join('&')}`, { headers: headers(host) }),
    )
    const s = data.result.status
    const sensors: Sensor[] = []
    for (const key of tempObjects) {
      const obj = s[key]
      // Klipper reports `null` for heater_generic sensors that haven't reported
      // yet (probe timing out, MCU still enumerating, disconnected thermistor),
      // and the response type only documents `undefined`. Guard both.
      if (
        obj?.temperature === undefined ||
        obj.temperature === null ||
        !Number.isFinite(obj.temperature)
      )
        continue
      sensors.push({
        name: labelOf(key),
        kind: sensorKindOf(key),
        current: obj.temperature,
        target: obj.target !== undefined && obj.target > 0 ? obj.target : undefined,
        min: obj.min_temp,
        max: obj.max_temp,
        fanSpeed: obj.speed,
      })
    }
    return {
      state: s.print_stats?.state ?? 'unknown',
      progress: s.display_status?.progress,
      file: s.print_stats?.filename,
      nozzleTemperature: s.extruder?.temperature,
      nozzleTarget: s.extruder?.target,
      bedTemperature: s.heater_bed?.temperature,
      bedTarget: s.heater_bed?.target,
      sensors,
    }
  },

  async upload(host, options: UploadOptions) {
    const base = normalizeUrl(host.url)
    const body = new FormData()
    body.append('file', filePart(options.path, options.filename), options.filename)
    body.append('root', 'gcodes')
    body.append('print', options.startPrint ? 'true' : 'false')
    const response = await fetch(`${base}/server/files/upload`, {
      method: 'POST',
      headers: headers(host),
      body,
    })
    if (!response.ok) {
      throw new Error(`Upload failed: Moonraker ${response.status} ${await response.text()}`)
    }
  },

  async pause(host) {
    await control(host, 'pause')
  },
  async resume(host) {
    await control(host, 'resume')
  },
  async cancel(host) {
    await control(host, 'cancel')
  },

  async setTemperature(host, tool, target) {
    // Moonraker exposes gcode injection at /printer/gcode/script?script=…
    // M104 sets nozzle target, M140 sets bed target; both take S<target>.
    const command =
      tool === 'nozzle' ? `M104 S${Math.round(target)}` : `M140 S${Math.round(target)}`
    const base = normalizeUrl(host.url)
    const url = `${base}/printer/gcode/script?script=${encodeURIComponent(command)}`
    const response = await fetch(url, { method: 'POST', headers: headers(host) })
    if (!response.ok)
      throw new Error(`Moonraker set ${tool} failed: ${response.status} ${await response.text()}`)
  },

  async webcamUrl(host) {
    // Moonraker's cameras array. Prefer the first camera whose stream is an
    // MJPEG endpoint (works with a plain <img>/WebView). Fall back to the
    // best-effort mjpg-streamer path Klipper installs default to.
    const base = normalizeUrl(host.url)
    try {
      const list = await json<{ result: { cameras: Array<{ stream_url?: string }> } }>(
        await fetch(`${base}/server/webcams/list`, { headers: headers(host) }),
      )
      const stream = list.result.cameras.find(
        (c) => c.stream_url !== undefined && c.stream_url !== '',
      )?.stream_url
      if (stream !== undefined && stream !== '') {
        return stream.startsWith('http')
          ? stream
          : `${base}${stream.startsWith('/') ? '' : '/'}${stream}`
      }
    } catch {
      /* older Moonraker or no cameras endpoint */
    }
    return `${base}/webcam/?action=stream`
  },
}

async function control(host: PrinterHost, action: 'pause' | 'resume' | 'cancel'): Promise<void> {
  const base = normalizeUrl(host.url)
  const response = await fetch(`${base}/printer/print/${action}`, {
    method: 'POST',
    headers: headers(host),
  })
  if (!response.ok)
    throw new Error(`Moonraker ${action} failed: ${response.status} ${await response.text()}`)
}
