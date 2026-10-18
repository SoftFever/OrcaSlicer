import { Directory, File, Paths } from 'expo-file-system'
import { Stack, useRouter } from 'expo-router'
import React, { useState } from 'react'
import { ScrollView, Text, TextInput, View } from 'react-native'

import { useSession } from '@/lib/core'
import { CALIBRATIONS, modelUrlFor, type Calibration } from '@/lib/calibrations'
import { haptic } from '@/lib/haptics'
import { nativePath } from '@/lib/profiles'
import { showToast } from '@/lib/toast'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'

// The Calibration screen is a launcher for common test wizards. Each row applies
// a set of process-preset edits — mostly a layer_change_gcode block that steps a
// parameter — and points the user at a model to import next. This is a v1 that
// leans on the user to bring their own model; a follow-up bundles the same .3mf
// tests desktop ships once the mobile core reads Draco (.drc) too.

export default function CalibrationScreen(): React.JSX.Element {
  const router = useRouter()
  const session = useSession()
  if (session === null) {
    return (
      <View className="flex-1 items-center justify-center bg-neutral-50 dark:bg-neutral-950">
        <Stack.Screen options={{ title: 'Calibration' }} />
        <Text className="text-subhead text-neutral-500">Loading…</Text>
      </View>
    )
  }
  return (
    <ScrollView
      className="bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="px-4 pt-2 pb-24 gap-4"
      contentInsetAdjustmentBehavior="automatic"
    >
      <Stack.Screen options={{ title: 'Calibration' }} />
      {CALIBRATIONS.map((c, i) => (
        <CalibrationCard
          key={c.id}
          calibration={c}
          index={i}
          onApplied={() => {
            haptic('success')
            showToast(`Applied ${c.name}`, 'success')
            router.back()
          }}
        />
      ))}
    </ScrollView>
  )
}

function CalibrationCard({
  calibration: c,
  index,
  onApplied,
}: {
  calibration: Calibration
  index: number
  onApplied: () => void
}): React.JSX.Element {
  const session = useSession()
  const [values, setValues] = useState<Record<string, number>>(
    Object.fromEntries(c.param.map((p) => [p.key, p.default])),
  )
  const [downloading, setDownloading] = useState(false)
  const apply = (): void => {
    if (session === null) return
    const { warnings } = c.apply(session, values)
    if (warnings.length > 0) {
      showToast(warnings.join(' · '), 'error')
      return
    }
    onApplied()
  }
  const modelUrl = modelUrlFor(c)
  const downloadAndLoad = async (): Promise<void> => {
    if (session === null || modelUrl === null) return
    setDownloading(true)
    try {
      // Cache each calibration file under Documents/calib/<name>.3mf so a repeat
      // run does not re-fetch — MMKV-side metadata is overkill for a handful of
      // known filenames. Save always overwrites so an upstream repo update lands.
      const dir = new Directory(Paths.document, 'calib')
      if (!dir.exists) dir.create({ intermediates: true })
      const target = new File(dir, `${c.id}.3mf`)
      if (target.exists) target.delete()
      const response = await fetch(modelUrl)
      if (!response.ok) throw new Error(`HTTP ${response.status} fetching ${modelUrl}`)
      const bytes = new Uint8Array(await response.arrayBuffer())
      // expo-file-system exposes File.write for binary via the `Uint8Array`
      // overload — matches what Sharing/Downloads use elsewhere.
      target.write(bytes)
      const path = nativePath(target.parentDirectory) + '/' + c.id + '.3mf'
      await session.openProject(path)
      const { warnings } = c.apply(session, values)
      if (warnings.length > 0) showToast(warnings.join(' · '), 'error')
      else onApplied()
    } catch (e) {
      showToast(`Download failed: ${String(e)}`, 'error')
    } finally {
      setDownloading(false)
    }
  }
  return (
    <Card title={c.name} padded index={index}>
      <Text className="text-callout leading-5 text-neutral-500 dark:text-neutral-400">
        {c.description}
      </Text>
      <View className="mt-2 rounded-xl bg-neutral-100 p-3 dark:bg-neutral-800">
        <Text className="text-caption font-semibold uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
          Recommended model
        </Text>
        <Text className="mt-0.5 text-footnote text-neutral-700 dark:text-neutral-300">
          {c.suggestedModel}
        </Text>
      </View>
      <View className="mt-3 flex-row gap-3">
        {c.param.map((p) => (
          <View key={p.key} className="flex-1">
            <Text className="mb-1 text-caption uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
              {p.label}
            </Text>
            <TextInput
              defaultValue={String(values[p.key] ?? p.default)}
              keyboardType="numeric"
              onEndEditing={(e) => {
                const n = Number.parseFloat(e.nativeEvent.text)
                if (!Number.isNaN(n)) setValues((v) => ({ ...v, [p.key]: n }))
              }}
              className="rounded-xl bg-neutral-100 px-3 py-2 text-subhead text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100"
            />
          </View>
        ))}
      </View>
      <View className="mt-3 gap-2">
        {modelUrl !== null ? (
          <Button
            title={downloading ? 'Downloading…' : 'Download & load model + apply'}
            onPress={() => { void downloadAndLoad() }}
            disabled={downloading}
            fullWidth
          />
        ) : null}
        <Button
          title={modelUrl !== null ? 'Apply preset only (keep current plate)' : 'Apply to current preset'}
          variant={modelUrl !== null ? 'secondary' : 'primary'}
          onPress={apply}
          fullWidth
        />
      </View>
    </Card>
  )
}
