import * as Network from 'expo-network'

import type { PrinterHostKind } from './printers'

export interface DiscoveredHost {
  kind: PrinterHostKind
  ip: string
  url: string
  info: string
}

// iOS gates true mDNS behind an entitlement the Expo runtime does not carry,
// so we do a best-effort /24 sweep instead: hit /server/info on Moonraker's
// port and /api/version on OctoPrint's, both under a short timeout. This
// finds the common self-hosted case (Klipper + Mainsail on a Raspberry Pi
// or an OctoPi at .100/.150 kind of address) without any native code.

const MOONRAKER_PORT = 7125
const OCTOPRINT_PORT = 80
const PROBE_TIMEOUT_MS = 800

/** Derives the /24 network prefix from an IP like 192.168.1.42 → 192.168.1. Returns null for anything unexpected. */
export function subnetPrefix(ip: string): string | null {
  const parts = ip.split('.')
  if (parts.length !== 4) return null
  if (parts.some((p) => !/^\d+$/.test(p))) return null
  return `${parts[0]}.${parts[1]}.${parts[2]}.`
}

async function withTimeout<T>(p: Promise<T>, ms: number): Promise<T> {
  return await Promise.race([
    p,
    new Promise<T>((_, reject) => setTimeout(() => reject(new Error('timeout')), ms)),
  ])
}

async function probeMoonraker(ip: string): Promise<DiscoveredHost | null> {
  const url = `http://${ip}:${MOONRAKER_PORT}`
  try {
    const response = await withTimeout(fetch(`${url}/server/info`), PROBE_TIMEOUT_MS)
    if (!response.ok) return null
    const body = (await response.json()) as { result?: { klippy_state?: string } }
    return { kind: 'moonraker', ip, url, info: `Klipper ${body.result?.klippy_state ?? ''}`.trim() }
  } catch {
    return null
  }
}

async function probeOctoprint(ip: string): Promise<DiscoveredHost | null> {
  const url = `http://${ip}:${OCTOPRINT_PORT}`
  try {
    // /api/version returns 200 even without an API key; that is enough to
    // confirm an OctoPrint is at this address.
    const response = await withTimeout(fetch(`${url}/api/version`), PROBE_TIMEOUT_MS)
    if (!response.ok) return null
    const body = (await response.json()) as { text?: string; server?: string }
    return {
      kind: 'octoprint',
      ip,
      url,
      info: body.text ?? `OctoPrint ${body.server ?? ''}`.trim(),
    }
  } catch {
    return null
  }
}

/**
 * Scan the local /24 for Moonraker and OctoPrint hosts. Reports found hosts
 * incrementally via onFound; resolves once every address has been probed or
 * cancelled through the AbortSignal. Runs at most 16 probes concurrently.
 */
export async function scanLocalNetwork(
  onFound: (host: DiscoveredHost) => void,
  signal?: AbortSignal,
): Promise<void> {
  const ip = await Network.getIpAddressAsync()
  const prefix = subnetPrefix(ip)
  if (prefix === null) return

  const addresses: string[] = []
  for (let i = 1; i <= 254; i += 1) addresses.push(`${prefix}${i}`)

  const concurrency = 16
  let cursor = 0
  const worker = async (): Promise<void> => {
    while (!signal?.aborted) {
      const idx = cursor
      cursor += 1
      if (idx >= addresses.length) return
      const target = addresses[idx]
      if (target === undefined) return
      const [m, o] = await Promise.all([probeMoonraker(target), probeOctoprint(target)])
      if (m !== null) onFound(m)
      if (o !== null) onFound(o)
    }
  }
  await Promise.all(Array.from({ length: concurrency }, () => worker()))
}
