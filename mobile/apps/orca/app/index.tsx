import NativeSegmentedControl from '@react-native-segmented-control/segmented-control'
import Slider from '@react-native-community/slider'
import * as DocumentPicker from 'expo-document-picker'
import { Directory, File, Paths } from 'expo-file-system'
import * as Linking from 'expo-linking'
import { Stack, useRouter } from 'expo-router'
import * as Sharing from 'expo-sharing'
import React, { useCallback, useEffect, useRef, useState } from 'react'
import { ActivityIndicator, Alert, AppState, Pressable, ScrollView, Text, useWindowDimensions, View } from 'react-native'
import { callback } from 'react-native-nitro-modules'
import { OrcaViewport, type ObjectInfo, type OrcaViewportRef, type PlateInfo, type PresetInfo, type PresetKind, type SliceResult, type SliceStatistics } from 'react-native-orca-core'

import { Alert as InlineAlert } from '@/ui/Alert'
import { Button } from '@/ui/Button'
import { Card, Row } from '@/ui/Card'
import { DevicePanel } from '@/ui/DevicePanel'
import { ObjectDetailSheet } from '@/ui/ObjectDetailSheet'
import { ObjectToolbar } from '@/ui/ObjectToolbar'
import { OnboardingSheet } from '@/ui/OnboardingSheet'
import { PresetPicker } from '@/ui/PresetPicker'
import { PresetRow } from '@/ui/PresetRow'
import { ProgressBar } from '@/ui/ProgressBar'
import { useCore } from '@/lib/core'
import { haptic } from '@/lib/haptics'
import { t } from '@/lib/i18n'
import { clientFor, loadPrinters, type PrinterHost } from '@/lib/printers'
import { history, printHistory, projectHistory, pushHistory, pushPrint, pushProject, removeHistory, removePrint, removeProject, type HistoryEntry, type PrintEntry, type ProjectEntry } from '@/lib/history'
import { AUTOSAVE_FILE, readLastPane, readRememberedPresets, writeLastPane, writeRememberedPresets } from '@/lib/prefs'
import { filamentColorAt } from '@/lib/filamentColor'
import { hasOnboarded } from '@/lib/onboarding'
import { relativeTime } from '@/lib/time'
import { showToast } from '@/lib/toast'
import { registerSyncContext, syncIfStale } from '@/lib/cloud/sync'
import { classifyOpenedUrl } from '@/lib/openFile'
import { importPresetFile } from '@/lib/presetFiles'
import { installedVendors, nativePath, removePresetFile, removeVendor } from '@/lib/profiles'
import { queryClient, queryKeys } from '@/lib/queries'
import { useViewportStore } from '@/lib/viewport'
import { colors, useThemeColors } from '@/lib/theme'
import { EmptyState } from '@/ui/EmptyState'
import { Icon } from '@/ui/Icon'

const PRESET_KINDS: Array<{ kind: PresetKind; label: string }> = [
  { kind: 'printer', label: 'Printer' },
  { kind: 'filament', label: 'Filament' },
  { kind: 'process', label: 'Process' },
]

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600)
  const m = Math.round((seconds % 3600) / 60)
  return h > 0 ? `${h} h ${m} min` : `${m} min`
}

// The core surfaces preset errors as one long string. Two shapes we know about:
//   1) "Found duplicated settings in vendor Qidi's json file lists: …" — pull
//      the vendor from the possessive marker.
//   2) "Failed loading configuration file /.../data/system/Qidi/machine/… :
//      [json.exception.parse_error…]" — a bad file in one vendor's tree. Pull
//      the vendor from the `system/<Vendor>/` path segment.
// Both branches let the alert offer a one-tap Remove for the guilty vendor.
function offendingVendorIn(error: string): string | null {
  const byPossessive = /vendor\s+([A-Za-z0-9][A-Za-z0-9 _-]*?)['`’]s\b/.exec(error)
  if (byPossessive?.[1] !== undefined) return byPossessive[1].trim()
  const byPath = /\/(?:system|user)\/([A-Za-z0-9][A-Za-z0-9 _-]*?)\/(?:machine|filament|process)\//.exec(error)
  if (byPath?.[1] !== undefined) return byPath[1].trim()
  return null
}

/** Pull the exact preset file path out of a "Failed loading configuration file
 * /var/…/Qidi/machine/Qidi Q1 Pro.json: [json.exception…]" style error, when
 * present. Lets the recovery UI offer to delete just the bad file rather than
 * blow away the whole vendor. */
function offendingFileIn(error: string): string | null {
  const m = /Failed loading configuration file\s+([^:]+\.json)\s*:/.exec(error)
  return m?.[1]?.trim() ?? null
}

export default function HomeScreen(): React.JSX.Element {
  const router = useRouter()
  const { version, session, presetError, ready, reloadPresets } = useCore()
  // On iPad we give the viewport a much bigger canvas — h-[360px] leaves half the
  // screen empty in landscape. Scales with height so the cards below still get
  // room to breathe. Portrait (or iPhone) keeps the compact 360 / 440 sizing.
  const { width: winWidth, height: winHeight } = useWindowDimensions()
  const theme = useThemeColors()
  const isWideLandscape = winWidth >= 900 && winWidth > winHeight
  const [removing, setRemoving] = useState(false)
  const [objects, setObjects] = useState<ObjectInfo[]>([])
  const [selected, setSelected] = useState<Record<PresetKind, string>>({ printer: '', filament: '', process: '' })
  const [progress, setProgress] = useState<{ percent: number; message: string } | null>(null)
  const [result, setResult] = useState<SliceResult | null>(null)
  const [stats, setStats] = useState<SliceStatistics | null>(null)
  const [gcodePath, setGcodePath] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  // Mirror `busy` into a ref so the AppState listener (registered once) can
  // gate itself without capturing a stale value between renders.
  const busyRef = useRef(false)
  useEffect(() => { busyRef.current = busy }, [busy])
  const [sending, setSending] = useState(false)
  const [printers, setPrinters] = useState<PrinterHost[]>([])
  const viewMode = useViewportStore((s) => s.mode)
  const setViewMode = useViewportStore((s) => s.setMode)
  const minLayer = useViewportStore((s) => s.minLayer)
  const setMinLayer = useViewportStore((s) => s.setMinLayer)
  const maxLayer = useViewportStore((s) => s.maxLayer)
  const setMaxLayer = useViewportStore((s) => s.setMaxLayer)
  const maxTime = useViewportStore((s) => s.maxTime)
  const setMaxTime = useViewportStore((s) => s.setMaxTime)
  const showTravels = useViewportStore((s) => s.showTravels)
  const setShowTravels = useViewportStore((s) => s.setShowTravels)
  const showRetractions = useViewportStore((s) => s.showRetractions)
  const setShowRetractions = useViewportStore((s) => s.setShowRetractions)
  const showWipes = useViewportStore((s) => s.showWipes)
  const setShowWipes = useViewportStore((s) => s.setShowWipes)
  const colorMode = useViewportStore((s) => s.colorMode)
  const setColorMode = useViewportStore((s) => s.setColorMode)
  const pane = useViewportStore((s) => s.pane)
  const setPane = useViewportStore((s) => s.setPane)
  const activeDeviceId = useViewportStore((s) => s.activeDeviceId)
  const setActiveDeviceId = useViewportStore((s) => s.setActiveDeviceId)
  const focusDevice = useViewportStore((s) => s.focusDevice)
  const selectedId = useViewportStore((s) => s.selectedId)
  const setSelectedId = useViewportStore((s) => s.setSelectedId)
  const objectTool = useViewportStore((s) => s.objectTool)
  const setObjectTool = useViewportStore((s) => s.setObjectTool)
  // Nitro views expose native methods via the `hybridRef` prop, NOT through the
  // standard React `ref` — the latter is the RN HostComponent instance (measure /
  // setNativeProps only). The callback below fires once when the view mounts
  // and again if Nitro remounts it, so `.current` stays live for method calls.
  const viewportRef = useRef<OrcaViewportRef | null>(null)
  const [revision, setRevision] = useState(0)
  const [pickerKind, setPickerKind] = useState<PresetKind | null>(null)
  const [pickerPresets, setPickerPresets] = useState<PresetInfo[]>([])
  const [pastSlices, setPastSlices] = useState<HistoryEntry[]>([])
  const [pastProjects, setPastProjects] = useState<ProjectEntry[]>([])
  const [pastPrints, setPastPrints] = useState<PrintEntry[]>([])
  // Object detail sheet opens on the toolbar's ⋯ (or the Objects list row),
  // not automatically on selection — that would block the viewport and prevent
  // the drag-to-move / rotate / scale flows the toolbar unlocks.
  const [detailSheetOpen, setDetailSheetOpen] = useState(false)
  // Runs once on mount; if `onboarded` is unset in MMKV we show the welcome
  // overlay. Kept in local state so tapping "Skip" instantly hides it without a
  // re-render round-trip through storage.
  const [showOnboarding, setShowOnboarding] = useState(() => !hasOnboarded())
  // Cached filament color for slot 1 — session.option runs through the mutex, so
  // reading on every render (once per parent update) burns CPU during slice and
  // throws while the mutex is held. Refreshed alongside other session-derived
  // state in refresh(), then read cheaply from React state everywhere else.
  const [primaryFilamentColor, setPrimaryFilamentColor] = useState(colors.placeholder)
  // Cache undo/redo state so React does not read the getters mid-render — those go
  // through the session's mutex and throw during a long op (import, arrange, slice).
  const [undoState, setUndoState] = useState<{ canUndo: boolean; canRedo: boolean; undoLabel: string; redoLabel: string }>({
    canUndo: false,
    canRedo: false,
    undoLabel: '',
    redoLabel: '',
  })
  const [plates, setPlates] = useState<PlateInfo[]>([])

  const refresh = useCallback(() => {
    if (session === null) return
    try {
      setRevision((r) => r + 1)
      setObjects(session.objects())
      setSelected({
        printer: session.selectedPreset('printer'),
        filament: session.selectedPreset('filament'),
        process: session.selectedPreset('process'),
      })
      setUndoState({
        canUndo: session.canUndo,
        canRedo: session.canRedo,
        undoLabel: session.undoLabel(),
        redoLabel: session.redoLabel(),
      })
      setPlates(session.plateList())
      setPrimaryFilamentColor(filamentColorAt(session, 1))
    } catch {
      /* mutex briefly held; caller will refresh again */
    }
  }, [session])

  useEffect(() => {
    if (ready && session !== null) {
      // Restore the user's last picked printer / filament / process before the
      // first refresh so the pane opens with the same context they left it in.
      // Each selectPreset is best-effort: if the vendor was removed since, the
      // native call returns false and we silently fall back to whatever the core
      // decided at boot. Presets are shared across plates, so applying once is
      // enough regardless of the current plate.
      try {
        const remembered = readRememberedPresets()
        if (remembered.printer  !== null) session.selectPreset('printer',  remembered.printer)
        if (remembered.filament !== null) session.selectPreset('filament', remembered.filament)
        if (remembered.process  !== null) session.selectPreset('process',  remembered.process)
      } catch {
        /* mutex briefly held or a preset name went stale; refresh() sorts it */
      }
      // Restore the last pane, but never restore Preview on cold launch — the
      // slice results are ephemeral and Preview mode with no stats looks broken.
      const lastPane = readLastPane()
      if (lastPane === 'device' || lastPane === 'prepare') {
        setPane(lastPane)
        setViewMode('scene')
      }
      refresh()
      setPrinters(loadPrinters())
      setPastSlices(history())
      setPastProjects(projectHistory())
      setPastPrints(printHistory())
    }
  }, [ready, refresh, session, setPane, setViewMode])

  // Persist the pane whenever it changes so the next launch reopens it.
  useEffect(() => { writeLastPane(pane) }, [pane])

  // Autosave the project on background / inactive so a locked / home-swiped app
  // survives an OS reclaim with everything still on the plate. On first foreground
  // after a cold launch, if `autosave.3mf` exists we open it so the user picks
  // up where they left off — unless the app was launched with a file URL from
  // Files.app / a share sheet, in which case that URL wins and we skip the
  // autosave restore. `saveProject` here uses the multi-plate writer so all
  // plates come back — not just the current one.
  useEffect(() => {
    if (session === null) return
    let autosaved = false
    const autosavePath = `${nativePath(Paths.document)}/${AUTOSAVE_FILE}`

    void (async () => {
      try {
        // Check the cold-launch URL first — if the user is opening a specific
        // .3mf / .stl from Files.app, we don't want to preload autosave on top
        // of it and then have the URL-import append.
        const initialUrl = await Linking.getInitialURL()
        if (initialUrl !== null && initialUrl.length > 0) return
        const f = new File(Paths.document, AUTOSAVE_FILE)
        if (f.exists) {
          await session.openProject(autosavePath)
          refresh()
        }
      } catch { /* stale autosave; ignored */ }
    })()

    const sub = AppState.addEventListener('change', (state) => {
      if (state === 'background' || state === 'inactive') {
        // Don't touch the session while a long op holds the mutex — the write
        // would throw, and the plateList probe would too. The next background
        // event will run after busy clears.
        if (busyRef.current) return
        try {
          if (session.plateList().some((p) => p.objectCount > 0)) {
            void session.saveProject(autosavePath).catch(() => { /* best-effort */ })
            autosaved = true
          } else if (autosaved) {
            // Nothing to save now; clear the stale file so a subsequent launch
            // starts blank instead of restoring the previous session's plate.
            const f = new File(Paths.document, AUTOSAVE_FILE)
            if (f.exists) f.delete()
            autosaved = false
          }
        } catch { /* mutex or write failure; ignored */ }
      }
    })
    return () => sub.remove()
  }, [session, refresh])

  // Tap-to-pick via gesture-handler. A Pressable overlay would claim the touch on
  // touch-down and starve the Metal view's UIPan / UIPinch recognizers underneath;
  // gesture-handler's Tap composes with them so camera orbit / pan / pinch keep
  // working.
  const handleViewportTap = useCallback((x: number, y: number) => {
    const vp = viewportRef.current
    // Ignore taps outside Prepare (Preview / Device don't select). We used to gate
    // this on the JSX side by conditionally passing `undefined`, but Fabric's raw-
    // prop converter for the Nitro wrapped-callback shape throws when the value
    // toggles between an Object and undefined mid-lifecycle.
    if (session === null || vp === null || pane !== 'prepare') return
    // TEMP DIAG: surface each stage of the pick pipeline so we can tell where
    // the flow is breaking. Remove once selection is verified working.
    showToast(`tap ${Math.round(x)},${Math.round(y)}`, 'info')
    void (async () => {
      try {
        const ray = await vp.screenToRay(x, y)
        showToast(
          `ray o(${ray.originX.toFixed(0)},${ray.originY.toFixed(0)},${ray.originZ.toFixed(0)}) ` +
          `d(${ray.directionX.toFixed(2)},${ray.directionY.toFixed(2)},${ray.directionZ.toFixed(2)})`,
          'info',
        )
        const hit = session.pickAt(
          { x: ray.originX, y: ray.originY, z: ray.originZ },
          { x: ray.directionX, y: ray.directionY, z: ray.directionZ },
        )
        showToast(`hit id=${hit.hitId}`, hit.hitId !== 0 ? 'success' : 'error')
        if (hit.hitId !== 0) {
          haptic('select')
          setSelectedId(hit.hitId)
          void vp.setSelectedId(hit.hitId)
        } else {
          setSelectedId(null)
          void vp.setSelectedId(0)
        }
      } catch (e) {
        showToast(`pick failed: ${String(e)}`, 'error')
      }
    })()
  }, [session, setSelectedId, pane])

  // Push the current tool selection down to the native viewport whenever the
  // user picks Move / Rotate / Scale. `setObjectTool` on the HybridRef is a
  // simple assignment; safe to call whenever the ref becomes non-null too so a
  // remount picks up the last-picked tool.
  useEffect(() => {
    try { void viewportRef.current?.setObjectTool(objectTool) } catch { /* ref not ready */ }
  }, [objectTool])


  const openPicker = useCallback(
    (kind: PresetKind) => {
      if (session === null) return
      try {
        setPickerPresets(session.presets(kind))
        setPickerKind(kind)
      } catch (e) {
        showToast(`Could not load presets: ${String(e)}`, 'error')
      }
    },
    [session],
  )

  const pickPreset = useCallback(
    (name: string) => {
      if (session === null || pickerKind === null) return
      try {
        session.selectPreset(pickerKind, name)
        // Remember what the user picked so the next launch reopens with the same
        // printer / filament / process instead of resetting to the vendor default.
        writeRememberedPresets(
          pickerKind === 'printer'  ? { printer:  name }
          : pickerKind === 'filament' ? { filament: name }
          :                             { process:  name },
        )
      } catch (e) {
        showToast(`Could not select preset: ${String(e)}`, 'error')
      }
      setPickerKind(null)
      refresh()
    },
    [session, pickerKind, refresh],
  )

  const importPath = useCallback(async (path: string) => {
    if (session === null) return
    setBusy(true)
    try {
      // .3mf project files replace the whole project (multi-plate + config);
      // everything else appends to the current plate. Matches desktop's split
      // between "Open Project" and "Import".
      if (/\.3mf$/i.test(path)) {
        await session.openProject(path)
      } else {
        await session.importModel(path)
      }
      setResult(null)
      setStats(null)
      setGcodePath(null)
      refresh()
    } catch (error) {
      showToast(`Import failed: ${String(error)}`, 'error')
    } finally {
      setBusy(false)
    }
  }, [session, refresh])

  const importModel = useCallback(async () => {
    if (session === null) return
    // expo-document-picker hardcodes `asCopy: true` on iOS, so this URL already
    // points at a sandboxed copy we can read directly. `copyToCacheDirectory` would
    // copy it a second time into Caches/DocumentPicker, which nothing ever prunes —
    // that leaked a full duplicate per import (100 MB a go on a detailed mesh).
    const picked = await DocumentPicker.getDocumentAsync({ copyToCacheDirectory: false, multiple: false })
    const asset = picked.assets?.[0]
    if (picked.canceled || asset === undefined) return
    await importPath(decodeURIComponent(asset.uri.replace(/^file:\/\//, '')))
  }, [session, importPath])

  const importFromUrl = useCallback(async () => {
    if (session === null) return
    // Ask for the URL via a native prompt (Alert.prompt only exists on iOS, but
    // that is our only platform for now — the Android port lands in Phase 13).
    Alert.prompt?.('Import from URL', 'Paste a .3mf, .stl, .obj, .step or .amf link.', async (url) => {
      const trimmed = (url ?? '').trim()
      if (trimmed === '' || !/^https?:\/\//i.test(trimmed)) {
        showToast('Please paste a full http(s) URL.', 'info')
        return
      }
      setBusy(true)
      try {
        const response = await fetch(trimmed)
        if (!response.ok) throw new Error(`HTTP ${response.status}`)
        // Prefer the server's Content-Disposition filename when present; falls
        // back to the URL's last segment. We only accept known-good extensions
        // so a stray HTML redirect page does not get treated as a model.
        const cd = response.headers.get('Content-Disposition') ?? ''
        const cdMatch = /filename\*?=(?:UTF-8'')?"?([^";]+)"?/i.exec(cd)
        const nameFromUrl = decodeURIComponent(new URL(trimmed).pathname.split('/').pop() ?? '')
        let name = cdMatch?.[1] ?? nameFromUrl ?? `model-${Date.now()}.3mf`
        if (!/\.(3mf|stl|obj|step|stp|amf)$/i.test(name)) name = `${name}.3mf`
        const bytes = new Uint8Array(await response.arrayBuffer())
        const dir = new Directory(Paths.cache, 'downloads')
        if (!dir.exists) dir.create({ intermediates: true })
        const target = new File(dir, name)
        if (target.exists) target.delete()
        target.write(bytes)
        await importPath(nativePath(dir) + '/' + name)
      } catch (error) {
        showToast(`Import failed: ${String(error)}`, 'error')
      } finally {
        setBusy(false)
      }
    }, 'plain-text', 'https://')
  }, [session, importPath])

  const saveProject = useCallback(async (share: boolean) => {
    if (session === null) return
    if (objects.length === 0) {
      showToast('Nothing to save — import an object first.', 'info')
      return
    }
    setBusy(true)
    try {
      const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
      const name = objects.length === 1 ? objects[0]!.name.replace(/\.[^.]+$/, '') : 'project'
      const out = `${nativePath(Paths.document)}/${name}-${stamp}.3mf`
      const written = await session.saveProject(out)
      pushProject({
        path: written,
        name: `${name}-${stamp}`,
        objectCount: objects.length,
        objectNames: objects.map((o) => o.name),
        printer: selected.printer,
      })
      setPastProjects(projectHistory())
      // Silent save just confirms; share flow opens the sheet so the user can
      // AirDrop, Files, or push to a chat. Cancelling the sheet still leaves the
      // file in Documents where the Files app can grab it later.
      if (share && (await Sharing.isAvailableAsync())) {
        await Sharing.shareAsync(written, { mimeType: 'model/3mf', dialogTitle: 'Save project', UTI: 'io.github.angelohub.orcaslicer.3mf' })
      } else {
        showToast(`Saved ${name}-${stamp}.3mf to Files`, 'success')
      }
    } catch (error) {
      showToast(`Save failed: ${String(error)}`, 'error')
    } finally {
      setBusy(false)
    }
  }, [session, objects, selected.printer])

  // Cloud sync: the engine gets this screen's refresh so a pull that changed
  // presets re-reads the selections, and a foreground resume syncs when stale.
  useEffect(() => {
    if (session === null || !ready) return
    registerSyncContext({ session, reloadPresets, onChanged: refresh })
    const sub = AppState.addEventListener('change', (state) => {
      if (state === 'active') syncIfStale()
    })
    return () => sub.remove()
  }, [session, ready, reloadPresets, refresh])

  // Handle files opened from Files.app, a share sheet, or a deep link. On cold
  // launch `getInitialURL` returns the URL that opened the app; while running,
  // `Linking.addEventListener` fires for each subsequent open. We accept file://
  // URLs whose extension libslic3r can read; iOS delivers them into the app's
  // sandbox under `Documents/Inbox/…` after Info.plist declares the UTI.
  useEffect(() => {
    if (session === null) return
    const openFromUrl = (raw: string): void => {
      const opened = classifyOpenedUrl(raw)
      if (opened === null) return
      if (opened.kind === 'model') void importPath(opened.path)
      else void importPresetFile(opened.path, { session, reloadPresets, onChanged: refresh })
    }
    void Linking.getInitialURL().then((url) => { if (url !== null && url.length > 0) openFromUrl(url) })
    const sub = Linking.addEventListener('url', (ev) => { if (ev.url.length > 0) openFromUrl(ev.url) })
    return () => sub.remove()
  }, [session, importPath, reloadPresets, refresh])

  const slice = useCallback(async () => {
    if (session === null) return
    setBusy(true)
    setResult(null)
    setStats(null)
    setGcodePath(null)
    setProgress({ percent: 0, message: 'Starting' })
    try {
      // Progress callback runs on the TurboModule queue and marshals back into JS.
      // Three defensive shields:
      //   1) A per-integer-percent throttle — libslic3r emits many progress ticks
      //      per percent, and each round-trip triggers a React commit; without a
      //      dedup, reanimated's layout-animation pipeline queues a mount for
      //      every one and eventually throws bad_alloc.
      //   2) A rAF-style micro-defer via requestAnimationFrame so the setState
      //      runs on the JS thread's idle slot instead of racing the current
      //      commit that reanimated is walking.
      //   3) A try/catch around the setState — if React or reanimated throws
      //      mid-commit, we swallow it here rather than let the exception bubble
      //      back through the native invocation and abort() the whole process.
      let lastPercent = -1
      const sliced = await session.slice((p) => {
        const pct = Math.floor(p.percent)
        if (pct === lastPercent) return
        lastPercent = pct
        try {
          requestAnimationFrame(() => {
            try { setProgress({ percent: pct, message: p.message }) }
            catch (e) { console.warn('setProgress dropped:', e) }
          })
        } catch (e) { console.warn('progress raf dropped:', e) }
      })
      setResult(sliced)
      if (sliced.outcome === 'finished') {
        const out = new File(Paths.cache, 'plate_1.gcode')
        const written = await session.exportGCode(nativePath(out.parentDirectory) + '/plate_1.gcode')
        setGcodePath(written)
        const stats = session.statistics()
        setStats(stats)
        setMaxLayer(-1)
        setViewMode('preview')
        setPane('preview')
        setRevision((r) => r + 1)
        pushHistory({
          printer: selected.printer,
          filament: selected.filament,
          process: selected.process,
          objectNames: objects.map((o) => o.name),
          sourcePaths: [],
          stats,
          gcodePath: written,
        })
        setPastSlices(history())
      }
    } catch (error) {
      // `std::bad_alloc` bubbles up from libslic3r when the slicer can't
      // allocate — typically a too-large / too-detailed model, or the vendor
      // preset partly failed to load and left the print config in a state
      // that grows unboundedly. Say something actionable instead of dumping
      // the raw C++ error.
      const raw = String(error)
      const humanized = /bad_alloc|out of memory/i.test(raw)
        ? 'Slicing ran out of memory. Try scaling the model down, reducing wall / infill, or restarting the app.'
        : `Slicing failed: ${raw}`
      showToast(humanized, 'error')
    } finally {
      setProgress(null)
      setBusy(false)
    }
  }, [session])

  const share = useCallback(async () => {
    if (gcodePath === null) return
    await Sharing.shareAsync('file://' + gcodePath, { mimeType: 'text/x-gcode', dialogTitle: 'Send G-code' })
  }, [gcodePath])

  const sendTo = useCallback(
    async (host: PrinterHost, startPrint: boolean) => {
      if (gcodePath === null) return
      setSending(true)
      try {
        const filename = `${objects[0]?.name.replace(/\.[^.]+$/, '') ?? 'plate'}_${Date.now().toString(36)}.gcode`
        await clientFor(host).upload(host, { path: gcodePath, filename, startPrint })
        pushPrint({ hostId: host.id, hostName: host.name, filename, started: startPrint })
        setPastPrints(printHistory())
        showToast(`${startPrint ? 'Started' : 'Uploaded'} ${filename} on ${host.name}`, 'success')
      } catch (error) {
        showToast(`Could not send to ${host.name}: ${String(error)}`, 'error')
      } finally {
        setSending(false)
      }
    },
    [gcodePath, objects],
  )

  const send = useCallback(
    (startPrint: boolean) => {
      const current = loadPrinters()
      setPrinters(current)
      if (current.length === 0) {
        Alert.alert('No printers', 'Add a printer first.')
        return
      }
      if (current.length === 1 && current[0] !== undefined) {
        void sendTo(current[0], startPrint)
        return
      }
      Alert.alert('Send to', undefined, [
        ...current.map((host) => ({ text: host.name, onPress: () => void sendTo(host, startPrint) })),
        { text: 'Cancel', style: 'cancel' as const },
      ])
    },
    [sendTo],
  )

  if (!ready || session === null) {
    return (
      <View className="flex-1 items-center justify-center gap-3 bg-gray-50 dark:bg-black">
        <ActivityIndicator color={theme.tint} />
        <Text className="text-subhead text-gray-500 dark:text-gray-400">Loading profiles…</Text>
      </View>
    )
  }

  const vendors = installedVendors()
  // A preset is "chosen" only if it points at a real system/user profile;
  // the built-in "Default Printer" / "Default Filament" / "Default Setting"
  // fallbacks are placeholders that fail validation the moment you slice.
  const isDefaultPreset = (name: string) => name === '' || name.startsWith('Default ')
  const hasRealPrinter = !isDefaultPreset(selected.printer)
  const canSlice = objects.length > 0 && hasRealPrinter
  const stepValid = (l: number) => (l < 0 && stats !== null ? stats.layerCount : l)
  const layerLabel = stats === null ? '' : maxLayer < 0 ? `All ${stats.layerCount} layers` : `Layer ${maxLayer} of ${stats.layerCount}`

  const canPreview = stats !== null
  const paneIndex = pane === 'preview' ? 1 : pane === 'device' ? 2 : 0

  // Extracted so the wide-iPad layout can render the viewport in a left column
  // while the cards scroll on the right. In the narrow (single-column) layout it
  // sits at the top of the ScrollView as before.
  const viewportBlock = session === null ? null : (
    <View className="mb-6 gap-3">
      <View
        className="relative overflow-hidden rounded-3xl bg-white shadow-soft dark:bg-neutral-900"
        style={{ height: isWideLandscape ? Math.round(winHeight * 0.75) : (pane === 'preview' ? 440 : 360) }}
      >
        <OrcaViewport
          hybridRef={callback((ref) => { viewportRef.current = ref })}
          style={{ flex: 1 }}
          sessionId={session.id}
          mode={pane === 'preview' ? 'preview' : 'scene'}
          minLayer={minLayer}
          maxLayer={maxLayer}
          maxTime={maxTime}
          showTravels={showTravels}
          showRetractions={showRetractions}
          showWipes={showWipes}
          colorMode={colorMode}
          revision={revision}
          onTap={callback(handleViewportTap)}
        />
        <Pressable
          onPress={() => router.push('/fullscreen')}
          hitSlop={12}
          className="absolute right-3 top-3 rounded-full bg-neutral-900/80 px-3 py-2 active:opacity-70 dark:bg-white/20"
          accessibilityLabel="Enter fullscreen viewport"
        >
          <Text className="text-footnote font-semibold text-white">↗ Fullscreen</Text>
        </Pressable>
        {pane === 'prepare' ? (
          <View className="absolute left-3 top-3 flex-row gap-1 rounded-full bg-neutral-900/80 dark:bg-white/20">
            <Pressable
              onPress={() => { if (undoState.canUndo) { haptic('tap'); try { session.undo(); refresh() } catch { /* busy; ignored */ } } }}
              hitSlop={12}
              disabled={!undoState.canUndo}
              className={`rounded-l-full px-3 py-2 active:opacity-70 ${undoState.canUndo ? '' : 'opacity-40'}`}
              accessibilityLabel={undoState.canUndo ? `Undo ${undoState.undoLabel}` : 'Nothing to undo'}
            >
              <Icon name="undo" size={16} color={colors.onFill} />
            </Pressable>
            <Pressable
              onPress={() => { if (undoState.canRedo) { haptic('tap'); try { session.redo(); refresh() } catch { /* busy; ignored */ } } }}
              hitSlop={12}
              disabled={!undoState.canRedo}
              className={`rounded-r-full px-3 py-2 active:opacity-70 ${undoState.canRedo ? '' : 'opacity-40'}`}
              accessibilityLabel={undoState.canRedo ? `Redo ${undoState.redoLabel}` : 'Nothing to redo'}
            >
              <Icon name="redo" size={16} color={colors.onFill} />
            </Pressable>
          </View>
        ) : null}
        {pane === 'prepare' ? (
          <View pointerEvents="box-none" className="absolute inset-x-0 bottom-3 items-center">
            <ObjectToolbar
              visible={selectedId !== null}
              tool={objectTool}
              onChangeTool={setObjectTool}
              onOpenDetails={() => setDetailSheetOpen(true)}
            />
          </View>
        ) : null}
      </View>
      {pane === 'preview' && stats !== null ? (
        <View className="flex-row items-center gap-3 pt-1">
          <Button
            title="−"
            variant="secondary"
            onPress={() => setMaxLayer((l) => Math.max(0, stepValid(l) - 1))}
            disabled={stepValid(maxLayer) <= 0}
          />
          <Text className="flex-1 text-center text-subhead text-neutral-700 dark:text-neutral-300">
            {layerLabel}
          </Text>
          <Button
            title="+"
            variant="secondary"
            onPress={() => setMaxLayer((l) => (l < 0 || l + 1 >= stats.layerCount ? -1 : l + 1))}
            disabled={maxLayer < 0}
          />
        </View>
      ) : null}
    </View>
  )

  return (
    <>
      <Stack.Screen
        options={{
          headerLargeTitle: false,
          headerTitle: () => (
            <NativeSegmentedControl
              values={['Prepare', 'Preview', 'Device']}
              selectedIndex={paneIndex}
              onChange={(e) => {
                const idx = e.nativeEvent.selectedSegmentIndex
                if (idx === 1 && !canPreview) return
                if (idx === 0) { setPane('prepare'); setViewMode('scene') }
                else if (idx === 1) { setPane('preview'); setViewMode('preview') }
                else { setPane('device') }
              }}
              // 220pt fits iPhone-width headers next to the settings gear (SettingsLink) and the
              // downloads pill without one shoving another off-screen. iPad still
              // has room to spare either side.
              style={{ width: 220 }}
            />
          ),
        }}
      />
      {pane === 'device' ? (
        <ScrollView
          className="bg-neutral-50 dark:bg-neutral-950"
          contentContainerClassName="px-4 pt-2 pb-24"
          contentInsetAdjustmentBehavior="automatic"
        >
          <DevicePanel
            hosts={printers}
            activeId={activeDeviceId}
            onFocus={(id) => setActiveDeviceId(id)}
          />
          {printers.length === 0 ? (
            <View className="mt-4">
              <Button title="Manage destinations" variant="secondary" onPress={() => router.push('/printers')} fullWidth />
            </View>
          ) : null}
          {pastPrints.length > 0 ? (
            <Card
              className="mt-4"
              index={2}
              collapsible
              collapseKey="recent-prints"
              title="Recent prints"
              subtitle={`${pastPrints.length} send${pastPrints.length === 1 ? '' : 's'}`}
              padded={false}
            >
              {pastPrints.slice(0, 8).map((p, i) => (
                <Pressable
                  key={p.id}
                  onPress={() => { if (printers.some((h) => h.id === p.hostId)) setActiveDeviceId(p.hostId) }}
                  onLongPress={() => {
                    Alert.alert('Remove from history?', p.filename, [
                      { text: 'Cancel', style: 'cancel' },
                      { text: 'Remove', style: 'destructive', onPress: () => { removePrint(p.id); setPastPrints(printHistory()) } },
                    ])
                  }}
                  className={`flex-row items-center gap-3 px-4 py-3 active:opacity-70 ${i === Math.min(7, pastPrints.length - 1) ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
                  accessibilityLabel={`Focus ${p.hostName}`}
                >
                  <View className="flex-1">
                    <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                      {p.filename}
                    </Text>
                    <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
                      {relativeTime(p.at)} · {p.hostName} · {p.started ? 'started' : 'uploaded'}
                    </Text>
                  </View>
                </Pressable>
              ))}
            </Card>
          ) : null}
        </ScrollView>
      ) : (
      <View className={`flex-1 bg-neutral-50 dark:bg-neutral-950 ${isWideLandscape ? 'flex-row' : ''}`}>
        {isWideLandscape ? (
          <View style={{ width: Math.round(winWidth * 0.55) }} className="p-4 pt-3">
            {viewportBlock}
          </View>
        ) : null}
      <ScrollView
        className={isWideLandscape ? 'flex-1 border-l border-neutral-200 dark:border-neutral-800' : ''}
        contentContainerClassName={isWideLandscape ? 'px-4 pt-4 pb-24' : 'px-4 pt-2 pb-24'}
        contentInsetAdjustmentBehavior="automatic"
      >
        {!hasRealPrinter ? (
          <View className="mb-4">
            <Card padded>
              <View className="items-start gap-3">
                <Text className="text-label font-semibold uppercase tracking-caps text-accent-600">
                  Get set up
                </Text>
                <Text className="text-title2 font-semibold tracking-tight text-neutral-900 dark:text-neutral-100">
                  Pick your printer to start slicing
                </Text>
                <Text className="text-subhead leading-5 text-neutral-500 dark:text-neutral-400">
                  Browse every printer OrcaSlicer knows about. The right filament and process presets install with it.
                </Text>
                <Button title="Choose a printer" size="lg" fullWidth onPress={() => router.push('/vendors')} />
              </View>
            </Card>
          </View>
        ) : null}

        {presetError !== '' ? (
          <View className="mb-4">
            <InlineAlert
              tone="critical"
              title="Profile load failed"
              clampLines={4}
              action={
                <View className="flex-row flex-wrap gap-2">
                  {(() => {
                    // Two recovery paths, offered together when the error names a
                    // specific file: nuke just that file (least destructive — the
                    // rest of the vendor keeps working) or nuke the whole vendor.
                    const badFile   = offendingFileIn(presetError)
                    const badVendor = offendingVendorIn(presetError)
                    return (
                      <>
                        {badFile !== null ? (
                          <Button
                            title="Delete bad file"
                            variant="secondary"
                            loading={removing}
                            disabled={removing}
                            onPress={async () => {
                              setRemoving(true)
                              try {
                                const removed = removePresetFile(badFile)
                                if (!removed) {
                                  showToast('Could not locate that file — try Remove vendor.', 'error')
                                  return
                                }
                                await queryClient.invalidateQueries({ queryKey: queryKeys.installedVendors })
                                await reloadPresets()
                                refresh()
                              } finally { setRemoving(false) }
                            }}
                          />
                        ) : null}
                        {badVendor !== null ? (
                          <Button
                            title={`Remove ${badVendor}`}
                            variant="destructive"
                            loading={removing}
                            disabled={removing}
                            onPress={async () => {
                              setRemoving(true)
                              try {
                                removeVendor(badVendor)
                                await queryClient.invalidateQueries({ queryKey: queryKeys.installedVendors })
                                await reloadPresets()
                                refresh()
                              } finally { setRemoving(false) }
                            }}
                          />
                        ) : null}
                      </>
                    )
                  })()}
                  <Button title="Change printer" variant="secondary" onPress={() => router.push('/vendors')} />
                </View>
              }
            >
              {presetError}
            </InlineAlert>
          </View>
        ) : null}

        {!isWideLandscape ? viewportBlock : null}

        {pane === 'prepare' ? (
          <>
        <Card
          className="mb-6"
          title="Presets"
          padded={false}
          index={0}
          collapsible
          collapseKey="presets"
        >
          {PRESET_KINDS.map(({ kind, label }, i) => (
            <PresetRow
              key={kind}
              label={label}
              value={selected[kind]}
              swatchColor={kind === 'filament' ? primaryFilamentColor : undefined}
              disabled={busy}
              onPickPress={() => openPicker(kind)}
              onEditPress={() => router.push({ pathname: '/settings/[kind]', params: { kind } })}
              last={i === PRESET_KINDS.length - 1}
            />
          ))}
        </Card>

        <Card
          className="mb-6"
          index={1}
          collapsible
          collapseKey="plate"
          title="Plate"
          subtitle={objects.length === 0 ? 'Nothing on the plate yet' : `${objects.length} object${objects.length === 1 ? '' : 's'}`}
          padded={false}
        >
          {plates.length > 0 ? (
            <ScrollView
              horizontal
              showsHorizontalScrollIndicator={false}
              contentContainerClassName="gap-2 px-4 pt-3"
            >
              {plates.map((p, i) => (
                <Pressable
                  key={p.id}
                  onPress={() => { if (!p.isCurrent) { session.selectPlate(p.id); refresh() } }}
                  onLongPress={() => {
                    // Long-press on any plate opens a small action sheet: duplicate
                    // is available for any plate; remove is only offered when there
                    // are at least two plates. Both actions target the pressed
                    // plate, not necessarily the current one.
                    const buttons: Array<{ text: string; style?: 'cancel' | 'destructive'; onPress?: () => void }> = [
                      { text: 'Cancel', style: 'cancel' },
                      { text: `Duplicate plate ${i + 1}`, onPress: () => {
                        // duplicatePlate() always targets the CURRENT plate, so
                        // switch focus first if the user pressed a different one.
                        // If the select fails (id vanished between renders), skip
                        // the duplicate so we do not clone the wrong plate.
                        if (!p.isCurrent && !session.selectPlate(p.id)) return
                        session.duplicatePlate()
                        refresh()
                      } },
                    ]
                    if (plates.length > 1) {
                      buttons.push({ text: `Remove plate ${i + 1}`, style: 'destructive', onPress: () => { session.removePlate(p.id); refresh() } })
                    }
                    Alert.alert(`Plate ${i + 1}`, `${p.objectCount} object${p.objectCount === 1 ? '' : 's'}`, buttons)
                  }}
                  className={`rounded-full px-3 py-1.5 ${p.isCurrent ? 'bg-neutral-900 dark:bg-white' : 'bg-neutral-100 dark:bg-neutral-800'}`}
                  accessibilityLabel={`Plate ${i + 1}${p.isCurrent ? ', current' : ''}`}
                >
                  <Text className={`text-footnote font-semibold ${p.isCurrent ? 'text-white dark:text-neutral-900' : 'text-neutral-700 dark:text-neutral-200'}`}>
                    Plate {i + 1}{p.objectCount > 0 ? ` · ${p.objectCount}` : ''}
                  </Text>
                </Pressable>
              ))}
              <Pressable
                onPress={() => { session.addPlate(); refresh(); haptic('tap') }}
                className="rounded-full bg-neutral-100 px-3 py-1.5 dark:bg-neutral-800"
                accessibilityLabel="Add plate"
              >
                <Text className="text-footnote font-semibold text-neutral-700 dark:text-neutral-200">＋ Plate</Text>
              </Pressable>
            </ScrollView>
          ) : null}
          {objects.length === 0 ? (
            <EmptyState
              icon="cube"
              title="Nothing on the plate"
              body="Import an STL, OBJ, 3MF or STEP file from Files or iCloud, or open one from any app."
            />
          ) : (
            objects.map((o, i) => (
              <Row key={o.id} last={i === objects.length - 1}>
                <Pressable
                  onPress={() => { haptic('select'); setSelectedId(o.id); void viewportRef.current?.setSelectedId(o.id); setDetailSheetOpen(true) }}
                  className="flex-row items-center gap-3 active:opacity-60"
                  accessibilityLabel={`Edit ${o.name}`}
                >
                  <View className="flex-1">
                    <Text className="text-subhead text-black dark:text-white" numberOfLines={1}>
                      {o.name}
                    </Text>
                    <Text className="mt-0.5 text-footnote text-gray-500 dark:text-gray-400">
                      {o.size.x.toFixed(1)} × {o.size.y.toFixed(1)} × {o.size.z.toFixed(1)} mm
                    </Text>
                  </View>
                  <Button
                    title="Remove"
                    variant="ghost"
                    onPress={() => {
                      session.removeObject(o.id)
                      refresh()
                    }}
                    disabled={busy}
                  />
                </Pressable>
              </Row>
            ))
          )}
          <View className="flex-row gap-3 border-t border-gray-100 p-4 dark:border-neutral-800">
            <View className="flex-1">
              <Button
                title="Import model"
                variant="secondary"
                onPress={importModel}
                onLongPress={importFromUrl}
                accessibilityHint="Long press to import from a URL"
                disabled={busy}
                fullWidth
              />
            </View>
            <View className="flex-1">
              <Button
                title="Arrange"
                variant="secondary"
                disabled={busy || objects.length === 0}
                fullWidth
                onPress={() => {
                  setBusy(true)
                  session
                    .arrange()
                    .then(refresh)
                    .catch((error: unknown) => showToast(`Arrange failed: ${String(error)}`, 'error'))
                    .finally(() => setBusy(false))
                }}
              />
            </View>
          </View>
        </Card>

        <Card className="mb-6" title="Slice" index={2} collapsible collapseKey="slice">
          {progress !== null ? (
            <View className="gap-3">
              <View className="flex-row items-center justify-between">
                <Text className="text-subhead font-medium text-black dark:text-white" numberOfLines={1}>
                  {t(progress.message)}
                </Text>
                <Text className="text-subhead font-semibold text-blue-500">{progress.percent}%</Text>
              </View>
              <ProgressBar percent={progress.percent} />
              <Button title="Cancel" variant="ghost" onPress={() => session.cancel()} fullWidth />
            </View>
          ) : (
            <Button
              title={stats !== null ? 'Slice again' : 'Slice plate'}
              size="lg"
              onPress={slice}
              disabled={!canSlice}
              fullWidth
            />
          )}
          <View className="flex-row gap-3">
            <View className="flex-1">
              <Button
                title="Save .3mf"
                variant="secondary"
                disabled={busy || objects.length === 0}
                fullWidth
                onPress={() => { void saveProject(true) }}
                onLongPress={() => { void saveProject(false) }}
                accessibilityHint="Long press to save to Files without sharing"
              />
            </View>
            <View className="flex-1">
              <Button title="Calibration" variant="secondary" disabled={!hasRealPrinter} fullWidth onPress={() => router.push('/calibration')} />
            </View>
          </View>

          {result !== null && result.outcome !== 'finished' ? (
            <InlineAlert
              tone="critical"
              title={result.outcome === 'cancelled' ? 'Cancelled' : result.outcome === 'outOfMemory' ? 'Out of memory' : 'Slicing failed'}
            >
              {result.outcome === 'cancelled'
                ? 'You cancelled the slice.'
                : result.outcome === 'outOfMemory'
                  ? `${result.error} Tap an object and use Simplify in the object sheet to reduce its triangle count, or print fewer objects at once.`
                  : result.error}
            </InlineAlert>
          ) : null}

          {result !== null && result.warnings.length > 0 ? (
            <InlineAlert
              tone={result.warnings.some((w) => w.critical) ? 'critical' : 'warning'}
              title={result.warnings.length === 1 ? '1 slicing warning' : `${result.warnings.length} slicing warnings`}
            >
              <View className="gap-1">
                {result.warnings.map((w, i) => (
                  <Text
                    key={i}
                    className={`text-footnote leading-5 ${w.critical ? 'text-red-900/80 dark:text-red-100/80' : 'text-amber-900/80 dark:text-amber-100/80'}`}
                  >
                    • {w.text}
                  </Text>
                ))}
              </View>
            </InlineAlert>
          ) : null}
        </Card>
          </>
        ) : (
          stats === null ? (
            <Card index={2}>
              <EmptyState
                icon="layers"
                title="Nothing sliced yet"
                body="Import a model on the Prepare tab, pick a printer, and tap Slice. The toolpath, print time and filament estimate show up here."
                action={{ title: 'Go to Prepare', onPress: () => { setPane('prepare'); setViewMode('scene') } }}
              />
            </Card>
          ) : (
            <Card className="mb-6" title="Result" index={2}>
              <View className="flex-row gap-3">
                <StatTile label="Print time" value={formatDuration(stats.printTimeSeconds)} />
                <StatTile label="Filament" value={`${stats.filamentGrams.toFixed(1)} g`} />
                <StatTile label="Layers" value={String(stats.layerCount)} />
              </View>
              <View className="flex-row gap-3">
                <View className="flex-1">
                  <Button title="Share G-code" variant="secondary" onPress={share} disabled={gcodePath === null} fullWidth />
                </View>
                <View className="flex-1">
                  <Button
                    title="Upload"
                    variant="secondary"
                    onPress={() => send(false)}
                    disabled={gcodePath === null || sending}
                    fullWidth
                  />
                </View>
                <View className="flex-1">
                  <Button
                    title="Print"
                    onPress={() => send(true)}
                    disabled={gcodePath === null || sending}
                    fullWidth
                  />
                </View>
              </View>
              <Button
                title="Slice again"
                variant="ghost"
                onPress={() => {
                  setPane('prepare')
                  setViewMode('scene')
                  slice()
                }}
                disabled={busy}
                fullWidth
              />
            </Card>
          )
        )}

        {pane === 'preview' && stats !== null ? (
          <Card
            className="mb-6"
            index={3}
            collapsible
            collapseKey="preview-options"
            title="Preview options"
            padded={false}
          >
            <View className="gap-4 p-4">
              <View>
                <Text className="mb-2 text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
                  Color by
                </Text>
                <NativeSegmentedControl
                  values={['Feature', 'Speed', 'Flow', 'Tool']}
                  selectedIndex={colorMode === 'speed' ? 1 : colorMode === 'flow' ? 2 : colorMode === 'tool' ? 3 : 0}
                  onChange={(e) => {
                    const idx = e.nativeEvent.selectedSegmentIndex
                    setColorMode(idx === 1 ? 'speed' : idx === 2 ? 'flow' : idx === 3 ? 'tool' : 'feature')
                  }}
                />
              </View>
              <View className="gap-2">
                <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
                  Show moves
                </Text>
                <ToggleRow label="Travels" value={showTravels} onChange={setShowTravels} />
                <ToggleRow label="Wipes" value={showWipes} onChange={setShowWipes} />
                <ToggleRow label="Retractions" value={showRetractions} onChange={setShowRetractions} />
              </View>
              {stats.layerCount > 1 ? (
                <View className="gap-2">
                  <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
                    Layer range
                  </Text>
                  <View className="flex-row items-center gap-3">
                    <Button
                      title="−"
                      variant="secondary"
                      onPress={() => setMinLayer((l) => Math.max(0, (l < 0 ? 0 : l) - 1))}
                      disabled={minLayer <= 0}
                    />
                    <Text className="flex-1 text-center text-footnote text-neutral-700 dark:text-neutral-300">
                      From layer {minLayer < 0 ? 0 : minLayer}
                    </Text>
                    <Button
                      title="+"
                      variant="secondary"
                      onPress={() => {
                        const maxAllowed = (maxLayer < 0 ? stats.layerCount - 1 : maxLayer) - 1
                        setMinLayer((l) => Math.min(maxAllowed, (l < 0 ? 0 : l) + 1))
                      }}
                      disabled={minLayer >= (maxLayer < 0 ? stats.layerCount - 1 : maxLayer) - 1}
                    />
                  </View>
                </View>
              ) : null}
              {stats.printTimeSeconds > 0 ? (
                <View className="gap-2">
                  <View className="flex-row items-center justify-between">
                    <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
                      Scrub print time
                    </Text>
                    <Text className="text-label text-neutral-500 dark:text-neutral-400">
                      {maxTime < 0
                        ? `full ${formatDuration(stats.printTimeSeconds)}`
                        : `${formatDuration(maxTime)} of ${formatDuration(stats.printTimeSeconds)}`}
                    </Text>
                  </View>
                  <Slider
                    minimumValue={0}
                    maximumValue={stats.printTimeSeconds}
                    value={maxTime < 0 ? stats.printTimeSeconds : Math.min(maxTime, stats.printTimeSeconds)}
                    onValueChange={(v) => setMaxTime(v >= stats.printTimeSeconds - 0.5 ? -1 : v)}
                    step={Math.max(1, Math.round(stats.printTimeSeconds / 400))}
                  />
                </View>
              ) : null}
            </View>
          </Card>
        ) : null}

        {pastProjects.length > 0 ? (
          <Card
            className="mb-6"
            index={3}
            collapsible
            collapseKey="recent-projects"
            title="Recent projects"
            subtitle={`${pastProjects.length} .3mf saved`}
            padded={false}
          >
            {pastProjects.slice(0, 5).map((p, i) => (
              <Pressable
                key={p.id}
                onPress={() => { void importPath(p.path) }}
                onLongPress={() => {
                  Alert.alert('Remove from history?', p.name, [
                    { text: 'Cancel', style: 'cancel' },
                    { text: 'Remove', style: 'destructive', onPress: () => { removeProject(p.id); setPastProjects(projectHistory()) } },
                  ])
                }}
                className={`flex-row items-center gap-3 px-4 py-3 active:opacity-70 ${i === Math.min(4, pastProjects.length - 1) ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
                accessibilityLabel={`Open ${p.name}`}
              >
                <View className="flex-1">
                  <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                    {p.name}
                  </Text>
                  <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
                    {relativeTime(p.at)} · {p.printer.replace(/^Default /, '')} · {p.objectCount} object{p.objectCount === 1 ? '' : 's'}
                  </Text>
                </View>
                <Icon name="open" size={14} />
              </Pressable>
            ))}
          </Card>
        ) : null}

        {pastSlices.length > 0 ? (
          <Card
            className="mb-6"
            index={4}
            collapsible
            collapseKey="recent"
            defaultCollapsed
            title="Recent slices"
            subtitle={`${pastSlices.length} slice${pastSlices.length === 1 ? '' : 's'} in history`}
            padded={false}
          >
            {pastSlices.slice(0, 5).map((h, i) => (
              <View
                key={h.id}
                className={`flex-row items-center gap-3 px-4 py-3 ${i === Math.min(4, pastSlices.length - 1) ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
              >
                <View className="flex-1">
                  <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                    {h.objectNames.join(', ') || 'Untitled plate'}
                  </Text>
                  <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
                    {relativeTime(h.at)} · {h.printer.replace(/^Default /, '')} · {formatDuration(h.stats.printTimeSeconds)} · {h.stats.filamentGrams.toFixed(1)} g
                  </Text>
                </View>
                <Pressable
                  onPress={() => {
                    removeHistory(h.id)
                    setPastSlices(history())
                  }}
                  hitSlop={8}
                  className="rounded-full px-2 py-1 active:opacity-60"
                >
                  <Text className="text-callout font-semibold text-red-500">✕</Text>
                </Pressable>
              </View>
            ))}
          </Card>
        ) : null}

        <Card
          className="mb-6"
          index={4}
          collapsible
          collapseKey="destinations"
          defaultCollapsed
          title="Destinations"
          subtitle={printers.length === 0 ? 'None configured' : `${printers.length} configured`}
          padded={false}
        >
          {printers.length === 0 ? (
            <EmptyState
              icon="network"
              title="No destinations yet"
              body="Add a Moonraker or OctoPrint host to send G-code straight to the printer."
            />
          ) : null}
          {printers.map((p, i) => {
            const last = i === printers.length - 1
            return (
              <Pressable
                key={p.id}
                onPress={() => focusDevice(p.id)}
                className={`flex-row items-center gap-3 px-4 py-3 active:bg-neutral-100 dark:active:bg-neutral-800 ${last ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
              >
                <View className="flex-1">
                  <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                    {p.name}
                  </Text>
                  <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
                    {p.url}
                  </Text>
                </View>
                <Icon name="chevron" size={16} />
              </Pressable>
            )
          })}
          <View className="flex-row gap-2 border-t border-neutral-100 p-4 dark:border-neutral-800">
            <View className="flex-1">
              <Button title="Manage" variant="secondary" fullWidth onPress={() => router.push('/printers')} />
            </View>
            <View className="flex-1">
              <Button title="Change printer" variant="secondary" fullWidth onPress={() => router.push('/vendors')} />
            </View>
          </View>
        </Card>

        <Text className="text-center text-caption uppercase tracking-widest text-neutral-400 dark:text-neutral-600">
          OrcaCore {version}
        </Text>
      </ScrollView>
      </View>
      )}

      {pickerKind !== null ? (
        <PresetPicker
          kind={pickerKind}
          title={pickerKind === 'printer' ? 'Choose a printer' : pickerKind === 'filament' ? 'Choose a filament' : 'Choose a process'}
          presets={pickerPresets}
          selected={selected[pickerKind]}
          visible
          onPick={pickPreset}
          onClose={() => setPickerKind(null)}
        />
      ) : null}
      <ObjectDetailSheet
        visible={detailSheetOpen && selectedId !== null}
        session={session}
        objectId={selectedId}
        onClose={() => setDetailSheetOpen(false)}
        onChanged={() => {
          setRevision((r) => r + 1)
          setObjects(session.objects())
        }}
      />
      <OnboardingSheet visible={showOnboarding} onDismiss={() => setShowOnboarding(false)} />
    </>
  )
}

function StatTile({ label, value }: { label: string; value: string }): React.JSX.Element {
  return (
    <View className="flex-1 rounded-xl bg-gray-100 px-3 py-3 dark:bg-neutral-800">
      <Text className="text-label uppercase tracking-wider text-gray-500 dark:text-gray-400">{label}</Text>
      <Text className="mt-1 text-body font-semibold text-black dark:text-white" numberOfLines={1}>
        {value}
      </Text>
    </View>
  )
}

function ToggleRow({ label, value, onChange }: { label: string; value: boolean; onChange: (v: boolean) => void }): React.JSX.Element {
  return (
    <Pressable
      onPress={() => onChange(!value)}
      className="flex-row items-center justify-between rounded-xl bg-gray-50 px-3 py-3 active:bg-gray-100 dark:bg-neutral-800 dark:active:bg-neutral-700"
    >
      <Text className="text-subhead text-black dark:text-white">{label}</Text>
      <View
        className={`h-6 w-11 justify-center rounded-full px-0.5 ${value ? 'bg-blue-500' : 'bg-gray-300 dark:bg-neutral-600'}`}
      >
        <View className={`h-5 w-5 rounded-full bg-white ${value ? 'self-end' : 'self-start'}`} />
      </View>
    </Pressable>
  )
}
