import React, { useState } from 'react'
import { Pressable, Text, View } from 'react-native'

type Tone = 'info' | 'warning' | 'critical' | 'success'

type Props = {
  tone?: Tone
  title?: string
  children: React.ReactNode
  action?: React.ReactNode
  /** Collapse a long string body to this many lines with a "Show all" toggle. Ignored for non-string children. */
  clampLines?: number
  className?: string
}

const container: Record<Tone, string> = {
  info: 'bg-blue-50 dark:bg-blue-500/10',
  warning: 'bg-amber-50 dark:bg-amber-500/10',
  critical: 'bg-red-50 dark:bg-red-500/10',
  success: 'bg-green-50 dark:bg-green-500/10',
}
const bar: Record<Tone, string> = {
  info: 'bg-blue-500',
  warning: 'bg-amber-500',
  critical: 'bg-red-500',
  success: 'bg-green-500',
}
const titleColor: Record<Tone, string> = {
  info: 'text-blue-800 dark:text-blue-200',
  warning: 'text-amber-800 dark:text-amber-200',
  critical: 'text-red-800 dark:text-red-200',
  success: 'text-green-800 dark:text-green-200',
}
const bodyColor: Record<Tone, string> = {
  info: 'text-blue-900/80 dark:text-blue-100/80',
  warning: 'text-amber-900/80 dark:text-amber-100/80',
  critical: 'text-red-900/80 dark:text-red-100/80',
  success: 'text-green-900/80 dark:text-green-100/80',
}
const label: Record<Tone, string> = {
  info: 'Note',
  warning: 'Heads up',
  critical: 'Error',
  success: 'Done',
}

export function Alert({
  tone = 'info',
  title,
  children,
  action,
  clampLines = 4,
  className,
}: Props): React.JSX.Element {
  const [expanded, setExpanded] = useState(false)
  const isString = typeof children === 'string'
  const willClamp = isString && !expanded && clampLines > 0
  return (
    <View className={`overflow-hidden rounded-2xl ${container[tone]} ${className ?? ''}`}>
      <View className="flex-row">
        <View className={`w-1 ${bar[tone]}`} />
        <View className="flex-1 gap-1 p-3">
          <Text
            className={`text-caption font-semibold uppercase tracking-caps ${titleColor[tone]}`}
          >
            {title ?? label[tone]}
          </Text>
          {isString ? (
            <>
              <Text
                className={`text-footnote leading-5 ${bodyColor[tone]}`}
                numberOfLines={willClamp ? clampLines : undefined}
              >
                {children}
              </Text>
              <Pressable onPress={() => setExpanded((e) => !e)} hitSlop={6}>
                <Text className={`pt-1 text-label font-semibold ${titleColor[tone]}`}>
                  {expanded ? 'Show less' : 'Show all'}
                </Text>
              </Pressable>
            </>
          ) : (
            children
          )}
          {action !== undefined ? <View className="pt-2">{action}</View> : null}
        </View>
      </View>
    </View>
  )
}
