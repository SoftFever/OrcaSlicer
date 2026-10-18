import React from 'react'
import { View } from 'react-native'

type Props = { percent: number }

// Slim progress track; percent is clamped so callers do not have to.
export function ProgressBar({ percent }: Props): React.JSX.Element {
  const p = Math.max(0, Math.min(100, percent))
  return (
    <View className="h-1 overflow-hidden rounded bg-gray-200 dark:bg-neutral-800">
      <View className="h-full rounded bg-blue-500" style={{ width: `${p}%` }} />
    </View>
  )
}
