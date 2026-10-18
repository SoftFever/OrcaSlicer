import { filePart, normalizeUrl, type PrinterClient, type PrinterHost, type PrinterStatus, type Sensor, type UploadOptions } from './types'

// OctoPrint. API reference: https://docs.octoprint.org/en/master/api/

function headers(host: PrinterHost): Record<string, string> {
  return { 'X-Api-Key': host.apiKey ?? '' }
}

async function json<T>(response: Response): Promise<T> {
  if (!response.ok) {
    throw new Error(`OctoPrint ${response.status} ${response.statusText}`)
  }
  return (await response.json()) as T
}

interface Version {
  server: string
  text: string
}

interface Job {
  state: string
  progress: { completion: number | null }
  job: { file: { name: string | null } }
}

interface Printer {
  temperature: {
    [key: string]: { actual: number; target: number } | undefined
  }
}

export const octoprint: PrinterClient = {
  async info(host) {
    const base = normalizeUrl(host.url)
    const version = await json<Version>(await fetch(`${base}/api/version`, { headers: headers(host) }))
    return version.text
  },

  async status(host): Promise<PrinterStatus> {
    const base = normalizeUrl(host.url)
    const job = await json<Job>(await fetch(`${base}/api/job`, { headers: headers(host) }))
    const status: PrinterStatus = {
      state: job.state,
      progress: job.progress.completion === null ? undefined : job.progress.completion / 100,
      file: job.job.file.name ?? undefined,
      sensors: [],
    }
    // /api/printer answers 409 while the printer is disconnected; temperatures are optional.
    const printerResponse = await fetch(`${base}/api/printer?exclude=sd,state`, { headers: headers(host) })
    if (printerResponse.ok) {
      const printer = (await printerResponse.json()) as Printer
      status.nozzleTemperature = printer.temperature.tool0?.actual
      status.nozzleTarget = printer.temperature.tool0?.target
      status.bedTemperature = printer.temperature.bed?.actual
      status.bedTarget = printer.temperature.bed?.target
      // Every tool + bed + chamber the host exposes, so the mobile Device panel can
      // render the same rows Mainsail / OctoPrint show. `toolN` keys are numbered
      // starting at 0 — OctoPrint's convention for multi-extruder machines.
      for (const [key, temp] of Object.entries(printer.temperature)) {
        // OctoPrint returns `actual: null` for tools that aren't connected and
        // for a bed on a printer that has no bed. Skip those instead of pushing
        // a Sensor whose typed-number `current` is really null.
        if (temp === undefined || temp.actual === null || !Number.isFinite(temp.actual)) continue
        const s: Sensor = {
          name: key === 'bed' ? 'Bed' : key === 'chamber' ? 'Chamber' : key.startsWith('tool') ? `Tool ${key.slice(4)}` : key,
          kind: key === 'bed' ? 'bed' : key === 'chamber' ? 'chamber' : key.startsWith('tool') ? 'nozzle' : 'other',
          current: temp.actual,
          target: temp.target > 0 ? temp.target : undefined,
        }
        status.sensors.push(s)
      }
    }
    return status
  },

  async upload(host, options: UploadOptions) {
    const base = normalizeUrl(host.url)
    const body = new FormData()
    body.append('file', filePart(options.path, options.filename), options.filename)
    body.append('select', 'true')
    body.append('print', options.startPrint ? 'true' : 'false')
    const response = await fetch(`${base}/api/files/local`, { method: 'POST', headers: headers(host), body })
    if (!response.ok) {
      throw new Error(`Upload failed: OctoPrint ${response.status} ${await response.text()}`)
    }
  },

  async pause(host) { await job(host, 'pause', 'pause') },
  async resume(host) { await job(host, 'pause', 'resume') },
  async cancel(host) { await job(host, 'cancel') },

  async setTemperature(host, tool, target) {
    // OctoPrint splits by tool. /api/printer/tool for nozzle heaters,
    // /api/printer/bed for the print bed.
    const base = normalizeUrl(host.url)
    const path = tool === 'nozzle' ? '/api/printer/tool' : '/api/printer/bed'
    const body = tool === 'nozzle'
      ? { command: 'target', targets: { tool0: Math.round(target) } }
      : { command: 'target', target: Math.round(target) }
    const response = await fetch(`${base}${path}`, {
      method: 'POST',
      headers: { ...headers(host), 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    if (!response.ok) throw new Error(`OctoPrint set ${tool} failed: ${response.status} ${await response.text()}`)
  },

  async webcamUrl(host) {
    // OctoPrint stores the stream URL in /api/settings under `webcam.streamUrl`
    // (older) or `webcam.stream` (newer). Fall back to the default mjpg-streamer
    // path that ships with OctoPi.
    const base = normalizeUrl(host.url)
    try {
      const settings = await json<{ webcam?: { streamUrl?: string; stream?: string } }>(
        await fetch(`${base}/api/settings`, { headers: headers(host) }),
      )
      const stream = settings.webcam?.streamUrl ?? settings.webcam?.stream
      if (stream !== undefined && stream !== '') {
        return stream.startsWith('http') ? stream : `${base}${stream.startsWith('/') ? '' : '/'}${stream}`
      }
    } catch {
      /* no settings access or missing key */
    }
    return `${base}/webcam/?action=stream`
  },
}

async function job(host: PrinterHost, command: 'pause' | 'cancel', action?: 'pause' | 'resume' | 'toggle'): Promise<void> {
  const base = normalizeUrl(host.url)
  const body = action !== undefined ? { command, action } : { command }
  const response = await fetch(`${base}/api/job`, {
    method: 'POST',
    headers: { ...headers(host), 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  if (!response.ok) throw new Error(`OctoPrint ${command} failed: ${response.status} ${await response.text()}`)
}
