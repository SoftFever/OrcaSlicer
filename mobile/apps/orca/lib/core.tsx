import { getOrcaCore, type OrcaSession } from 'react-native-orca-core'
import React, { createContext, useContext, useEffect, useMemo, useState } from 'react'

import { storedCloudUserId, type CloudUser } from './cloud/auth'
import { useCloudStore } from './cloud/store'
import { adoptLocalPresets, registerSyncContext, syncIfStale, syncNow } from './cloud/sync'
import { deviceLanguage, setLanguage } from './i18n'
import {
  dataDirectory,
  ensureDirectories,
  ensureFilamentLibrary,
  installedVendors,
  nativePath,
  ORCA_FILAMENT_LIBRARY,
  resourcesDirectory,
} from './profiles'

// One session for the app. The native core is initialized once with the app's
// directories, then a session is created and its presets loaded.

export interface CoreState {
  version: string
  session: OrcaSession | null
  /** Error text from loading presets, empty when fine. */
  presetError: string
  ready: boolean
  /** Re-reads the profiles from disk, e.g. after installing a vendor. */
  reloadPresets: () => Promise<void>
  /**
   * Switches the user preset folder to this Orca Cloud account (null = signed
   * out), reloads presets and, on sign-in, runs the first sync. The first sign-in
   * on a device with local presets but an empty account adopts those presets.
   */
  setCloudUser: (user: CloudUser | null) => Promise<void>
}

const CoreContext = createContext<CoreState | null>(null)

export function CoreProvider({ children }: { children: React.ReactNode }): React.JSX.Element {
  const core = useMemo(() => getOrcaCore(), [])
  const [session, setSession] = useState<OrcaSession | null>(null)
  const [presetError, setPresetError] = useState('')
  const [ready, setReady] = useState(false)

  useEffect(() => {
    setLanguage(deviceLanguage())
    ensureDirectories()
    core.initialize(nativePath(resourcesDirectory()), nativePath(dataDirectory()))
    const created = core.createSession()
    // Presets live under data/user/<cloud user id>; tell the core before the first load.
    created.setCloudUser(storedCloudUserId())
    setSession(created)

    // Vendor filament presets inherit `@base` profiles from the shared
    // OrcaFilamentLibrary bundle; loading a vendor without it always
    // fails at parse time (see profiles.ts). Every install adds the
    // library, but earlier builds did not — self-heal here so users
    // who installed BBL / Prusa / etc. before this landed get it too.
    const needsRepair =
      installedVendors().some((v) => v !== ORCA_FILAMENT_LIBRARY) &&
      !installedVendors().includes(ORCA_FILAMENT_LIBRARY)
    const boot = needsRepair
      ? ensureFilamentLibrary().catch(() => undefined)
      : Promise.resolve()

    void boot
      .then(() => created.loadPresets())
      .then((error) => setPresetError(error))
      .catch((error: unknown) => setPresetError(String(error)))
      .finally(() => setReady(true))
  }, [core])

  // The sync engine needs the live session; the home screen adds its refresh
  // callback on top. First sync on launch happens once presets are in.
  useEffect(() => {
    if (!ready || session === null) return
    registerSyncContext({ session, reloadPresets: () => session.reloadPresets().then((e) => setPresetError(e)) })
    syncIfStale()
    return () => registerSyncContext(null)
  }, [ready, session])

  const value = useMemo<CoreState>(
    () => ({
      version: core.version,
      session,
      presetError,
      ready,
      reloadPresets: async () => {
        if (session === null) return
        // reloadPresets wipes data/system before rereading so a fresh
        // vendor install (or a reinstall over a corrupt file) really
        // takes effect, unlike loadPresets which skips a same-version
        // reinstall and leaves the mirror as-is. A native throw (bad
        // filesystem state, malformed profile) surfaces as an error
        // banner rather than an unhandled rejection.
        try {
          setPresetError(await session.reloadPresets())
        } catch (e) {
          setPresetError(String(e))
        }
      },
      setCloudUser: async (user) => {
        if (session === null) return
        const id = user?.id ?? ''
        session.setCloudUser(id)
        useCloudStore.getState().setUser(user)
        try {
          setPresetError(await session.reloadPresets())
        } catch (e) {
          setPresetError(String(e))
        }
        if (user === null) return
        const report = await syncNow()
        if (report !== null && report.pulled === 0 && (await adoptLocalPresets(id, false))) {
          try {
            setPresetError(await session.reloadPresets())
          } catch (e) {
            setPresetError(String(e))
          }
          await syncNow()
        }
      },
    }),
    [core, session, presetError, ready]
  )

  return <CoreContext.Provider value={value}>{children}</CoreContext.Provider>
}

export function useCore(): CoreState {
  const state = useContext(CoreContext)
  if (state === null) {
    throw new Error('useCore() needs a CoreProvider above it')
  }
  return state
}

/** The session, once the provider created it. Screens render nothing until then. */
export function useSession(): OrcaSession | null {
  return useCore().session
}
