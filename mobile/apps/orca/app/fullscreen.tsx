import * as DocumentPicker from 'expo-document-picker'
import * as ScreenOrientation from 'expo-screen-orientation'
import { Stack, useRouter } from 'expo-router'
import { StatusBar } from 'expo-status-bar'
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { Alert, Pressable, Text, View } from 'react-native'
import { callback } from 'react-native-nitro-modules'
import {
  OrcaViewport,
  type MirrorAxis,
  type ObjectInfo,
  type OrcaViewportRef,
} from 'react-native-orca-core'

import { ObjectToolbar } from '@/ui/ObjectToolbar'
import { colors } from '@/lib/theme'
import { Icon } from '@/ui/Icon'

import { useSession } from '@/lib/core'
import { haptic } from '@/lib/haptics'
import { useViewportStore } from '@/lib/viewport'

// Full-screen editor. Renders through the router (not a Modal portal) so
// react-navigation's context stays intact and any React state upstream re-renders
// cleanly. The device is locked to landscape while mounted and released on unmount.
// Camera gestures (orbit / two-finger pan / pinch / double-tap-to-fit) live on the
// native metal view; taps hit a JS Pressable that ray-picks against the session so
// selection state and the action bar work identically to the home viewport.
//
// The overlay chrome is sized for iPad mini in landscape (≈ 1133 × 744 pt): every
// button is a rounded pill on a dark scrim, spacing is tight enough that the corner
// clusters do not collide when both are populated, and the selection bar collapses
// to a single row so it fits above the mode pill without covering it.

export default function FullscreenScreen(): React.JSX.Element {
  const router = useRouter()
  const session = useSession()
  const pane = useViewportStore((s) => s.pane)
  const setPane = useViewportStore((s) => s.setPane)
  const setMode = useViewportStore((s) => s.setMode)
  const mode = useViewportStore((s) => s.mode)
  const minLayer = useViewportStore((s) => s.minLayer)
  const maxLayer = useViewportStore((s) => s.maxLayer)
  const maxTime = useViewportStore((s) => s.maxTime)
  const showTravels = useViewportStore((s) => s.showTravels)
  const showRetractions = useViewportStore((s) => s.showRetractions)
  const showWipes = useViewportStore((s) => s.showWipes)
  const colorMode = useViewportStore((s) => s.colorMode)
  const selectedId = useViewportStore((s) => s.selectedId)
  const setSelectedId = useViewportStore((s) => s.setSelectedId)
  const objectTool = useViewportStore((s) => s.objectTool)
  const setObjectTool = useViewportStore((s) => s.setObjectTool)

  // Nitro views expose their methods via `hybridRef`, not the standard React
  // `ref`. Populated by the callback on <OrcaViewport hybridRef=... />.
  const viewportRef = useRef<OrcaViewportRef | null>(null)
  const [revision, setRevision] = useState(0)
  const [busy, setBusy] = useState(false)
  const [objects, setObjects] = useState<ObjectInfo[]>([])
  const [undoState, setUndoState] = useState<{
    canUndo: boolean
    canRedo: boolean
    undoLabel: string
    redoLabel: string
  }>({
    canUndo: false,
    canRedo: false,
    undoLabel: '',
    redoLabel: '',
  })

  const refresh = useCallback(() => {
    if (session === null) return
    try {
      setRevision((r) => r + 1)
      setObjects(session.objects())
      setUndoState({
        canUndo: session.canUndo,
        canRedo: session.canRedo,
        undoLabel: session.undoLabel(),
        redoLabel: session.redoLabel(),
      })
    } catch {
      /* mutex briefly held; caller will refresh again */
    }
  }, [session])

  useEffect(() => {
    void ScreenOrientation.lockAsync(ScreenOrientation.OrientationLock.LANDSCAPE).catch(() => {})
    return () => {
      void ScreenOrientation.unlockAsync().catch(() => {})
    }
  }, [])

  useEffect(() => {
    if (session !== null) refresh()
  }, [session, refresh])

  const selectedObject = useMemo(
    () => (selectedId === null ? null : (objects.find((o) => o.id === selectedId) ?? null)),
    [objects, selectedId],
  )

  const importModel = useCallback(async () => {
    if (session === null) return
    const picked = await DocumentPicker.getDocumentAsync({
      copyToCacheDirectory: false,
      multiple: false,
    })
    const asset = picked.assets?.[0]
    if (picked.canceled || asset === undefined) return
    setBusy(true)
    try {
      await session.importModel(decodeURIComponent(asset.uri.replace(/^file:\/\//, '')))
      refresh()
    } catch (error) {
      Alert.alert('Import failed', String(error))
    } finally {
      setBusy(false)
    }
  }, [session, refresh])

  const arrange = useCallback(() => {
    if (session === null) return
    setBusy(true)
    session
      .arrange()
      .then(() => {
        haptic('success')
        refresh()
      })
      .catch((error: unknown) => Alert.alert('Arrange failed', String(error)))
      .finally(() => setBusy(false))
  }, [session, refresh])

  const doUndo = useCallback(() => {
    if (session === null) return
    haptic('tap')
    try {
      if (session.undo()) refresh()
    } catch {
      /* busy */
    }
  }, [session, refresh])

  const doRedo = useCallback(() => {
    if (session === null) return
    haptic('tap')
    try {
      if (session.redo()) refresh()
    } catch {
      /* busy */
    }
  }, [session, refresh])

  const setPaneAndMode = useCallback(
    (next: 'prepare' | 'preview') => {
      setPane(next)
      setMode(next === 'preview' ? 'preview' : 'scene')
    },
    [setPane, setMode],
  )

  // Handle a tap on the viewport by ray-picking. This runs on the JS thread —
  // the worklet only relays coords through runOnJS. Using a gesture-handler Tap
  // (rather than a Pressable overlay) is what lets the native pan / pinch on the
  // Metal view keep working: gesture-handler composes with UIKit recognizers,
  // whereas RN Pressable claims the responder on touch-down and starves them.
  const handleTap = useCallback(
    (x: number, y: number) => {
      const vp = viewportRef.current
      // Same reason as the home screen: gate pane INSIDE the handler so we can
      // pass the wrapped callback unconditionally and avoid Fabric's raw-value
      // converter throwing when onTap toggles between an Object and undefined.
      if (vp === null || session === null || pane !== 'prepare') return
      void (async () => {
        try {
          const ray = await vp.screenToRay(x, y)
          const hit = session.pickAt(
            { x: ray.originX, y: ray.originY, z: ray.originZ },
            { x: ray.directionX, y: ray.directionY, z: ray.directionZ },
          )
          if (hit.hitId !== 0) {
            haptic('select')
            setSelectedId(hit.hitId)
            void vp.setSelectedId(hit.hitId)
          } else {
            setSelectedId(null)
            void vp.setSelectedId(0)
          }
        } catch {
          /* picking failed silently */
        }
      })()
    },
    [session, setSelectedId, pane],
  )

  useEffect(() => {
    try {
      void viewportRef.current?.setObjectTool(objectTool)
    } catch {
      /* ref not ready */
    }
  }, [objectTool])

  // Object action helpers — thin wrappers so onPress handlers stay readable and
  // every mutation both takes a snapshot (native side does it) and refreshes.
  const withSelected = (fn: (id: number) => void) => (): void => {
    if (selectedObject === null) return
    fn(selectedObject.id)
    haptic('tap')
    refresh()
  }
  const duplicate = withSelected((id) => {
    session?.duplicateObject(id)
  })
  const layFlat = withSelected((id) => {
    session?.layFlat(id)
  })
  const mirror = (axis: MirrorAxis) =>
    withSelected((id) => {
      session?.mirrorObject(id, axis)
    })
  const remove = withSelected((id) => {
    session?.removeObject(id)
    setSelectedId(null)
    void viewportRef.current?.setSelectedId(0)
  })

  if (session === null) {
    return (
      <View className="flex-1 items-center justify-center bg-black">
        <Stack.Screen options={{ headerShown: false }} />
        <Text className="text-subhead text-white/70">Loading…</Text>
      </View>
    )
  }

  return (
    <View className="flex-1 bg-black">
      <Stack.Screen options={{ headerShown: false }} />
      <StatusBar style="light" hidden />
      <OrcaViewport
        hybridRef={callback((ref) => {
          viewportRef.current = ref
          try {
            void ref.setObjectTool(objectTool)
          } catch {
            /* first mount, ok */
          }
        })}
        style={{ flex: 1 }}
        sessionId={session.id}
        mode={mode}
        minLayer={minLayer}
        maxLayer={maxLayer}
        maxTime={maxTime}
        showTravels={showTravels}
        showRetractions={showRetractions}
        showWipes={showWipes}
        colorMode={colorMode}
        revision={revision}
        onTap={callback(handleTap)}
      />

      {/* Top-left: undo / redo cluster (Prepare only — Preview has nothing to undo). */}
      {pane === 'prepare' ? (
        <View className="absolute left-4 top-4 flex-row overflow-hidden rounded-full bg-black/55">
          <Pressable
            onPress={doUndo}
            hitSlop={12}
            disabled={!undoState.canUndo}
            accessibilityLabel={
              undoState.canUndo ? `Undo ${undoState.undoLabel}` : 'Nothing to undo'
            }
            className={`px-4 py-2 active:opacity-70 ${undoState.canUndo ? '' : 'opacity-40'}`}
          >
            <Icon name="undo" size={18} color={colors.onFill} />
          </Pressable>
          <View className="my-2 w-px bg-white/25" />
          <Pressable
            onPress={doRedo}
            hitSlop={12}
            disabled={!undoState.canRedo}
            accessibilityLabel={
              undoState.canRedo ? `Redo ${undoState.redoLabel}` : 'Nothing to redo'
            }
            className={`px-4 py-2 active:opacity-70 ${undoState.canRedo ? '' : 'opacity-40'}`}
          >
            <Icon name="redo" size={18} color={colors.onFill} />
          </Pressable>
        </View>
      ) : null}

      {/* Top-right: scene actions + done. Preview drops import / arrange, keeps done. */}
      <View className="absolute right-4 top-4 flex-row gap-2">
        {pane === 'prepare' ? (
          <>
            <ChromeButton label="Import" onPress={importModel} disabled={busy} />
            <ChromeButton
              label="Arrange"
              onPress={arrange}
              disabled={busy || objects.length === 0}
            />
          </>
        ) : null}
        <ChromeButton label="Done" tone="primary" onPress={() => router.back()} />
      </View>

      {/* Selection action bar — appears above the mode toggle when an object is
          picked. Discrete ops the plate can undo; the numeric transform sliders
          are on the home ObjectDetailSheet for now (that Modal misbehaves in
          landscape, so we don't open it from here). */}
      {pane === 'prepare' && selectedObject !== null ? (
        <View pointerEvents="box-none" className="absolute inset-x-0 bottom-28 items-center gap-2">
          <ObjectToolbar visible tool={objectTool} onChangeTool={setObjectTool} />
          <View className="max-w-full flex-row items-center gap-2 rounded-full bg-black/60 px-3 py-2">
            <Text className="max-w-[220px] text-footnote font-medium text-white" numberOfLines={1}>
              {selectedObject.name}
            </Text>
            <Text className="text-caption text-white/60">
              {selectedObject.size.x.toFixed(0)} × {selectedObject.size.y.toFixed(0)} ×{' '}
              {selectedObject.size.z.toFixed(0)} mm
            </Text>
            <View className="mx-1 h-4 w-px bg-white/25" />
            <MiniButton label="Lay flat" onPress={layFlat} />
            <MiniButton label="Duplicate" onPress={duplicate} />
            <MiniButton label="Mirror X" onPress={mirror('x')} />
            <MiniButton label="Mirror Y" onPress={mirror('y')} />
            <MiniButton label="Delete" tone="destructive" onPress={remove} />
          </View>
        </View>
      ) : null}

      {/* Bottom-center mode toggle — segmented pill so users can flip to preview
          without leaving fullscreen. Preview is disabled until a slice has run. */}
      <View className="absolute inset-x-0 bottom-4 items-center">
        <View className="flex-row overflow-hidden rounded-full bg-black/55">
          <Pressable
            onPress={() => setPaneAndMode('prepare')}
            className={`px-4 py-1.5 ${pane === 'prepare' ? 'bg-white/25' : ''}`}
          >
            <Text
              className={`text-label font-medium uppercase tracking-wider ${pane === 'prepare' ? 'text-white' : 'text-white/70'}`}
            >
              Objects
            </Text>
          </Pressable>
          <Pressable
            onPress={() => setPaneAndMode('preview')}
            className={`px-4 py-1.5 ${pane === 'preview' ? 'bg-white/25' : ''}`}
          >
            <Text
              className={`text-label font-medium uppercase tracking-wider ${pane === 'preview' ? 'text-white' : 'text-white/40'}`}
            >
              Preview
            </Text>
          </Pressable>
        </View>
      </View>
    </View>
  )
}

function ChromeButton({
  label,
  onPress,
  disabled,
  tone,
}: {
  label: string
  onPress: () => void
  disabled?: boolean
  tone?: 'primary' | 'default'
}): React.JSX.Element {
  const bg = tone === 'primary' ? 'bg-white' : 'bg-black/55'
  const text = tone === 'primary' ? 'text-black' : 'text-white'
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      hitSlop={12}
      className={`rounded-full px-4 py-2 active:opacity-70 ${bg} ${disabled === true ? 'opacity-40' : ''}`}
    >
      <Text className={`text-footnote font-semibold uppercase tracking-wider ${text}`}>
        {label}
      </Text>
    </Pressable>
  )
}

function MiniButton({
  label,
  onPress,
  tone,
}: {
  label: string
  onPress: () => void
  tone?: 'destructive'
}): React.JSX.Element {
  const text = tone === 'destructive' ? 'text-red-300' : 'text-white'
  return (
    <Pressable onPress={onPress} hitSlop={8} className="rounded-full px-2.5 py-1 active:opacity-70">
      <Text className={`text-label font-semibold ${text}`}>{label}</Text>
    </Pressable>
  )
}
