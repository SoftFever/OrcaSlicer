import Constants from 'expo-constants'

// Which build of the app this is, from the `extra` that app.config.ts embeds.
// CFBundleShortVersionString is numeric only, so the full version with its prerelease
// label (0.3.0-beta.2) comes from here rather than from expoConfig.version.

export type AppVariant = 'development' | 'beta' | 'production'

const config = Constants.expoConfig
const extra = (config?.extra ?? {}) as { variant?: AppVariant; marketingVersion?: string }

export const appVariant: AppVariant = extra.variant ?? 'development'
export const isBeta = appVariant === 'beta'
export const marketingVersion = extra.marketingVersion ?? config?.version ?? '0.0.0'
export const buildNumber = config?.ios?.buildNumber ?? ''
