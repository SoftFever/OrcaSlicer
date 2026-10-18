import React, { useCallback, useEffect, useState } from 'react'
import Animated, { FadeInDown, LinearTransition } from 'react-native-reanimated'
import { AccessibilityInfo, Pressable, Text, View } from 'react-native'

import { haptic } from '@/lib/haptics'
import { storage } from '@/lib/storage'

// Cached across every Card instance — the OS setting rarely changes, and the
// listener below keeps it live. Read synchronously into initial state so the
// very first render already respects the setting instead of playing one
// animation and then switching.
let cachedReduceMotion = false
const reduceMotionListeners = new Set<(v: boolean) => void>()
void AccessibilityInfo.isReduceMotionEnabled().then((v) => {
  cachedReduceMotion = v
  reduceMotionListeners.forEach((listener) => listener(v))
})
AccessibilityInfo.addEventListener('reduceMotionChanged', (v) => {
  cachedReduceMotion = v
  reduceMotionListeners.forEach((listener) => listener(v))
})

function useReduceMotion(): boolean {
  const [v, setV] = useState(cachedReduceMotion)
  useEffect(() => {
    reduceMotionListeners.add(setV)
    return () => {
      reduceMotionListeners.delete(setV)
    }
  }, [])
  return v
}

type Props = {
  title?: string
  subtitle?: string
  action?: React.ReactNode
  padded?: boolean
  children: React.ReactNode
  className?: string
  /** Optional stagger index for the entering animation. */
  index?: number
  /** When set, the card header becomes a tap target that hides / shows the body. Persisted across app launches under `card:collapsed:<key>`. */
  collapsible?: boolean
  collapseKey?: string
  defaultCollapsed?: boolean
}

// Grouped surface with soft shadow, no border. Reanimated's FadeInDown gives
// it a subtle rise on mount; passing `index` staggers a list of cards.
// Collapsible mode tucks the body away with a spring layout transition and
// persists the open/closed state under an MMKV key so the layout the user
// arranged sticks between launches.
export function Card({
  title,
  subtitle,
  action,
  padded = true,
  children,
  className,
  index = 0,
  collapsible = false,
  collapseKey,
  defaultCollapsed = false,
}: Props): React.JSX.Element {
  const reduceMotion = useReduceMotion()
  const persistKey = collapsible ? `card:collapsed:${collapseKey ?? title ?? ''}` : null
  const [collapsed, setCollapsed] = useState<boolean>(() => {
    if (persistKey === null) return false
    const stored = storage.getBoolean(persistKey)
    return stored ?? defaultCollapsed
  })
  const toggle = useCallback(() => {
    haptic('toggle')
    setCollapsed((prev) => {
      const next = !prev
      if (persistKey !== null) storage.set(persistKey, next)
      return next
    })
  }, [persistKey])

  const hasHeader =
    title !== undefined || subtitle !== undefined || action !== undefined || collapsible

  const Header = (
    <View className="flex-row items-end justify-between px-1">
      <View className="flex-1 pr-2">
        {title !== undefined ? (
          <View className="flex-row items-center gap-2">
            <Text className="text-caption font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
              {title}
            </Text>
            {collapsible ? (
              <Text
                className={`text-footnote leading-3 text-neutral-400 dark:text-neutral-500 ${collapsed ? '' : 'rotate-90'}`}
                style={{ transform: [{ rotate: collapsed ? '0deg' : '90deg' }] }}
              >
                ›
              </Text>
            ) : null}
          </View>
        ) : null}
        {subtitle !== undefined ? (
          <Text
            className="mt-1 text-footnote text-neutral-500 dark:text-neutral-400"
            numberOfLines={1}
          >
            {subtitle}
          </Text>
        ) : null}
      </View>
      {action}
    </View>
  )

  return (
    <Animated.View
      layout={reduceMotion ? undefined : LinearTransition.duration(220)}
      entering={reduceMotion ? undefined : FadeInDown.duration(260).delay(index * 40)}
      className={className}
    >
      {hasHeader ? (
        collapsible ? (
          <Pressable onPress={toggle} hitSlop={4} className="mb-2 active:opacity-60">
            {Header}
          </Pressable>
        ) : (
          <View className="mb-2">{Header}</View>
        )
      ) : null}
      {!collapsed ? (
        <Animated.View
          layout={reduceMotion ? undefined : LinearTransition.duration(200)}
          className={`overflow-hidden rounded-3xl bg-white shadow-soft dark:bg-neutral-900 ${padded ? 'gap-3 p-4' : ''}`}
        >
          {children}
        </Animated.View>
      ) : null}
    </Animated.View>
  )
}

export function Row({
  last,
  children,
  className,
}: {
  last?: boolean
  children: React.ReactNode
  className?: string
}): React.JSX.Element {
  return (
    <View
      className={`px-4 py-3 ${last === true ? '' : 'border-b border-neutral-100 dark:border-neutral-800'} ${className ?? ''}`}
    >
      {children}
    </View>
  )
}
