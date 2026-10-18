import { describe, expect, it, vi } from 'vitest'

vi.mock('expo-network', () => ({ getNetworkStateAsync: vi.fn() }))
vi.mock('expo-file-system', () => ({ Directory: class {}, File: class {} }))
vi.mock('expo-secure-store', () => ({ getItemAsync: vi.fn(), setItemAsync: vi.fn(), deleteItemAsync: vi.fn() }))
vi.mock('../storage', () => ({ storage: { getString: () => undefined, getBoolean: () => undefined, getNumber: () => undefined, set: vi.fn(), remove: vi.fn() } }))
vi.mock('../profiles', () => ({ dataDirectory: vi.fn(), installedVendors: () => [], installVendor: vi.fn() }))
vi.mock('../toast', () => ({ showToast: vi.fn() }))

const { runSync } = await import('./sync')
const { settingIdFor } = await import('./uuid5')

import type { CloudApi, PushResult } from './api'
import type { SyncSession } from './sync'
import type { CloudPresetState, PendingDelete, SkippedPreset } from 'react-native-orca-core'

const USER = 'a8ad795c-5b90-47da-8171-2f7381de06c6'

function fakeSession(opts: { dirty?: CloudPresetState[]; pending?: PendingDelete[]; unresolved?: SkippedPreset[] } = {}) {
  const marks: Array<{ name: string; settingId: string; updatedTime: number; syncInfo: string }> = []
  const applied: string[] = []
  const confirmed: string[] = []
  const session: SyncSession = {
    cloudPendingDeletes: () => opts.pending ?? [],
    cloudConfirmDeleted: (id) => { confirmed.push(id) },
    cloudApplyPull: async (json) => { applied.push(json); return { error: '', unresolved: opts.unresolved ?? [] } },
    cloudDirtyPresets: () => opts.dirty ?? [],
    cloudPushPayload: (_kind, name) => JSON.stringify({ content: { name, type: 'filament', version: '2.0.0.0' }, updated_time: 100, setting_id: '', sync_info: '' }),
    cloudMarkSynced: (_kind, name, settingId, updatedTime, syncInfo) => { marks.push({ name, settingId, updatedTime, syncInfo }) },
  }
  return { session, marks, applied, confirmed }
}

function fakeApi(push: (call: { id: string; original?: number; force?: boolean }) => PushResult) {
  const calls: Array<{ id: string; original?: number; force?: boolean }> = []
  const removed: string[] = []
  const api: CloudApi = {
    health: async () => true,
    pull: async () => ({ upserts: [{ id: 'x', name: 'Remote', updated_time: 5, content: { name: 'Remote' } }], deletes: [] }),
    push: async (id, _name, _content, original, force) => { const c = { id, original, force }; calls.push(c); return push(c) },
    remove: async (id) => { removed.push(id); return true },
  }
  return { api, calls, removed }
}

const dirty = (name: string, settingId = '', syncInfo = ''): CloudPresetState => ({ kind: 'filament', name, settingId, baseId: '', syncInfo, updatedTime: 100 })

describe('runSync', () => {
  it('confirms deletions, applies the full pull, then pushes never-synced presets with a deterministic id', async () => {
    const s = fakeSession({ dirty: [dirty('Grey PLA')], pending: [{ settingId: 'dead', infoPath: '/x.info' }] })
    const a = fakeApi(() => ({ kind: 'ok', updatedTime: 200 }))
    const report = await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets: async () => undefined })
    expect(a.removed).toEqual(['dead'])
    expect(s.confirmed).toEqual(['dead'])
    expect(JSON.parse(s.applied[0]!)).toHaveLength(1)
    expect(a.calls[0]).toEqual({ id: settingIdFor(USER, 'Grey PLA'), original: undefined, force: undefined })
    expect(s.marks).toEqual([{ name: 'Grey PLA', settingId: settingIdFor(USER, 'Grey PLA'), updatedTime: 200, syncInfo: '' }])
    expect(report).toMatchObject({ deleted: 1, pulled: 1, pushed: 1, errors: [] })
  })

  it('sends the version check for updates and retries once with the server time on a conflict', async () => {
    const s = fakeSession({ dirty: [dirty('Grey PLA', 'sid', 'update')] })
    let n = 0
    const a = fakeApi(() => (n++ === 0 ? { kind: 'conflict', server: { id: 'sid', name: 'Grey PLA', updatedTime: 150 } } : { kind: 'ok', updatedTime: 160 }))
    const report = await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets: async () => undefined })
    expect(a.calls.map((c) => c.original)).toEqual([100, 150])
    expect(s.marks.at(-1)).toEqual({ name: 'Grey PLA', settingId: 'sid', updatedTime: 160, syncInfo: '' })
    expect(report.pushed).toBe(1)
  })

  it('recreates a preset the server deleted with a force push', async () => {
    const s = fakeSession({ dirty: [dirty('Grey PLA', 'sid', 'update')] })
    let n = 0
    const a = fakeApi(() => (n++ === 0 ? { kind: 'conflict', server: null } : { kind: 'ok', updatedTime: 300 }))
    await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets: async () => undefined })
    expect(a.calls[1]).toMatchObject({ id: 'sid', force: true })
    expect(s.marks.at(-1)?.syncInfo).toBe('')
  })

  it('marks oversized presets will_not_sync and stops creating after the account limit', async () => {
    const s = fakeSession({ dirty: [dirty('Big'), dirty('Second'), dirty('Third')] })
    const a = fakeApi((c) => (c.id === settingIdFor(USER, 'Big') ? { kind: 'too_large' } : { kind: 'limit' }))
    const report = await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets: async () => undefined })
    expect(report.notSynced).toEqual(['Big'])
    expect(report.limitHit).toBe(true)
    expect(a.calls).toHaveLength(2) // Third is skipped once the limit is hit
    expect(s.marks.find((m) => m.name === 'Big')?.syncInfo).toBe('will_not_sync')
    expect(s.marks.find((m) => m.name === 'Second')?.syncInfo).toBe('hold')
  })

  it('does not push when the pull failed', async () => {
    const s = fakeSession({ dirty: [dirty('Grey PLA')] })
    const a = fakeApi(() => ({ kind: 'ok', updatedTime: 1 }))
    a.api.pull = async () => { throw new Error('boom') }
    const report = await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets: async () => undefined })
    expect(a.calls).toHaveLength(0)
    expect(report.errors[0]).toContain('Pull failed')
  })

  it('installs a missing vendor once and re-applies the pull', async () => {
    const missing: SkippedPreset = { name: 'Orphan', reason: 'missing_parent', inherits: 'Qidi X-Max 3 0.4 nozzle', vendorHint: 'Qidi' }
    const s = fakeSession({ unresolved: [missing] })
    const a = fakeApi(() => ({ kind: 'ok', updatedTime: 1 }))
    const installVendorFor = vi.fn(async () => true)
    const reloadPresets = vi.fn(async () => undefined)
    await runSync({ session: s.session, api: a.api, userId: USER, reloadPresets, installVendorFor })
    expect(installVendorFor).toHaveBeenCalledWith(missing)
    expect(reloadPresets).toHaveBeenCalledTimes(1)
    expect(s.applied).toHaveLength(2)
  })
})
