import React, { useEffect } from 'react'
import BootSplash from 'react-native-bootsplash'

import { useCore } from '@/lib/core'

// The native bootsplash stays up until JS asks it to go, so we hold it over
// the first preset bundle load — the slowest part of startup — instead of
// flashing an empty screen while it runs. hide() is idempotent, but the
// effect only fires once anyway since `ready` never goes back to false.
export function HideBootSplash(): null {
  const { ready } = useCore()
  useEffect(() => {
    if (ready) void BootSplash.hide({ fade: true })
  }, [ready])
  return null
}
