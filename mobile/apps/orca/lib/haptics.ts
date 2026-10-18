import * as Haptics from 'expo-haptics'
import { Platform } from 'react-native'

// Thin wrapper around expo-haptics with a small vocabulary that maps to
// human-scale intents ("tap", "toggle", "success", "warn") instead of the
// raw impact / notification enums. Every call is silent-safe: no throw if
// the device has no Taptic Engine, no-op on Android when a matching
// pattern doesn't exist.
//
// Guidelines used across the app:
//  - `tap`     — a primary button press or a nav segment change.
//  - `toggle`  — a switch / chip flip, a collapse toggle, a chip picker.
//  - `select`  — picking an item in a list, a preset choice.
//  - `success` — a slice finishes, an upload completes, a preset saves.
//  - `warn`    — a print pauses, a non-critical alert opens.
//  - `error`   — a destructive confirm, a failed request.

type Intent = 'tap' | 'toggle' | 'select' | 'success' | 'warn' | 'error'

export function haptic(intent: Intent): void {
  if (Platform.OS === 'web') return
  try {
    switch (intent) {
      case 'tap':
        void Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Light)
        return
      case 'toggle':
        void Haptics.selectionAsync()
        return
      case 'select':
        void Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Soft)
        return
      case 'success':
        void Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success)
        return
      case 'warn':
        void Haptics.notificationAsync(Haptics.NotificationFeedbackType.Warning)
        return
      case 'error':
        void Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error)
        return
    }
  } catch {
    /* silent: no Taptic Engine or the pattern is unavailable */
  }
}
