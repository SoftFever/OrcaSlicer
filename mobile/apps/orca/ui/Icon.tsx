import { MaterialIcons } from '@expo/vector-icons'
import { SymbolView, type SymbolWeight } from 'expo-symbols'
import React from 'react'
import { Platform } from 'react-native'
import { useColorScheme } from 'nativewind'

import { colors } from '@/lib/theme'

// The app's icon set: SF Symbols on iOS, Material Icons elsewhere, behind one
// name so screens never spell a platform symbol. Add a name here, not inline.

const ICONS = {
  gear: { sf: 'gearshape', material: 'settings' },
  info: { sf: 'info.circle', material: 'info-outline' },
  help: { sf: 'questionmark.circle', material: 'help-outline' },
  cloud: { sf: 'icloud', material: 'cloud-queue' },
  chevron: { sf: 'chevron.right', material: 'chevron-right' },
  open: { sf: 'arrow.up.right', material: 'open-in-new' },
  expand: { sf: 'arrow.up.left.and.arrow.down.right', material: 'fullscreen' },
  undo: { sf: 'arrow.uturn.backward', material: 'undo' },
  redo: { sf: 'arrow.uturn.forward', material: 'redo' },
  plus: { sf: 'plus', material: 'add' },
  minus: { sf: 'minus', material: 'remove' },
  import: { sf: 'square.and.arrow.down', material: 'file-download' },
  export: { sf: 'square.and.arrow.up', material: 'ios-share' },
  printer: { sf: 'printer', material: 'print' },
  cube: { sf: 'cube', material: 'view-in-ar' },
  layers: { sf: 'square.3.layers.3d', material: 'layers' },
  storage: { sf: 'internaldrive', material: 'storage' },
  network: { sf: 'network', material: 'router' },
  check: { sf: 'checkmark.circle.fill', material: 'check-circle' },
  circle: { sf: 'circle', material: 'radio-button-unchecked' },
} as const

export type IconName = keyof typeof ICONS

type Props = {
  name: IconName
  size?: number
  /** Explicit colour; defaults to a neutral that reads on both schemes. */
  color?: string
  weight?: SymbolWeight
}

export function Icon({ name, size = 20, color, weight = 'medium' }: Props): React.JSX.Element {
  const { colorScheme } = useColorScheme()
  const tint = color ?? (colorScheme === 'dark' ? colors.iconMuted.dark : colors.iconMuted.light)
  const icon = ICONS[name]
  if (Platform.OS === 'ios') {
    return (
      <SymbolView
        name={icon.sf}
        size={size}
        tintColor={tint}
        weight={weight}
        resizeMode="scaleAspectFit"
        fallback={<MaterialIcons name={icon.material} size={size} color={tint} />}
      />
    )
  }
  return <MaterialIcons name={icon.material} size={size} color={tint} />
}

/** The accent tint for an interactive icon (header buttons, links). */
export function useIconTint(): string {
  const { colorScheme } = useColorScheme()
  return colorScheme === 'dark' ? colors.accent[400] : colors.accent[600]
}
