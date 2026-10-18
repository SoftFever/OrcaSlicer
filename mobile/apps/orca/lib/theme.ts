import { useColorScheme } from 'nativewind'

// Typed view of ui/theme/colors.js for the handful of native props that take a
// colour value instead of a class: navigation header options, ActivityIndicator,
// placeholderTextColor. Everything else uses Tailwind classes.

interface Scheme {
  light: string
  dark: string
}

export interface ThemeColors {
  accent: Record<50 | 100 | 200 | 300 | 400 | 500 | 600 | 700 | 800 | 900, string>
  placeholder: string
  header: Scheme
  canvas: Scheme
  ink: Scheme
  iconMuted: Scheme
  onFill: string
  capsTracking: string
}

// eslint-disable-next-line @typescript-eslint/no-require-imports
export const colors = require('../ui/theme/colors') as ThemeColors

/** The colours resolved for the current scheme. */
export function useThemeColors(): {
  dark: boolean
  header: string
  canvas: string
  ink: string
  tint: string
  placeholder: string
} {
  const { colorScheme } = useColorScheme()
  const dark = colorScheme === 'dark'
  return {
    dark,
    header: dark ? colors.header.dark : colors.header.light,
    canvas: dark ? colors.canvas.dark : colors.canvas.light,
    ink: dark ? colors.ink.dark : colors.ink.light,
    tint: dark ? colors.accent[400] : colors.accent[600],
    placeholder: colors.placeholder,
  }
}
