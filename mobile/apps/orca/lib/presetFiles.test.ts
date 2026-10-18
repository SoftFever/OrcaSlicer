import { describe, expect, it, vi } from 'vitest'

vi.mock('expo-document-picker', () => ({ getDocumentAsync: vi.fn() }))
vi.mock('expo-file-system', () => ({ Directory: class {}, File: class {}, Paths: { cache: '' } }))
vi.mock('expo-sharing', () => ({ isAvailableAsync: vi.fn(), shareAsync: vi.fn() }))
vi.mock('react-native', () => ({ Alert: { alert: vi.fn() } }))
vi.mock('./downloads', () => ({
  useDownloadsStore: { getState: () => ({ install: vi.fn(), byId: {} }) },
}))
vi.mock('./profiles', () => ({
  availablePrinters: vi.fn(),
  installedVendors: () => [],
  installVendor: vi.fn(),
}))
vi.mock('./queries', () => ({ queryClient: { fetchQuery: vi.fn() }, queryKeys: {} }))
vi.mock('./prefs', () => ({ writeRememberedPresets: vi.fn() }))
vi.mock('./toast', () => ({ showToast: vi.fn() }))

const { archiveFileName, describeImported, describeSkipped, guessPrinterFor, missingVendorIn } =
  await import('./presetFiles')

describe('describeImported', () => {
  it('counts by kind with plural forms', () => {
    expect(describeImported([])).toBe('Nothing imported')
    expect(describeImported([{ kind: 'printer', name: 'a' }])).toBe('Imported 1 printer')
    expect(
      describeImported([
        { kind: 'printer', name: 'a' },
        { kind: 'filament', name: 'b' },
        { kind: 'filament', name: 'c' },
        { kind: 'process', name: 'd' },
        { kind: 'process', name: 'e' },
      ]),
    ).toBe('Imported 1 printer, 2 filaments and 2 processes')
  })
})

describe('missingVendorIn', () => {
  it('returns the vendor only when every hint agrees', () => {
    expect(
      missingVendorIn({
        skipped: [{ name: 'x', reason: 'missing_parent', inherits: 'p', vendorHint: 'Qidi' }],
      }),
    ).toBe('Qidi')
    expect(
      missingVendorIn({
        skipped: [
          { name: 'x', reason: 'missing_parent', inherits: 'p', vendorHint: 'Qidi' },
          { name: 'y', reason: 'missing_parent', inherits: 'q', vendorHint: 'BBL' },
        ],
      }),
    ).toBeNull()
    expect(
      missingVendorIn({
        skipped: [{ name: 'x', reason: 'missing_parent', inherits: 'p', vendorHint: '' }],
      }),
    ).toBeNull()
  })
})

describe('describeSkipped', () => {
  it('groups by reason and names the missing parent', () => {
    const lines = describeSkipped([
      { name: 'Built-in', reason: 'system_name', inherits: '', vendorHint: '' },
      { name: 'Orphan', reason: 'missing_parent', inherits: 'Parent @X', vendorHint: '' },
      { name: 'junk.json', reason: 'invalid', inherits: '', vendorHint: '' },
    ])
    expect(lines).toHaveLength(3)
    expect(lines[0]).toContain('Built-in')
    expect(lines[1]).toContain('Orphan (needs Parent @X)')
    expect(lines[2]).toContain('junk.json')
  })
})

describe('guessPrinterFor', () => {
  const catalogue = [
    { vendor: 'Qidi', vendorName: 'QIDI', model: 'X-Max 3' },
    { vendor: 'Qidi', vendorName: 'QIDI', model: 'X-Plus 3' },
    { vendor: 'Voron', vendorName: 'Voron', model: 'Voron 2.4' },
    { vendor: 'BBL', vendorName: 'Bambu Lab', model: 'X1' },
  ]
  it('picks the longest model name found in the parent preset name', () => {
    expect(guessPrinterFor('Qidi X-Max 3 0.4 nozzle', catalogue)?.model).toBe('X-Max 3')
    expect(guessPrinterFor('0.20mm Standard @Voron 2.4 350', catalogue)?.model).toBe('Voron 2.4')
  })
  it('ignores short names that would match everywhere and returns null when nothing fits', () => {
    expect(guessPrinterFor('Generic PLA @Nowhere', catalogue)).toBeNull()
    expect(guessPrinterFor('X1 Carbon 0.4 nozzle', catalogue)).toBeNull()
  })
})

describe('archiveFileName', () => {
  it('keeps spaces and strips path separators', () => {
    expect(archiveFileName('Qidi X-Max 3 0.4 nozzle', 'orca_printer')).toBe(
      'Qidi X-Max 3 0.4 nozzle.orca_printer',
    )
    expect(archiveFileName('a/b\\c:d', 'zip')).toBe('a-b-c-d.zip')
  })
})
