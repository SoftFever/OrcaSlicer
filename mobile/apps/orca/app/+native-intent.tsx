import { MODEL_FILE_PATTERN } from '@/lib/openFile'
import { PRESET_FILE_PATTERN } from '@/lib/presetFiles'

// Files handed to the app (Files.app, AirDrop, a share sheet) reach Expo Router as
// a path under the app's URL scheme, which would otherwise render "Unmatched
// Route". The home screen's Linking listener does the actual import, so the
// router just stays on the home screen for those.
export function redirectSystemPath({ path }: { path: string; initial: boolean }): string {
  return MODEL_FILE_PATTERN.test(path) || PRESET_FILE_PATTERN.test(path) ? '/' : path
}
