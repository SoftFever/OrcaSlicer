import React from 'react'
import { Pressable, Text, View } from 'react-native'
import { SafeAreaView } from 'react-native-safe-area-context'

import { useToastStore } from '@/lib/toast'

// Top-level toast overlay. Reads from the shared store and stacks up to three
// active toasts; older ones sit visually behind newer ones. Pressing dismisses.
// Kept as pointerEvents="box-none" so touches on the underlying view (nav bar
// segmented control, cards) still register — only the toast chips are hit.

const toneClasses = {
  info:    'bg-neutral-900 dark:bg-neutral-100',
  success: 'bg-emerald-600 dark:bg-emerald-500',
  error:   'bg-red-600 dark:bg-red-500',
} as const

const textClasses = {
  info:    'text-white dark:text-neutral-900',
  success: 'text-white',
  error:   'text-white',
} as const

export function ToastHost(): React.JSX.Element {
  const toasts = useToastStore((s) => s.toasts)
  const dismiss = useToastStore((s) => s.dismiss)
  if (toasts.length === 0) return <View />
  return (
    <SafeAreaView pointerEvents="box-none" style={{ position: 'absolute', inset: 0 }}>
      <View pointerEvents="box-none" className="items-center gap-2 pt-2">
        {toasts.slice(-3).map((t) => (
          <Pressable
            key={t.id}
            onPress={() => dismiss(t.id)}
            className={`max-w-[92%] rounded-full px-4 py-2 ${toneClasses[t.kind]} active:opacity-80`}
            accessibilityLabel={t.message}
          >
            <Text className={`text-footnote font-semibold ${textClasses[t.kind]}`} numberOfLines={2}>
              {t.message}
            </Text>
          </Pressable>
        ))}
      </View>
    </SafeAreaView>
  )
}
