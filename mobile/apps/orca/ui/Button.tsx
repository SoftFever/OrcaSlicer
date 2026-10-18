import React, { forwardRef } from 'react'

import { colors } from '@/lib/theme'
import { ActivityIndicator, Pressable, Text, type PressableProps } from 'react-native'

import { haptic } from '@/lib/haptics'

type Variant = 'primary' | 'secondary' | 'ghost' | 'destructive'
type Size = 'md' | 'lg'

type Props = Omit<PressableProps, 'style' | 'className'> & {
  title: string
  variant?: Variant
  size?: Size
  loading?: boolean
  fullWidth?: boolean
  className?: string
}

// Dark-neutral primary + quiet secondary. Moves the app off the default iOS
// blue-500 fill everywhere. The active states shift a shade rather than fade
// so a Pressable feels tactile without any bespoke animation.
const sizeContainer: Record<Size, string> = {
  md: 'min-h-[42px] px-4 py-2.5',
  lg: 'min-h-[52px] px-5 py-3.5',
}
const variantContainer: Record<Variant, string> = {
  primary: 'bg-neutral-950 active:bg-neutral-800 dark:bg-white dark:active:bg-neutral-200',
  secondary: 'bg-neutral-100 active:bg-neutral-200 dark:bg-neutral-800 dark:active:bg-neutral-700',
  ghost: 'bg-transparent active:bg-neutral-100 dark:active:bg-neutral-800',
  destructive: 'bg-red-600 active:bg-red-700',
}
const variantText: Record<Variant, string> = {
  primary: 'text-white dark:text-neutral-950',
  secondary: 'text-neutral-900 dark:text-neutral-100',
  ghost: 'text-neutral-900 dark:text-neutral-100',
  destructive: 'text-white',
}
const sizeText: Record<Size, string> = {
  md: 'text-subhead font-semibold tracking-tight',
  lg: 'text-body font-semibold tracking-tight',
}

export const Button = forwardRef<React.ComponentRef<typeof Pressable>, Props>(function Button(
  {
    title,
    onPress,
    variant = 'primary',
    size = 'md',
    disabled = false,
    loading = false,
    fullWidth = false,
    className,
    ...rest
  },
  ref,
) {
  const isDisabled = disabled || loading
  const classes = [
    'items-center justify-center rounded-2xl',
    sizeContainer[size],
    variantContainer[variant],
    fullWidth ? 'self-stretch' : 'self-start',
    isDisabled ? 'opacity-40' : '',
    className ?? '',
  ]
    .join(' ')
    .trim()
  const handlePress = isDisabled
    ? undefined
    : (event: Parameters<NonNullable<PressableProps['onPress']>>[0]) => {
        haptic(variant === 'destructive' ? 'warn' : 'tap')
        onPress?.(event)
      }
  return (
    <Pressable
      ref={ref}
      accessibilityRole="button"
      accessibilityState={{ disabled: isDisabled }}
      onPress={handlePress}
      {...rest}
      className={classes}
    >
      {loading ? (
        <ActivityIndicator
          color={
            variant === 'primary' || variant === 'destructive' ? colors.onFill : colors.ink.light
          }
        />
      ) : (
        <Text className={`${sizeText[size]} ${variantText[variant]}`}>{title}</Text>
      )}
    </Pressable>
  )
})
