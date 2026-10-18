import { createMMKV } from 'react-native-mmkv'

// Single shared MMKV instance. The mobile app's storage need is small:
// TanStack Query's persisted cache, printer host configs, UI preferences.
// MMKV is a JSI-backed ~1000x faster alternative to AsyncStorage; v4 is a
// Nitro module and rides on the same react-native-nitro-modules we ship for
// the slicing core.
export const storage = createMMKV({ id: 'orcaslicer' })

// Async-shaped adapter for TanStack Query's async-storage persister. MMKV
// itself is synchronous; the persister API expects Promises, so wrap.
export const mmkvAsyncStorage = {
  getItem: (key: string): Promise<string | null> => Promise.resolve(storage.getString(key) ?? null),
  setItem: (key: string, value: string): Promise<void> => {
    storage.set(key, value)
    return Promise.resolve()
  },
  removeItem: (key: string): Promise<void> => {
    storage.remove(key)
    return Promise.resolve()
  },
}
