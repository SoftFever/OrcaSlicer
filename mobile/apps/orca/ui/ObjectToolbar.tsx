import React from 'react'
import { Platform, Pressable, Text, View } from 'react-native'
import type { ObjectTool } from 'react-native-orca-core'

import { haptic } from '@/lib/haptics'

// Floating toolbar over the viewport for object interactions. Shown only when
// an object is selected. Each chip sets the native drag intent — a 1-finger
// drag on the selected object then translates / rotates / uniform-scales
// depending on which one is active. The ⋯ chip opens the full object detail
// sheet (numeric position, filament, cut plane, mirror, delete).
//
// Styling aims for iOS 26 Liquid Glass look without pulling the native module
// (`expo-glass-effect` is a transitive dep and not exposed here): a heavily
// translucent surface, a subtle inner border for the "glass edge" highlight,
// and a strong active-tint so the current tool is unambiguous over any
// underlying viewport content.

type Props = {
  visible: boolean
  tool: ObjectTool
  onChangeTool: (tool: ObjectTool) => void
  onOpenDetails?: () => void
}

type Entry = {
  tool: ObjectTool
  glyph: string
  label: string
}

const ENTRIES: readonly Entry[] = [
  { tool: 'move', glyph: '⇕', label: 'Move' },
  { tool: 'rotate', glyph: '↻', label: 'Rotate' },
  { tool: 'scale', glyph: '⇔', label: 'Scale' },
]

// A background just dark enough to read chip labels over a bright viewport,
// just translucent enough to still read as "glass". Same value both themes;
// dark mode makes the viewport dark and the pill blends naturally.
const glassBg = 'rgba(28,28,30,0.62)'

export function ObjectToolbar({
  visible,
  tool,
  onChangeTool,
  onOpenDetails,
}: Props): React.JSX.Element | null {
  if (!visible) return null

  return (
    <View
      style={{
        borderRadius: 26,
        overflow: 'hidden',
        backgroundColor: glassBg,
        borderWidth: Platform.OS === 'ios' ? 0.5 : 0,
        borderColor: 'rgba(255,255,255,0.18)',
        shadowColor: '#000',
        shadowOpacity: 0.25,
        shadowRadius: 12,
        shadowOffset: { width: 0, height: 6 },
      }}
    >
      <View className="flex-row items-center gap-1 px-1.5 py-1.5">
        {ENTRIES.map((e) => {
          const active = tool === e.tool
          return (
            <Pressable
              key={e.tool}
              onPress={() => {
                haptic('toggle')
                onChangeTool(e.tool)
              }}
              accessibilityLabel={e.label}
              accessibilityState={{ selected: active }}
              className={`min-w-[76px] items-center justify-center rounded-full px-4 py-2 active:opacity-70 ${active ? 'bg-blue-500' : ''}`}
            >
              <Text className="text-footnote font-semibold text-white">
                {e.glyph} {e.label}
              </Text>
            </Pressable>
          )
        })}
        {onOpenDetails !== undefined ? (
          <>
            <View className="mx-1 h-6 w-px bg-white/25" />
            <Pressable
              onPress={() => {
                haptic('tap')
                onOpenDetails()
              }}
              accessibilityLabel="Object details"
              className="items-center justify-center rounded-full px-3 py-2 active:opacity-70"
            >
              <Text className="text-subhead font-semibold text-white">⋯</Text>
            </Pressable>
          </>
        ) : null}
      </View>
    </View>
  )
}
