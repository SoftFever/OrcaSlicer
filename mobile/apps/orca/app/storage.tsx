import { Stack } from 'expo-router'
import React, { useState } from 'react'
import { Alert, ScrollView, Text, View } from 'react-native'

import { formatBytes, usageBuckets, type Bucket } from '@/lib/diskUsage'
import { haptic } from '@/lib/haptics'
import { resetOnboarding } from '@/lib/onboarding'
import { storage } from '@/lib/storage'
import { showToast } from '@/lib/toast'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'

// Storage manager. Enumerates the on-device buckets and offers Clear on each.
// The screen re-fetches sizes when a bucket is cleared so the row shrinks in
// place — no full-page refresh, no navigation round-trip.

export default function StorageScreen(): React.JSX.Element {
  const [buckets, setBuckets] = useState<Bucket[]>(() => usageBuckets())
  const total = buckets.reduce((sum, b) => sum + b.size, 0)

  const clear = (b: Bucket): void => {
    const doIt = (): void => {
      b.clear()
      setBuckets(usageBuckets())
      haptic('success')
      showToast(`Cleared ${b.label.toLowerCase()}`, 'success')
    }
    if (b.destructive) {
      Alert.alert(`Remove ${b.label.toLowerCase()}?`, b.description, [
        { text: 'Cancel', style: 'cancel' },
        { text: 'Remove', style: 'destructive', onPress: doIt },
      ])
    } else {
      doIt()
    }
  }

  return (
    <ScrollView
      className="flex-1 bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="gap-4 p-4 pb-10"
    >
      <Stack.Screen options={{ title: 'Storage' }} />
      <Card padded index={0}>
        <View className="items-start gap-1">
          <Text className="text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
            App data on this device
          </Text>
          <Text className="text-title1 font-bold tracking-tight text-neutral-900 dark:text-neutral-100">
            {formatBytes(total)}
          </Text>
          <Text className="text-footnote leading-5 text-neutral-500 dark:text-neutral-400">
            Sizes here cover only the buckets Orca manages. iOS' Settings → General → iPhone Storage
            shows the full app footprint including the JS bundle and native frameworks.
          </Text>
        </View>
      </Card>
      <Card padded index={1}>
        <View className="gap-2">
          <Text className="text-headline font-semibold text-neutral-900 dark:text-neutral-100">
            Reset app
          </Text>
          <Text className="text-footnote leading-5 text-neutral-500 dark:text-neutral-400">
            Nukes every bucket below, clears cached queries and preferences, and shows the
            onboarding again on next launch. Printer destinations you added by hand are gone too.
            There is no undo.
          </Text>
          <View className="pt-1">
            <Button
              title="Erase app data"
              variant="destructive"
              onPress={() => {
                Alert.alert(
                  'Erase app data?',
                  'This deletes every profile, preset, autosaved plate, cached model, saved preference and pinned printer. There is no undo.',
                  [
                    { text: 'Cancel', style: 'cancel' },
                    {
                      text: 'Erase',
                      style: 'destructive',
                      onPress: () => {
                        for (const b of buckets) b.clear()
                        storage.clearAll()
                        resetOnboarding()
                        setBuckets(usageBuckets())
                        haptic('warn')
                        showToast('App data erased. Restart to see onboarding.', 'success')
                      },
                    },
                  ],
                )
              }}
            />
          </View>
        </View>
      </Card>
      {buckets.map((b, i) => (
        <Card padded key={b.id} index={i + 2}>
          <View className="gap-2">
            <View className="flex-row items-baseline justify-between">
              <Text className="text-headline font-semibold text-neutral-900 dark:text-neutral-100">
                {b.label}
              </Text>
              <Text className="text-callout font-medium tabular-nums text-neutral-500 dark:text-neutral-400">
                {formatBytes(b.size)}
              </Text>
            </View>
            <Text className="text-footnote leading-5 text-neutral-500 dark:text-neutral-400">
              {b.description}
            </Text>
            <View className="pt-1">
              <Button
                title={b.size === 0 ? 'Empty' : 'Clear'}
                variant={b.destructive ? 'destructive' : 'secondary'}
                disabled={b.size === 0}
                onPress={() => clear(b)}
              />
            </View>
          </View>
        </Card>
      ))}
    </ScrollView>
  )
}
