// Deterministic cloud ids, matching the desktop's
// OrcaCloudServiceAgent::generate_uuid_for_setting_id: RFC 4122 version 5
// (SHA-1) over a fixed namespace and "<user id>/<preset name>". Pure TypeScript
// so it runs in tests and never waits on a native digest.

export const SETTING_ID_NAMESPACE = 'f47ac10b-58cc-4372-a567-0e02b2c3d479'

function utf8(s: string): Uint8Array {
  return new TextEncoder().encode(s)
}

function uuidBytes(uuid: string): Uint8Array {
  const hex = uuid.replace(/-/g, '')
  const out = new Uint8Array(16)
  for (let i = 0; i < 16; ++i) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16)
  return out
}

/** SHA-1 of `data`; small and plain, the inputs here are short. */
export function sha1(data: Uint8Array): Uint8Array {
  const ml = data.length * 8
  const padded = new Uint8Array(((data.length + 9 + 63) >> 6) << 6)
  padded.set(data)
  padded[data.length] = 0x80
  const view = new DataView(padded.buffer)
  view.setUint32(padded.length - 4, ml >>> 0)
  view.setUint32(padded.length - 8, Math.floor(ml / 0x100000000))

  let h0 = 0x67452301,
    h1 = 0xefcdab89,
    h2 = 0x98badcfe,
    h3 = 0x10325476,
    h4 = 0xc3d2e1f0
  const w = new Uint32Array(80)
  const rotl = (x: number, n: number): number => ((x << n) | (x >>> (32 - n))) >>> 0
  for (let off = 0; off < padded.length; off += 64) {
    for (let i = 0; i < 16; ++i) w[i] = view.getUint32(off + i * 4)
    for (let i = 16; i < 80; ++i)
      w[i] = rotl((w[i - 3]! ^ w[i - 8]! ^ w[i - 14]! ^ w[i - 16]!) >>> 0, 1)
    let a = h0,
      b = h1,
      c = h2,
      d = h3,
      e = h4
    for (let i = 0; i < 80; ++i) {
      let f: number, k: number
      if (i < 20) {
        f = (b & c) | (~b & d)
        k = 0x5a827999
      } else if (i < 40) {
        f = b ^ c ^ d
        k = 0x6ed9eba1
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d)
        k = 0x8f1bbcdc
      } else {
        f = b ^ c ^ d
        k = 0xca62c1d6
      }
      const t = (rotl(a, 5) + (f >>> 0) + e + k + w[i]!) >>> 0
      e = d
      d = c
      c = rotl(b, 30)
      b = a
      a = t
    }
    h0 = (h0 + a) >>> 0
    h1 = (h1 + b) >>> 0
    h2 = (h2 + c) >>> 0
    h3 = (h3 + d) >>> 0
    h4 = (h4 + e) >>> 0
  }
  const out = new Uint8Array(20)
  const ov = new DataView(out.buffer)
  ov.setUint32(0, h0)
  ov.setUint32(4, h1)
  ov.setUint32(8, h2)
  ov.setUint32(12, h3)
  ov.setUint32(16, h4)
  return out
}

/** RFC 4122 v5 UUID of `name` in `namespace`. */
export function uuid5(namespace: string, name: string): string {
  const ns = uuidBytes(namespace)
  const nm = utf8(name)
  const input = new Uint8Array(ns.length + nm.length)
  input.set(ns)
  input.set(nm, ns.length)
  const h = sha1(input)
  h[6] = (h[6]! & 0x0f) | 0x50 // version 5
  h[8] = (h[8]! & 0x3f) | 0x80 // RFC 4122 variant
  const hex = Array.from(h.slice(0, 16), (b) => b.toString(16).padStart(2, '0')).join('')
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20, 32)}`
}

/** The cloud `setting_id` the desktop would give `presetName` for `userId`. */
export function settingIdFor(userId: string, presetName: string): string {
  return uuid5(SETTING_ID_NAMESPACE, userId === '' ? presetName : `${userId}/${presetName}`)
}
