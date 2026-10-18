import type { OrcaSession } from 'react-native-orca-core'
import { create } from 'zustand'

import { notify } from './notifications'
import {
  hasPrinterModelFiles,
  installVendor,
  installedVendors,
  removeVendor,
  type AvailablePrinter,
} from './profiles'
import { queryClient, queryKeys } from './queries'

export type DownloadState = 'downloading' | 'installing' | 'done' | 'error' | 'cancelled'

export interface Download {
  id: string
  vendor: string
  vendorName: string
  model: string
  state: DownloadState
  done: number
  total: number
  error?: string
  /** For cancellation: the abort controller and a promise that resolves when the task ends. */
  abort: AbortController
  finished: Promise<void>
}

interface StoreState {
  byId: Record<string, Download>
  /** Kicks off an install in the background. Returns immediately; progress lives in the store. */
  install: (
    printer: AvailablePrinter,
    ctx: {
      session: OrcaSession | null
      reloadPresets: () => Promise<void>
      onCompleted?: (d: Download) => void
    },
  ) => Download
  /** Marks the download as cancelled; the vendor bundle is removed if partly installed. */
  cancel: (id: string) => void
  /** Removes a terminal download from the store; safe on any id. */
  clear: (id: string) => void
}

/**
 * Shared downloads store. Installations run to completion even when the screen
 * that started them unmounts; screens observe progress via `useDownloadsStore`.
 * On success we invalidate the installed-vendors query and select the picked
 * printer preset — the same steps the vendors screen used to do inline.
 */
export const useDownloadsStore = create<StoreState>((set, get) => {
  const setDownload = (id: string, patch: Partial<Download>) =>
    set((s) => {
      const current = s.byId[id]
      if (current === undefined) return s
      return { byId: { ...s.byId, [id]: { ...current, ...patch } } }
    })

  return {
    byId: {},

    install(printer, ctx) {
      const id = `${printer.vendor}::${printer.model}`
      const existing = get().byId[id]
      if (existing !== undefined && existing.state !== 'error' && existing.state !== 'cancelled') {
        return existing
      }

      const abort = new AbortController()
      let resolveFinished: () => void = () => {}
      const finished = new Promise<void>((res) => (resolveFinished = res))

      const download: Download = {
        id,
        vendor: printer.vendor,
        vendorName: printer.vendorName,
        model: printer.model,
        state: 'downloading',
        done: 0,
        total: 0,
        abort,
        finished,
      }
      set((s) => ({ byId: { ...s.byId, [id]: download } }))

      const run = async (): Promise<void> => {
        // Skip the download only when this exact printer's files are already on
        // disk. Just checking whether the vendor folder exists is not enough —
        // a previous unscoped install (or a scoped install for a different
        // model of the same vendor) would let us "succeed" without ever
        // downloading the newly-picked model's presets, and C++ would then
        // fail to find them at load time.
        const alreadyHere =
          installedVendors().includes(printer.vendor) &&
          hasPrinterModelFiles(printer.vendor, printer.model)
        try {
          if (!alreadyHere) {
            // Wipe any partial install so a re-run over a bad state starts clean.
            removeVendor(printer.vendor)
            await installVendor(
              printer.vendor,
              (done, total) => setDownload(id, { done, total }),
              undefined,
              abort.signal,
              printer.model,
            )
          }
          if (abort.signal.aborted) throw new Error('cancelled')
          setDownload(id, { state: 'installing' })
          await queryClient.invalidateQueries({ queryKey: queryKeys.installedVendors })
          await ctx.reloadPresets()
          // Best-effort: pick the printer preset that matches the model.
          try {
            if (ctx.session !== null) {
              const presets = ctx.session.presets('printer')
              const exact = presets.find(
                (c) => c.name === printer.model && c.vendor === printer.vendor,
              )
              const withNozzle = presets.find(
                (c) => c.vendor === printer.vendor && c.name.startsWith(`${printer.model} `),
              )
              const pick = exact ?? withNozzle
              if (pick !== undefined) ctx.session.selectPreset('printer', pick.name)
            }
          } catch {
            /* selection is optional */
          }
          setDownload(id, { state: 'done' })
          void notify(
            `${printer.model} ready`,
            `Installed from ${printer.vendorName}. Tap to slice.`,
          )
          ctx.onCompleted?.(get().byId[id] as Download)
        } catch (e) {
          const message = String(e)
          if (message.includes('cancelled')) {
            removeVendor(printer.vendor)
            void queryClient.invalidateQueries({ queryKey: queryKeys.installedVendors })
            setDownload(id, { state: 'cancelled' })
          } else {
            removeVendor(printer.vendor)
            void queryClient.invalidateQueries({ queryKey: queryKeys.installedVendors })
            setDownload(id, { state: 'error', error: message })
          }
        } finally {
          resolveFinished()
        }
      }

      void run()
      return download
    },

    cancel(id) {
      const d = get().byId[id]
      if (d === undefined) return
      d.abort.abort()
    },

    clear(id) {
      set((s) => {
        if (!(id in s.byId)) return s
        const { [id]: _dropped, ...rest } = s.byId
        return { byId: rest }
      })
    },
  }
})

/** In-flight downloads (not `done` / `error` / `cancelled`). */
export function selectActive(state: { byId: Record<string, Download> }): Download[] {
  return Object.values(state.byId).filter(
    (d) => d.state === 'downloading' || d.state === 'installing',
  )
}
