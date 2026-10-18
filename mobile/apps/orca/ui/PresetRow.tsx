import React from 'react'
import { Pressable, Text, View } from 'react-native'

import { Icon } from '@/ui/Icon'

type Props = {
  label: string
  value: string
  /** Optional swatch shown before the label — used by the Filament row to show
   * the currently-selected filament colour so multi-material setups are
   * distinguishable at a glance. */
  swatchColor?: string
  disabled?: boolean
  onPickPress: () => void
  onEditPress: () => void
  last?: boolean
}

export function PresetRow({
  label,
  value,
  swatchColor,
  disabled = false,
  onPickPress,
  onEditPress,
  last = false,
}: Props): React.JSX.Element {
  const isEmpty = value.length === 0 || value.startsWith('Default ')
  return (
    <View
      className={`flex-row items-center px-4 py-3 ${last ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
    >
      <Pressable
        onPress={disabled ? undefined : onPickPress}
        className={`flex-1 flex-row items-center gap-2 ${disabled ? 'opacity-40' : 'active:opacity-60'}`}
        accessibilityRole="button"
        accessibilityLabel={`${label}: ${!isEmpty ? value : 'not chosen'}`}
      >
        {swatchColor !== undefined && !isEmpty ? (
          <View
            className="h-3 w-3 rounded-full border border-neutral-300 dark:border-neutral-700"
            style={{ backgroundColor: swatchColor }}
          />
        ) : null}
        <View className="flex-1">
          <Text className="text-subhead font-semibold tracking-tight text-neutral-900 dark:text-neutral-100">
            {label}
          </Text>
          <Text
            numberOfLines={1}
            className={`mt-0.5 text-footnote ${isEmpty ? 'italic text-neutral-400 dark:text-neutral-500' : 'text-neutral-500 dark:text-neutral-400'}`}
          >
            {isEmpty ? 'Tap to choose' : value}
          </Text>
        </View>
        <Icon name="chevron" size={16} />
      </Pressable>
      <Pressable
        hitSlop={8}
        onPress={disabled ? undefined : onEditPress}
        className={`ml-3 rounded-full bg-neutral-100 px-3 py-1.5 dark:bg-neutral-800 ${disabled ? 'opacity-40' : 'active:opacity-60'}`}
        accessibilityRole="button"
        accessibilityLabel={`Edit ${label} preset`}
      >
        <Text className="text-label font-semibold uppercase tracking-wider text-neutral-600 dark:text-neutral-300">
          Edit
        </Text>
      </Pressable>
    </View>
  )
}
