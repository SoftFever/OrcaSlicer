import { create } from 'zustand'

import type { ObjectTool, PreviewColorMode, ViewportMode } from 'react-native-orca-core'

// Cross-screen viewport state — the home screen and the fullscreen route
// both render the same OrcaViewport, and both need to agree on the current
// mode, the layer range and the preview filter toggles. Route params were
// pushing this through the URL and touching expo-router's global state on
// every render, so we hold it here instead.

/** Workspace top-level pane. Drives the header segmented control. */
export type WorkspacePane = 'prepare' | 'preview' | 'device'

type ViewportState = {
  mode: ViewportMode
  minLayer: number
  maxLayer: number
  /** Preview time cutoff in seconds from print start; -1 disables the filter. */
  maxTime: number
  showTravels: boolean
  showRetractions: boolean
  showWipes: boolean
  colorMode: PreviewColorMode
  pane: WorkspacePane
  /** Currently-focused printer host in the Device pane, when the user configured any. */
  activeDeviceId: string | null
  /** Selected object id in the Prepare viewport, or null when nothing is picked. */
  selectedId: number | null
  /** Which drag interaction to run on the selected object. Move by default. */
  objectTool: ObjectTool
  setMode: (mode: ViewportMode) => void
  setMinLayer: (updater: number | ((prev: number) => number)) => void
  setMaxLayer: (updater: number | ((prev: number) => number)) => void
  setMaxTime: (v: number) => void
  setShowTravels: (v: boolean) => void
  setShowRetractions: (v: boolean) => void
  setShowWipes: (v: boolean) => void
  setColorMode: (mode: PreviewColorMode) => void
  setPane: (pane: WorkspacePane) => void
  setActiveDeviceId: (id: string | null) => void
  /** Convenience: switch to Device pane with a specific host focused. */
  focusDevice: (id: string) => void
  setSelectedId: (id: number | null) => void
  setObjectTool: (tool: ObjectTool) => void
  reset: () => void
}

export const useViewportStore = create<ViewportState>((set) => ({
  mode: 'scene',
  minLayer: -1,
  maxLayer: -1,
  maxTime: -1,
  showTravels: false,
  showRetractions: false,
  showWipes: false,
  colorMode: 'feature',
  pane: 'prepare',
  activeDeviceId: null,
  selectedId: null,
  objectTool: 'move',
  setMode: (mode) => set({ mode }),
  setMinLayer: (updater) =>
    set((prev) => ({
      minLayer: typeof updater === 'function' ? updater(prev.minLayer) : updater,
    })),
  setMaxLayer: (updater) =>
    set((prev) => ({
      maxLayer: typeof updater === 'function' ? updater(prev.maxLayer) : updater,
    })),
  setMaxTime: (maxTime) => set({ maxTime }),
  setShowTravels: (showTravels) => set({ showTravels }),
  setShowRetractions: (showRetractions) => set({ showRetractions }),
  setShowWipes: (showWipes) => set({ showWipes }),
  setColorMode: (colorMode) => set({ colorMode }),
  setPane: (pane) => set({ pane }),
  setActiveDeviceId: (activeDeviceId) => set({ activeDeviceId }),
  focusDevice: (id) => set({ pane: 'device', activeDeviceId: id }),
  setSelectedId: (selectedId) => set({ selectedId }),
  setObjectTool: (objectTool) => set({ objectTool }),
  reset: () =>
    set({      mode: 'scene',
      minLayer: -1,
      maxLayer: -1,
      maxTime: -1,
      showTravels: false,
      showRetractions: false,
      showWipes: false,
      colorMode: 'feature',
      pane: 'prepare',
    }),
}))
