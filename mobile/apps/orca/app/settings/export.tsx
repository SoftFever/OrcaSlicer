import { Stack } from 'expo-router'
import React, { useMemo, useState } from 'react'
import { Pressable, ScrollView, Text, View } from 'react-native'
import type { PresetKind, PresetRef } from 'react-native-orca-core'

import { useCore } from '@/lib/core'
import { exportPresetBundle } from '@/lib/presetFiles'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'
import { EmptyState } from '@/ui/EmptyState'
import { Icon, useIconTint } from '@/ui/Icon'

// Pick any mix of user presets and share them as one archive. One printer plus
// others becomes an .orca_printer; anything else an .orca_bundle. Built-in
// presets are not listed: the desktop does not export them either.

const KINDS: Array<{ kind: PresetKind; title: string }> = [
  { kind: 'printer', title: 'Printers' },
  { kind: 'filament', title: 'Filaments' },
  { kind: 'process', title: 'Processes' },
]

const key = (p: PresetRef): string => `${p.kind}::${p.name}`

export default function ExportPresetsScreen(): React.JSX.Element {
  const { session, ready } = useCore()
  const tint = useIconTint()
  const [picked, setPicked] = useState<Set<string>>(() => new Set())
  const [busy, setBusy] = useState(false)

  const groups = useMemo(
    () =>
      KINDS.map(({ kind, title }) => ({
        kind,
        title,
        presets: session === null || !ready ? [] : session.presets(kind).filter((p) => p.isUser).map((p) => p.name),
      })).filter((g) => g.presets.length > 0),
    // `ready` flips once presets are loaded; a cold start straight into this screen lands before that.
    [session, ready],
  )
  const total = groups.reduce((n, g) => n + g.presets.length, 0)

  const toggle = (ref: PresetRef): void => {
    setPicked((prev) => {
      const next = new Set(prev)
      const k = key(ref)
      if (next.has(k)) next.delete(k)
      else next.add(k)
      return next
    })
  }

  const selected: PresetRef[] = groups.flatMap((g) => g.presets.filter((name) => picked.has(key({ kind: g.kind, name }))).map((name) => ({ kind: g.kind, name })))
  const printers = selected.filter((p) => p.kind === 'printer').length

  const share = async (): Promise<void> => {
    if (session === null || selected.length === 0) return
    setBusy(true)
    try {
      await exportPresetBundle(session, selected)
    } finally {
      setBusy(false)
    }
  }

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <Stack.Screen options={{ title: 'Export presets' }} />
      <ScrollView contentContainerClassName="gap-5 p-5 pb-32">
        {total === 0 ? (
          <Card>
            <EmptyState
              icon="export"
              title="No presets of your own yet"
              body="Save a printer, filament or process under your own name and it will show up here for export."
            />
          </Card>
        ) : (
          groups.map((g, i) => (
            <Card key={g.kind} title={g.title} padded={false} index={i}>
              {g.presets.map((name, j) => {
                const ref: PresetRef = { kind: g.kind, name }
                const on = picked.has(key(ref))
                return (
                  <Pressable
                    key={name}
                    onPress={() => toggle(ref)}
                    accessibilityRole="checkbox"
                    accessibilityState={{ checked: on }}
                    className={`flex-row items-center gap-3 px-4 py-3 active:bg-neutral-100 dark:active:bg-neutral-800 ${j === g.presets.length - 1 ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
                  >
                    <Icon name={on ? 'check' : 'circle'} size={20} color={on ? tint : undefined} />
                    <Text className="flex-1 text-subhead text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                      {name}
                    </Text>
                  </Pressable>
                )
              })}
            </Card>
          ))
        )}
      </ScrollView>
      {total > 0 ? (
        <View className="absolute inset-x-0 bottom-0 border-t border-neutral-100 bg-white px-5 pb-8 pt-3 dark:border-neutral-800 dark:bg-neutral-900">
          <Text className="mb-2 text-footnote text-neutral-500 dark:text-neutral-400">
            {selected.length === 0
              ? 'Pick the presets to share.'
              : printers === 1
                ? `${selected.length} selected · exported as an .orca_printer bundle`
                : `${selected.length} selected · exported as an .orca_bundle`}
          </Text>
          <Button title={busy ? 'Exporting…' : 'Share bundle'} size="lg" fullWidth disabled={busy || selected.length === 0} onPress={() => void share()} />
        </View>
      ) : null}
    </View>
  )
}
