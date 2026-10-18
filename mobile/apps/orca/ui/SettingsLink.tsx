import { useRouter } from 'expo-router'
import React from 'react'
import { Pressable } from 'react-native'

import { Icon, useIconTint } from '@/ui/Icon'

// Header entry point for the settings hub (account, presets, help, storage,
// about). Lives in the home screen's headerLeft, which is free because index is
// the stack root and has no back button.
export function SettingsLink(): React.JSX.Element {
  const router = useRouter()
  const tint = useIconTint()
  return (
    <Pressable
      onPress={() => router.push('/settings')}
      hitSlop={12}
      accessibilityLabel="Settings"
      className="rounded-full active:opacity-60"
    >
      <Icon name="gear" size={22} color={tint} />
    </Pressable>
  )
}
