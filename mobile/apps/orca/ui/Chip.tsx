import React from 'react'
import { Pressable, Text } from 'react-native'

import { haptic } from '@/lib/haptics'

type Props = {
  label: string
  selected?: boolean
  disabled?: boolean
  onPress?: () => void
  className?: string
}

// Small pill for filters, mode selectors and categories. Selected state fills
// with the near-black primary tint used elsewhere; unselected is quiet on the
// surface. Sized to sit inside a horizontal scroll strip.
export function Chip({
  label,
  selected = false,
  disabled = false,
  onPress,
  className,
}: Props): React.JSX.Element {
  return (
    <Pressable
      onPress={
        disabled
          ? undefined
          : () => {
              haptic('toggle')
              onPress?.()
            }
      }
      className={[
        'rounded-full px-3 py-1.5',
        selected
          ? 'bg-neutral-900 dark:bg-white'
          : 'bg-neutral-100 active:bg-neutral-200 dark:bg-neutral-800 dark:active:bg-neutral-700',
        disabled ? 'opacity-40' : '',
        className ?? '',
      ]
        .join(' ')
        .trim()}
    >
      <Text
        className={
          selected
            ? 'text-footnote font-semibold text-white dark:text-neutral-950'
            : 'text-footnote font-medium text-neutral-700 dark:text-neutral-200'
        }
        numberOfLines={1}
      >
        {label}
      </Text>
    </Pressable>
  )
}
