import { storage } from './storage'

// One-time flag. Bump the version suffix when the onboarding content changes
// enough that shown-once users should see the new steps.
const KEY = 'onboarded:v1'

export function hasOnboarded(): boolean {
  return storage.getBoolean(KEY) === true
}

export function markOnboarded(): void {
  storage.set(KEY, true)
}

export function resetOnboarding(): void {
  storage.remove(KEY)
}
