import Slider from '@react-native-community/slider'
import React, { useEffect, useMemo, useState } from 'react'
import { Alert, Modal, Pressable, ScrollView, Text, TextInput, View } from 'react-native'
import type { MirrorAxis, ObjectInfo, OrcaSession } from 'react-native-orca-core'

import { filamentColors } from '@/lib/filamentColor'
import { haptic } from '@/lib/haptics'
import { showToast } from '@/lib/toast'
import { Button } from '@/ui/Button'

// A per-object controls sheet — position numeric fields, rotation-Z slider,
// uniform scale, mesh simplify / repair, mirror X/Y/Z, duplicate, lay flat,
// auto-orient, delete.
// Reads the current transform from the session on open and writes the whole
// transform back via `setTransform` on any commit; every action refreshes
// via the caller's `onChanged` so the viewport picks up the new geometry.

type Props = {
  visible: boolean
  session: OrcaSession
  objectId: number | null
  onClose: () => void
  onChanged: () => void
}

function findObject(session: OrcaSession, id: number | null): ObjectInfo | null {
  if (id === null) return null
  return session.objects().find((o) => o.id === id) ?? null
}

const AXES: MirrorAxis[] = ['x', 'y', 'z']

export function ObjectDetailSheet({
  visible,
  session,
  objectId,
  onClose,
  onChanged,
}: Props): React.JSX.Element {
  // Re-read the object every time the sheet is shown so we display fresh
  // values after undo / redo or an auto-orient elsewhere.
  const [revision, setRevision] = useState(0)
  const object = useMemo(
    () => findObject(session, objectId),
    [session, objectId, revision, visible],
  )

  const bump = (): void => {
    setRevision((r) => r + 1)
    onChanged()
  }

  const applyTransform = (
    next: Partial<{
      position: [number, number, number]
      rotation: [number, number, number]
      scale: [number, number, number]
    }>,
  ): void => {
    if (object === null) return
    const pos: [number, number, number] = next.position ?? [
      object.position.x,
      object.position.y,
      object.position.z,
    ]
    const rot: [number, number, number] = next.rotation ?? [
      object.rotation.x,
      object.rotation.y,
      object.rotation.z,
    ]
    const sca: [number, number, number] = next.scale ?? [
      object.scale.x,
      object.scale.y,
      object.scale.z,
    ]
    const ok = session.setTransform(
      object.id,
      { x: pos[0], y: pos[1], z: pos[2] },
      { x: rot[0], y: rot[1], z: rot[2] },
      { x: sca[0], y: sca[1], z: sca[2] },
    )
    if (ok) {
      // The core does not snapshot inside setTransform because a live drag would
      // spam the stack; the sheet only commits on release / blur, so one snapshot
      // per commit is right. Labels match desktop's Selection.cpp naming.
      const label =
        next.position !== undefined
          ? 'Move object'
          : next.rotation !== undefined
            ? 'Rotate'
            : 'Scale'
      session.takeSnapshot(label)
      bump()
    }
  }

  return (
    <Modal
      visible={visible}
      animationType="slide"
      presentationStyle="pageSheet"
      onRequestClose={onClose}
    >
      <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
        <View className="flex-row items-center justify-between border-b border-neutral-100 px-5 pb-3 pt-4 dark:border-neutral-800">
          <View className="flex-1 pr-4">
            <Text
              className="text-title2 font-bold text-neutral-900 dark:text-neutral-100"
              numberOfLines={1}
            >
              {object?.name ?? 'Object'}
            </Text>
            <Text
              className="mt-0.5 text-footnote text-neutral-500 dark:text-neutral-400"
              numberOfLines={1}
            >
              {object !== null
                ? `${object.size.x.toFixed(1)} × ${object.size.y.toFixed(1)} × ${object.size.z.toFixed(1)} mm · ${object.triangles.toLocaleString()} tris`
                : 'Nothing selected'}
            </Text>
          </View>
          <Pressable
            hitSlop={10}
            onPress={() => {
              haptic('tap')
              onClose()
            }}
            className="active:opacity-60"
          >
            <Text className="text-body font-semibold text-blue-500">Done</Text>
          </Pressable>
        </View>

        {object === null ? (
          <View className="flex-1 items-center justify-center p-6">
            <Text className="text-subhead text-neutral-500 dark:text-neutral-400">
              Tap an object on the plate to edit it.
            </Text>
          </View>
        ) : (
          <ScrollView contentContainerClassName="p-4 gap-4">
            <SectionCard title="Position (mm)">
              <View className="flex-row gap-3">
                <NumberField
                  label="X"
                  value={object.position.x}
                  onCommit={(v) =>
                    applyTransform({ position: [v, object.position.y, object.position.z] })
                  }
                />
                <NumberField
                  label="Y"
                  value={object.position.y}
                  onCommit={(v) =>
                    applyTransform({ position: [object.position.x, v, object.position.z] })
                  }
                />
                <NumberField
                  label="Z"
                  value={object.position.z}
                  onCommit={(v) =>
                    applyTransform({ position: [object.position.x, object.position.y, v] })
                  }
                />
              </View>
              <NudgePad
                position={object.position}
                onNudge={(dx, dy) =>
                  applyTransform({
                    position: [object.position.x + dx, object.position.y + dy, object.position.z],
                  })
                }
              />
            </SectionCard>

            <SectionCard title={`Rotate Z (${Math.round((object.rotation.z * 180) / Math.PI)}°)`}>
              <Slider
                minimumValue={-Math.PI}
                maximumValue={Math.PI}
                step={Math.PI / 180}
                value={object.rotation.z}
                onSlidingComplete={(v) =>
                  applyTransform({ rotation: [object.rotation.x, object.rotation.y, v] })
                }
              />
            </SectionCard>

            <SectionCard title={`Uniform scale (${object.scale.x.toFixed(2)}×)`}>
              <Slider
                minimumValue={0.1}
                maximumValue={5}
                step={0.05}
                value={object.scale.x}
                onSlidingComplete={(v) => applyTransform({ scale: [v, v, v] })}
              />
            </SectionCard>

            <FilamentSection session={session} objectId={object.id} onChanged={bump} />

            <MeshSection session={session} object={object} onChanged={bump} />

            <CutSection session={session} object={object} onChanged={bump} />

            <SectionCard title="Mirror">
              <View className="flex-row gap-2">
                {AXES.map((axis) => (
                  <View key={axis} className="flex-1">
                    <Button
                      title={axis.toUpperCase()}
                      variant="secondary"
                      fullWidth
                      onPress={() => {
                        session.mirrorObject(object.id, axis)
                        bump()
                      }}
                    />
                  </View>
                ))}
              </View>
            </SectionCard>

            <View className="flex-row gap-2">
              <View className="flex-1">
                <Button
                  title="Lay flat"
                  variant="secondary"
                  fullWidth
                  onPress={() => {
                    session.layFlat(object.id)
                    bump()
                  }}
                />
              </View>
              <View className="flex-1">
                <Button
                  title="Auto-orient"
                  variant="secondary"
                  fullWidth
                  onPress={() => {
                    session.autoOrient(object.id)
                    bump()
                  }}
                />
              </View>
              <View className="flex-1">
                <Button
                  title="Duplicate"
                  variant="secondary"
                  fullWidth
                  onPress={() => {
                    session.duplicateObject(object.id)
                    bump()
                  }}
                />
              </View>
            </View>

            <Button
              title="Delete"
              variant="destructive"
              fullWidth
              onPress={() => {
                Alert.alert('Remove object?', 'Take the object off the plate.', [
                  { text: 'Cancel', style: 'cancel' },
                  {
                    text: 'Remove',
                    style: 'destructive',
                    onPress: () => {
                      session.removeObject(object.id)
                      haptic('success')
                      onClose()
                      onChanged()
                    },
                  },
                ])
              }}
            />
          </ScrollView>
        )}
      </View>
    </Modal>
  )
}

function SectionCard({
  title,
  children,
}: {
  title: string
  children: React.ReactNode
}): React.JSX.Element {
  return (
    <View className="rounded-2xl bg-white p-4 shadow-soft dark:bg-neutral-900">
      <Text className="mb-2 text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
        {title}
      </Text>
      {children}
    </View>
  )
}

// Above this many triangles a plate is at real risk of running the slicer out of
// memory on a phone; the sheet says so before the user finds out from the slice.
const HEAVY_TRIANGLES = 500_000
const SIMPLIFY_RATIOS = [0.5, 0.25, 0.1] as const
type SimplifyRatio = (typeof SIMPLIFY_RATIOS)[number]

function MeshSection({
  session,
  object,
  onChanged,
}: {
  session: OrcaSession
  object: ObjectInfo
  onChanged: () => void
}): React.JSX.Element {
  // Both actions hold the session's mutex for seconds, during which any other
  // session call throws "busy"; the busy state lives here so only this card
  // re-renders until the promise settles and `onChanged` refreshes the rest.
  const [busy, setBusy] = useState<'simplify' | 'repair' | null>(null)
  const [ratio, setRatio] = useState<SimplifyRatio>(0.5)
  const heavy = object.triangles > HEAVY_TRIANGLES
  const target = Math.max(1000, Math.round(object.triangles * ratio))

  const simplify = async (): Promise<void> => {
    haptic('tap')
    setBusy('simplify')
    try {
      const ok = await session.simplifyObject(object.id, target)
      if (ok) {
        haptic('success')
        showToast(`Simplified to about ${target.toLocaleString()} triangles`, 'success')
      } else {
        showToast('Nothing to simplify', 'info')
      }
      onChanged()
    } catch (error) {
      showToast(`Simplify failed: ${String(error)}`, 'error')
    } finally {
      setBusy(null)
    }
  }

  const repair = async (): Promise<void> => {
    haptic('tap')
    setBusy('repair')
    try {
      const r = await session.repairObject(object.id)
      if (!r.changed) {
        showToast(
          r.openEdgesBefore > 0
            ? 'Open edges remain; this mesh needs a hole-filling tool'
            : 'No mesh errors found',
          'info',
        )
      } else {
        haptic('success')
        const lines = [
          r.verticesMerged > 0
            ? `${r.verticesMerged.toLocaleString()} duplicate vertices merged`
            : null,
          r.degenerateFaces > 0
            ? `${r.degenerateFaces.toLocaleString()} degenerate faces removed`
            : null,
          r.facesRemoved > 0 ? `${r.facesRemoved.toLocaleString()} loose faces removed` : null,
          r.facesReversed > 0 ? `${r.facesReversed.toLocaleString()} faces flipped` : null,
          r.edgesFixed > 0 ? `${r.edgesFixed.toLocaleString()} edges joined` : null,
          `Open edges: ${r.openEdgesBefore.toLocaleString()} → ${r.openEdgesAfter.toLocaleString()}`,
        ].filter((l): l is string => l !== null)
        Alert.alert('Mesh repaired', lines.join('\n'))
      }
      onChanged()
    } catch (error) {
      showToast(`Repair failed: ${String(error)}`, 'error')
    } finally {
      setBusy(null)
    }
  }

  return (
    <View className="rounded-2xl bg-white p-4 shadow-soft dark:bg-neutral-900">
      <View className="mb-2 flex-row items-center justify-between">
        <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
          Mesh
        </Text>
        <Text
          className={`text-label ${object.openEdges > 0 ? 'text-amber-600 dark:text-amber-400' : 'text-neutral-500 dark:text-neutral-400'}`}
        >
          {object.triangles.toLocaleString()} tris ·{' '}
          {object.openEdges > 0 ? `${object.openEdges.toLocaleString()} open edges` : 'watertight'}
        </Text>
      </View>
      {heavy ? (
        <Text className="mb-3 text-footnote text-amber-700 dark:text-amber-400">
          Heavy mesh. Slicing may run out of memory on this device; simplifying first is safer.
        </Text>
      ) : null}
      <View className="mb-2 flex-row items-center gap-2">
        <Text className="text-caption uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
          Keep
        </Text>
        {SIMPLIFY_RATIOS.map((r) => (
          <Pressable
            key={r}
            disabled={busy !== null}
            onPress={() => {
              haptic('toggle')
              setRatio(r)
            }}
            className={`rounded-lg px-3 py-1 ${ratio === r ? 'bg-blue-500' : 'bg-neutral-100 dark:bg-neutral-800'}`}
          >
            <Text
              className={`text-footnote font-semibold ${ratio === r ? 'text-white' : 'text-neutral-700 dark:text-neutral-300'}`}
            >
              {Math.round(r * 100)}%
            </Text>
          </Pressable>
        ))}
      </View>
      <View className="flex-row gap-2">
        <View className="flex-1">
          <Button
            title={busy === 'simplify' ? 'Simplifying…' : `Simplify to ${target.toLocaleString()}`}
            variant="secondary"
            fullWidth
            loading={busy === 'simplify'}
            disabled={busy !== null}
            onPress={() => void simplify()}
          />
        </View>
        <View className="flex-1">
          <Button
            title={busy === 'repair' ? 'Repairing…' : 'Repair'}
            variant="secondary"
            fullWidth
            loading={busy === 'repair'}
            disabled={busy !== null}
            onPress={() => void repair()}
          />
        </View>
      </View>
    </View>
  )
}

function CutSection({
  session,
  object,
  onChanged,
}: {
  session: OrcaSession
  object: ObjectInfo
  onChanged: () => void
}): React.JSX.Element {
  // The object's world-space Z range: bottom at position.z, top at position.z + size.z.
  // The cut plane defaults to the mid-height so a single tap on Cut splits it in half.
  const bottom = object.position.z
  const top = object.position.z + object.size.z
  const [z, setZ] = useState((bottom + top) / 2)
  useEffect(() => {
    setZ((bottom + top) / 2)
  }, [bottom, top])
  return (
    <View className="rounded-2xl bg-white p-4 shadow-soft dark:bg-neutral-900">
      <View className="mb-2 flex-row items-center justify-between">
        <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
          Cut plane
        </Text>
        <Text className="text-label text-neutral-500 dark:text-neutral-400">
          Z = {z.toFixed(1)} mm
        </Text>
      </View>
      <Slider
        minimumValue={bottom + 0.5}
        maximumValue={top - 0.5}
        step={0.1}
        value={z}
        onValueChange={setZ}
      />
      <Button
        title="Cut here"
        variant="secondary"
        fullWidth
        onPress={() => {
          session.cutObject(object.id, z)
          haptic('success')
          onChanged()
        }}
      />
    </View>
  )
}

function FilamentSection({
  session,
  objectId,
  onChanged,
}: {
  session: OrcaSession
  objectId: number
  onChanged: () => void
}): React.JSX.Element {
  // Pull the extruder list and current-assigned extruder every time the sheet
  // re-renders after an edit; extruderCount is a session-level property so a
  // printer change (rare mid-edit) is reflected without remounting the sheet.
  const [current, setCurrent] = useState<number>(0)
  const [filaments, setFilaments] = useState<string[]>([])
  const [colors, setColors] = useState<string[]>([])
  useEffect(() => {
    try {
      setCurrent(session.objectExtruder(objectId))
      setFilaments(session.selectedFilaments())
      setColors(filamentColors(session))
    } catch {
      /* mutex briefly held; caller will refresh */
    }
  }, [session, objectId])

  // Single-extruder printers don't need the picker — the row still shows which
  // filament will print the object so users spot mismatches (wrong material for
  // an object silently printed with the default).
  if (filaments.length <= 1) {
    return (
      <View className="rounded-2xl bg-white p-4 shadow-soft dark:bg-neutral-900">
        <Text className="mb-2 text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
          Filament
        </Text>
        <View className="flex-row items-center gap-2">
          {colors[0] !== undefined ? (
            <View
              className="h-3.5 w-3.5 rounded-full border border-neutral-300 dark:border-neutral-700"
              style={{ backgroundColor: colors[0] }}
            />
          ) : null}
          <Text
            className="flex-1 text-callout text-neutral-700 dark:text-neutral-300"
            numberOfLines={1}
          >
            {filaments[0] ?? '—'}
          </Text>
        </View>
      </View>
    )
  }

  const pick = (n: number): void => {
    haptic('select')
    if (session.setObjectExtruder(objectId, n)) {
      setCurrent(n)
      onChanged()
    }
  }
  return (
    <View className="rounded-2xl bg-white p-4 shadow-soft dark:bg-neutral-900">
      <Text className="mb-2 text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
        Filament
      </Text>
      <View className="flex-row flex-wrap gap-2">
        <Pressable
          onPress={() => pick(0)}
          className={`rounded-full px-3 py-1.5 ${current === 0 ? 'bg-neutral-900 dark:bg-white' : 'bg-neutral-100 dark:bg-neutral-800'}`}
        >
          <Text
            className={`text-footnote font-semibold ${current === 0 ? 'text-white dark:text-neutral-900' : 'text-neutral-700 dark:text-neutral-200'}`}
          >
            Auto
          </Text>
        </Pressable>
        {filaments.map((name, i) => {
          const slot = i + 1 // libslic3r extruder ids are 1-based
          const isCurrent = current === slot
          const color = colors[i]
          return (
            <Pressable
              key={slot}
              onPress={() => pick(slot)}
              className={`flex-row items-center gap-1.5 rounded-full px-3 py-1.5 ${isCurrent ? 'bg-neutral-900 dark:bg-white' : 'bg-neutral-100 dark:bg-neutral-800'}`}
            >
              {color !== undefined ? (
                <View className="h-2.5 w-2.5 rounded-full" style={{ backgroundColor: color }} />
              ) : null}
              <Text
                className={`text-footnote font-semibold ${isCurrent ? 'text-white dark:text-neutral-900' : 'text-neutral-700 dark:text-neutral-200'}`}
                numberOfLines={1}
              >
                {slot} · {name.length > 22 ? name.slice(0, 21) + '…' : name}
              </Text>
            </Pressable>
          )
        })}
      </View>
    </View>
  )
}

// Directional nudge pad. Fills the gap until a proper on-plate drag gizmo lands
// in native — X/Y numeric fields are precise but tedious, so give the user a
// grid of arrow buttons whose step size they can pick between 1 / 5 / 10 mm.
// The plate's X axis is "right" (finger nudge → +X) and Y is "away" (up-arrow → +Y).
function NudgePad({
  position,
  onNudge,
}: {
  position: { x: number; y: number }
  onNudge: (dx: number, dy: number) => void
}): React.JSX.Element {
  const [step, setStep] = useState<1 | 5 | 10>(5)
  const nudge = (dx: number, dy: number) => (): void => {
    haptic('tap')
    onNudge(dx * step, dy * step)
  }
  const btn =
    'flex-1 items-center justify-center rounded-xl bg-neutral-100 dark:bg-neutral-800 py-3 active:opacity-60'
  const arrow = 'text-title2 font-semibold text-neutral-800 dark:text-neutral-100'
  return (
    <View className="mt-3 gap-2">
      <View className="flex-row items-center gap-2">
        <Text className="text-caption uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
          Nudge step
        </Text>
        {[1, 5, 10].map((s) => (
          <Pressable
            key={s}
            onPress={() => {
              haptic('toggle')
              setStep(s as 1 | 5 | 10)
            }}
            className={`rounded-lg px-3 py-1 ${step === s ? 'bg-blue-500' : 'bg-neutral-100 dark:bg-neutral-800'}`}
          >
            <Text
              className={`text-footnote font-semibold ${step === s ? 'text-white' : 'text-neutral-700 dark:text-neutral-300'}`}
            >
              {s} mm
            </Text>
          </Pressable>
        ))}
      </View>
      <View className="flex-row justify-center">
        <Pressable onPress={nudge(0, 1)} className={btn} style={{ maxWidth: 96 }}>
          <Text className={arrow}>↑</Text>
        </Pressable>
      </View>
      <View className="flex-row gap-2">
        <Pressable onPress={nudge(-1, 0)} className={btn}>
          <Text className={arrow}>←</Text>
        </Pressable>
        <View className="flex-1 items-center justify-center rounded-xl bg-neutral-50 dark:bg-neutral-900 py-3">
          <Text className="text-footnote tabular-nums text-neutral-500 dark:text-neutral-400">
            X {position.x.toFixed(1)} · Y {position.y.toFixed(1)}
          </Text>
        </View>
        <Pressable onPress={nudge(1, 0)} className={btn}>
          <Text className={arrow}>→</Text>
        </Pressable>
      </View>
      <View className="flex-row justify-center">
        <Pressable onPress={nudge(0, -1)} className={btn} style={{ maxWidth: 96 }}>
          <Text className={arrow}>↓</Text>
        </Pressable>
      </View>
    </View>
  )
}

function NumberField({
  label,
  value,
  onCommit,
}: {
  label: string
  value: number
  onCommit: (v: number) => void
}): React.JSX.Element {
  const [draft, setDraft] = useState<string | null>(null)
  const shown = draft ?? value.toFixed(1)
  return (
    <View className="flex-1">
      <Text className="mb-1 text-caption uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
        {label}
      </Text>
      <TextInput
        value={shown}
        onChangeText={setDraft}
        keyboardType="numeric"
        onBlur={() => {
          if (draft !== null) {
            const n = Number.parseFloat(draft)
            if (!Number.isNaN(n)) onCommit(n)
          }
          setDraft(null)
        }}
        className="rounded-xl bg-neutral-100 px-3 py-2 text-subhead text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100"
      />
    </View>
  )
}
