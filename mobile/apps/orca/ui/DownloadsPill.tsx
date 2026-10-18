import React from 'react'
import { ActivityIndicator, Text, View } from 'react-native'
import { useShallow } from 'zustand/react/shallow'

import { selectActive, useDownloadsStore } from '@/lib/downloads'

// Small counter that sits in the header. Renders nothing when no downloads
// are in flight; otherwise shows a spinner + a compact progress summary of
// the frontmost download. Tapping it belongs to the caller (we ship it as a
// pure View so it composes into Link asChild if needed).
export function DownloadsPill(): React.JSX.Element | null {
  // Wrap the array-returning selector so Zustand bails out on shallow-equal
  // results; without this filter() produces a fresh array every render and
  // the subscribe → set cycle loops.
  const active = useDownloadsStore(useShallow(selectActive))
  if (active.length === 0) return null
  const head = active[0]
  if (head === undefined) return null
  const pct = head.total > 0 ? Math.round((head.done / head.total) * 100) : null
  // Only the percentage in the pill so it never crowds the segmented control on
  // iPhone-width headers; the download's model / vendor name is on the model
  // list itself and in the completion notification.
  return (
    <View className="mr-2 flex-row items-center gap-1.5 rounded-full bg-neutral-100 px-2.5 py-1 dark:bg-neutral-800">
      <ActivityIndicator size="small" />
      {pct !== null ? (
        <Text className="text-caption font-semibold tabular-nums text-neutral-700 dark:text-neutral-200">
          {pct}%
        </Text>
      ) : active.length > 1 ? (
        <Text className="text-caption font-semibold text-neutral-700 dark:text-neutral-200">
          {active.length}
        </Text>
      ) : null}
    </View>
  )
}
