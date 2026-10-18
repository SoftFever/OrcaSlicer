import * as Network from 'expo-network'
import { Directory, File } from 'expo-file-system'
import type { OrcaSession, SkippedPreset } from 'react-native-orca-core'

import { cloudApi, type CloudApi } from './api'
import { storedCloudUserId } from './auth'
import { useCloudStore } from './store'
import { settingIdFor } from './uuid5'
import { dataDirectory, installedVendors, installVendor } from '../profiles'
import { showToast } from '../toast'

// The sync engine. One run is: confirm pending deletions, full pull, push every
// dirty preset. The core does the preset work (lib/cloud is HTTP and policy);
// see Session::cloud_* in src/mobile/core/Session.hpp and the desktop's
// GUI_App::sync_preset, whose outcomes this mirrors.

export type SyncSession = Pick<
  OrcaSession,
  'cloudPendingDeletes' | 'cloudConfirmDeleted' | 'cloudApplyPull' | 'cloudDirtyPresets' | 'cloudPushPayload' | 'cloudMarkSynced'
>

export interface SyncDeps {
  session: SyncSession
  api: CloudApi
  userId: string
  /** Re-reads presets after a pull touched the vendor set. */
  reloadPresets: () => Promise<void>
  /** Installs the vendor a pulled preset needs; resolves true when it did. */
  installVendorFor?: (missing: SkippedPreset) => Promise<boolean>
}

export interface SyncReport {
  deleted: number
  pulled: number
  pushed: number
  /** Presets the server refused for size; they stay local. */
  notSynced: string[]
  /** Presets whose parent profile is not installed. */
  unresolved: string[]
  errors: string[]
  limitHit: boolean
}

interface PushPayload {
  content: Record<string, string>
  updated_time: number
  setting_id: string
  sync_info: string
}

export async function runSync(deps: SyncDeps): Promise<SyncReport> {
  const { session, api, userId } = deps
  const report: SyncReport = { deleted: 0, pulled: 0, pushed: 0, notSynced: [], unresolved: [], errors: [], limitHit: false }

  // 1. Deletions the server has not confirmed.
  for (const pending of session.cloudPendingDeletes()) {
    try {
      if (await api.remove(pending.settingId)) {
        session.cloudConfirmDeleted(pending.settingId)
        report.deleted += 1
      }
    } catch (e) {
      report.errors.push(`Delete failed: ${String(e)}`)
    }
  }

  // 2. Full pull. Presets whose parent vendor is missing get one install attempt.
  try {
    const pull = await api.pull()
    let applied = await session.cloudApplyPull(JSON.stringify(pull.upserts))
    if (applied.error !== '') report.errors.push(applied.error)
    const installable = applied.unresolved.filter((u) => u.vendorHint !== '' || u.inherits !== '')
    if (installable.length > 0 && deps.installVendorFor !== undefined) {
      let installed = false
      for (const u of installable) {
        if (await deps.installVendorFor(u)) { installed = true; break }
      }
      if (installed) {
        await deps.reloadPresets()
        applied = await session.cloudApplyPull(JSON.stringify(pull.upserts))
      }
    }
    report.unresolved = applied.unresolved.map((u) => u.name)
    report.pulled = pull.upserts.length
  } catch (e) {
    report.errors.push(`Pull failed: ${String(e)}`)
    return report // pushing on top of a failed pull risks overwriting newer server copies
  }

  // 3. Push what changed here.
  for (const state of session.cloudDirtyPresets()) {
    if (state.syncInfo === 'will_not_sync') continue
    const raw = session.cloudPushPayload(state.kind, state.name)
    if (raw === '') continue
    let payload: PushPayload
    try {
      payload = JSON.parse(raw) as PushPayload
    } catch {
      continue
    }
    const id = state.settingId !== '' ? state.settingId : settingIdFor(userId, state.name)
    const isUpdate = state.settingId !== '' && state.syncInfo !== 'create'
    if (!isUpdate && report.limitHit) continue

    let result = await api.push(id, state.name, payload.content, isUpdate ? payload.updated_time : undefined)
    if (result.kind === 'conflict') {
      if (result.server !== null) {
        // The server has a newer copy. The desktop takes the server's time and
        // pushes again, so the last writer wins; do the same, once.
        session.cloudMarkSynced(state.kind, state.name, id, result.server.updatedTime, 'update')
        result = await api.push(id, state.name, payload.content, result.server.updatedTime)
      } else {
        // Deleted on the server since we last saw it: recreate it in place.
        result = await api.push(id, state.name, payload.content, undefined, true)
      }
    }
    switch (result.kind) {
      case 'ok':
        session.cloudMarkSynced(state.kind, state.name, id, result.updatedTime, '')
        report.pushed += 1
        break
      case 'too_large':
        session.cloudMarkSynced(state.kind, state.name, state.settingId, 0, 'will_not_sync')
        report.notSynced.push(state.name)
        break
      case 'limit':
        report.limitHit = true
        session.cloudMarkSynced(state.kind, state.name, state.settingId, 0, 'hold')
        break
      case 'conflict':
        session.cloudMarkSynced(state.kind, state.name, id, result.server?.updatedTime ?? 0, 'update')
        report.errors.push(`${state.name}: still conflicting with the server copy`)
        break
      default:
        session.cloudMarkSynced(state.kind, state.name, state.settingId, 0, 'hold')
        report.errors.push(`${state.name}: ${result.message}`)
    }
  }
  return report
}

// ---- app-level wiring ------------------------------------------------------

export interface SyncContext {
  session: OrcaSession
  reloadPresets: () => Promise<void>
  /** Re-reads selections into React state after presets changed underneath. */
  onChanged?: () => void
}

let context: SyncContext | null = null
let inflight: Promise<SyncReport | null> | null = null
let debounce: ReturnType<typeof setTimeout> | null = null

/** The core provider registers the live session here once it exists. */
export function registerSyncContext(ctx: SyncContext | null): void {
  context = ctx
}

/** Installs the vendor a pulled preset needs, from the vendor hint or a model name in `inherits`. */
async function installVendorFor(missing: SkippedPreset): Promise<boolean> {
  const vendor = missing.vendorHint
  if (vendor === '') return false
  try {
    showToast(`Downloading ${vendor} profiles for ${missing.name}…`, 'info')
    await installVendor(vendor)
    return installedVendors().includes(vendor)
  } catch {
    return false
  }
}

/**
 * Runs a sync now, unless one is running (then returns its result). Resolves
 * to null when signed out or offline. Status and errors go to the cloud store.
 */
export function syncNow(): Promise<SyncReport | null> {
  if (inflight !== null) return inflight
  inflight = (async () => {
    const ctx = context
    const userId = storedCloudUserId()
    const store = useCloudStore.getState()
    if (ctx === null || userId === '') return null
    try {
      const net = await Network.getNetworkStateAsync()
      if (net.isInternetReachable === false) {
        store.setStatus('offline')
        return null
      }
    } catch { /* unknown reachability: try anyway */ }
    store.setStatus('syncing')
    try {
      const report = await runSync({ session: ctx.session, api: cloudApi, userId, reloadPresets: ctx.reloadPresets, installVendorFor })
      if (report.errors.length > 0) {
        useCloudStore.getState().setStatus('error', report.errors[0]!)
      } else {
        useCloudStore.getState().markSynced(report.notSynced)
      }
      if (report.pulled > 0 || report.deleted > 0) ctx.onChanged?.()
      if (report.limitHit) showToast('Orca Cloud preset limit reached; new presets stay on this device', 'error')
      if (report.notSynced.length > 0) showToast(`${report.notSynced.length} preset(s) are too large to sync`, 'info')
      return report
    } catch (e) {
      useCloudStore.getState().setStatus('error', String(e))
      return null
    } finally {
      inflight = null
    }
  })()
  return inflight
}

/** Sync soon, coalescing bursts of edits. */
export function scheduleSync(delayMs = 2000): void {
  if (!useCloudStore.getState().enabled || storedCloudUserId() === '') return
  if (debounce !== null) clearTimeout(debounce)
  debounce = setTimeout(() => {
    debounce = null
    void syncNow()
  }, delayMs)
}

/** Sync when the last one is older than `maxAgeMs`; used on foreground. */
export function syncIfStale(maxAgeMs = 5 * 60 * 1000): void {
  const s = useCloudStore.getState()
  if (!s.enabled || storedCloudUserId() === '') return
  if (Date.now() - s.lastSyncAt < maxAgeMs) return
  void syncNow()
}

/**
 * First sign-in on this device: when the account has no presets yet and the
 * device has local ones under data/user/default, adopt them for the account.
 * Mirrors the desktop's maybe_migrate_user_presets_on_login. Returns true when
 * files moved; the caller reloads presets.
 */
export async function adoptLocalPresets(userId: string, cloudHasPresets: boolean): Promise<boolean> {
  if (userId === '' || cloudHasPresets) return false
  const userRoot = new Directory(dataDirectory(), 'user')
  const target = new Directory(userRoot, userId)
  const source = new Directory(userRoot, 'default')
  if (!source.exists) return false
  let moved = false
  for (const kind of ['machine', 'filament', 'process']) {
    const from = new Directory(source, kind)
    if (!from.exists) continue
    const files = from.list().filter((e): e is File => e instanceof File)
    if (files.length === 0) continue
    const to = new Directory(target, kind)
    if (!to.exists) to.create({ intermediates: true })
    for (const f of files) {
      const dest = new File(to, f.name)
      if (dest.exists) continue
      try {
        f.move(dest)
        moved = true
      } catch { /* leave it; the user can import by file */ }
    }
  }
  return moved
}
