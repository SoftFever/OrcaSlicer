import { create } from 'zustand'

import { storedCloudUser, type CloudUser } from './auth'
import { storage } from '../storage'

// What the Account screen and the header show about sync. `enabled` and
// `lastSyncAt` persist; the rest is per launch.

export type SyncStatus = 'idle' | 'syncing' | 'offline' | 'error'

export interface CloudState {
  user: CloudUser | null
  /** Auto sync on foreground and after edits. Manual "Sync now" always works. */
  enabled: boolean
  status: SyncStatus
  /** Unix ms of the last successful sync, 0 when never. */
  lastSyncAt: number
  lastError: string
  /** Presets the server refused for size; shown so the user knows they stay local. */
  notSynced: string[]
  setUser: (user: CloudUser | null) => void
  setEnabled: (enabled: boolean) => void
  setStatus: (status: SyncStatus, error?: string) => void
  markSynced: (notSynced: string[]) => void
}

const ENABLED_KEY = 'cloud:v1:enabled'
const LAST_SYNC_KEY = 'cloud:v1:lastSync'

export const useCloudStore = create<CloudState>((set) => ({
  user: storedCloudUser(),
  enabled: storage.getBoolean(ENABLED_KEY) ?? true,
  status: 'idle',
  lastSyncAt: storage.getNumber(LAST_SYNC_KEY) ?? 0,
  lastError: '',
  notSynced: [],
  setUser: (user) => set({ user, lastError: '', notSynced: [] }),
  setEnabled: (enabled) => {
    storage.set(ENABLED_KEY, enabled)
    set({ enabled })
  },
  setStatus: (status, error = '') => set({ status, lastError: error }),
  markSynced: (notSynced) => {
    const now = Date.now()
    storage.set(LAST_SYNC_KEY, now)
    set({ status: 'idle', lastError: '', lastSyncAt: now, notSynced })
  },
}))
