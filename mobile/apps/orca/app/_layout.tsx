import { Stack } from 'expo-router'
import { StatusBar } from 'expo-status-bar'
import { createAsyncStoragePersister } from '@tanstack/query-async-storage-persister'
import { PersistQueryClientProvider } from '@tanstack/react-query-persist-client'
import React, { useEffect } from 'react'

import '../global.css'

import { ensureBackgroundTaskRegistered } from '@/lib/backgroundTasks'
import { startPrinterActivityBridge } from '@/lib/printerActivity'
import { CoreProvider } from '@/lib/core'
import { queryClient } from '@/lib/queries'
import { mmkvAsyncStorage } from '@/lib/storage'
import { DownloadsPill } from '@/ui/DownloadsPill'
import { SettingsLink } from '@/ui/SettingsLink'
import { useThemeColors } from '@/lib/theme'
import { HideBootSplash } from '@/ui/HideBootSplash'
import { ToastHost } from '@/ui/ToastHost'

// The Stack is styled once at the root so every screen inherits the
// grouped-background look and a large iOS-style title on the home screen.
// PersistQueryClientProvider owns the shared cache for GitHub profile
// lookups and per-printer status polls, backed by an MMKV-persisted store
// so vendor listings survive an app restart with no network hit.
const persister = createAsyncStoragePersister({ storage: mmkvAsyncStorage, key: 'orca-query-cache' })

export default function RootLayout(): React.JSX.Element {
  const { dark, header, canvas, ink, tint } = useThemeColors()
  useEffect(() => {
    void ensureBackgroundTaskRegistered()
  }, [])
  // Mirrors printer status into the home screen widget and the print-progress
  // Live Activity; reads the status queries the screens already run.
  useEffect(() => startPrinterActivityBridge(), [])
  return (
    <PersistQueryClientProvider
      client={queryClient}
      persistOptions={{
        persister,
        maxAge: 24 * 60 * 60 * 1000,
        // Buster changes whenever the shape of a cached response changes so
        // stale data does not decode into new types.
        buster: 'v1',
      }}
    >
      <CoreProvider>
        <HideBootSplash />
        <StatusBar style={dark ? 'light' : 'dark'} />
        <Stack
          screenOptions={{
            headerStyle: { backgroundColor: header },
            headerTintColor: tint,
            headerTitleStyle: { color: ink },
            contentStyle: { backgroundColor: canvas },
          }}
        >
          <Stack.Screen
            name="index"
            options={{
              title: 'Orca Mobile',
              headerLargeTitle: true,
              headerLeft: () => <SettingsLink />,
              headerRight: () => <DownloadsPill />,
            }}
          />
          <Stack.Screen name="settings/index" options={{ title: 'Settings' }} />
          <Stack.Screen name="settings/export" options={{ title: 'Export presets' }} />
          <Stack.Screen name="settings/[kind]" options={{ title: 'Edit preset' }} />
          <Stack.Screen name="host/[id]" options={{ title: 'Destination' }} />
          <Stack.Screen name="host/web/[id]" options={{ title: 'Printer web UI' }} />
          <Stack.Screen name="calibration" options={{ title: 'Calibration' }} />
          <Stack.Screen name="storage" options={{ title: 'Storage' }} />
          <Stack.Screen
            name="fullscreen"
            options={{ presentation: 'fullScreenModal', headerShown: false, animation: 'fade' }}
          />
          <Stack.Screen name="vendors" options={{ title: 'Choose printer' }} />
          <Stack.Screen name="printers" options={{ title: 'Printers' }} />
          <Stack.Screen name="about" options={{ title: 'About' }} />
          <Stack.Screen name="help/index" options={{ title: 'Help' }} />
          <Stack.Screen name="account/index" options={{ title: 'Orca Cloud' }} />
          <Stack.Screen name="account/login" options={{ title: 'Sign in', presentation: 'modal' }} />
          <Stack.Screen name="help/[topic]" options={{ title: 'Guide' }} />
        </Stack>
        <ToastHost />
      </CoreProvider>
    </PersistQueryClientProvider>
  )
}
