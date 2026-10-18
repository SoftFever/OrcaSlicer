import { describe, expect, it } from 'vitest'

import { settingIdFor, sha1, uuid5 } from './uuid5'

const hex = (b: Uint8Array): string => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('')

describe('sha1', () => {
  it('matches the reference vectors', () => {
    expect(hex(sha1(new TextEncoder().encode('')))).toBe('da39a3ee5e6b4b0d3255bfef95601890afd80709')
    expect(hex(sha1(new TextEncoder().encode('abc')))).toBe('a9993e364706816aba3e25717850c26c9cd0d89d')
    expect(hex(sha1(new TextEncoder().encode('The quick brown fox jumps over the lazy dog')))).toBe(
      '2fd4e1c67a2d28fced849ee1bb76e7391b93eb12',
    )
  })
})

describe('uuid5', () => {
  it('reproduces the RFC 4122 example', () => {
    // uuid5(NAMESPACE_DNS, "python.org") from Python's uuid module docs
    expect(uuid5('6ba7b810-9dad-11d1-80b4-00c04fd430c8', 'python.org')).toBe('886313e1-3b8a-5372-9b90-0c9aee199e5d')
  })
})

describe('settingIdFor', () => {
  // Ids read from the .info sidecars of presets synced by the desktop app.
  const user = 'a8ad795c-5b90-47da-8171-2f7381de06c6'
  it('matches ids the desktop generated for the same user and preset', () => {
    expect(settingIdFor(user, 'Grey PLA')).toBe('96e5bc75-5245-5347-9a44-0b67e0fa8149')
    expect(settingIdFor(user, 'Qidi X-Max 3 Freedi')).toBe('48f9cb34-837a-5550-a94b-24ea5a787dc9')
  })
  it('differs per user', () => {
    expect(settingIdFor('other-user', 'Grey PLA')).not.toBe(settingIdFor(user, 'Grey PLA'))
  })
})
