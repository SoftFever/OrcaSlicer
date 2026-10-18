import { Stack, useLocalSearchParams, useRouter } from 'expo-router'
import React, { useEffect, useMemo } from 'react'
import { ActivityIndicator, Pressable, Text, View } from 'react-native'
import { WebView } from 'react-native-webview'

import { loadPrinters, normalizeUrl, type PrinterHost } from '@/lib/printers'

// Embedded browser for the printer's own web UI — Mainsail for Moonraker hosts,
// OctoPrint's default UI otherwise. Reached from the Device panel's ↗ button;
// keeps the user inside OrcaSlicer instead of context-switching to Safari, so
// they can jog / heat / start a print without leaving the app.
//
// The route accepts the printer id via /host/web/[id] and looks up its base URL.
// We never rewrite the URL or intercept navigations — the printer's UI runs as
// it does in Safari, cookies included.

export default function PrinterWebScreen(): React.JSX.Element {
  const router = useRouter()
  const { id } = useLocalSearchParams<{ id: string }>()
  const host: PrinterHost | null = useMemo(
    () => (id !== undefined ? loadPrinters().find((h) => h.id === id) ?? null : null),
    [id],
  )

  useEffect(() => {
    if (host === null) router.back()
  }, [host, router])

  if (host === null) {
    return (
      <View className="flex-1 items-center justify-center bg-neutral-50 dark:bg-neutral-950">
        <Stack.Screen options={{ title: 'Printer' }} />
        <Text className="text-subhead text-neutral-500">Printer not found.</Text>
      </View>
    )
  }

  return (
    <View className="flex-1 bg-white dark:bg-neutral-950">
      <Stack.Screen
        options={{
          title: host.name,
          headerBackTitle: 'Back',
          headerRight: () => (
            <Pressable onPress={() => router.back()} hitSlop={8}>
              <Text className="text-subhead font-semibold text-blue-500">Done</Text>
            </Pressable>
          ),
        }}
      />
      <WebView
        source={{ uri: normalizeUrl(host.url) }}
        style={{ flex: 1 }}
        // Match iOS Safari behavior on cache/media/PTR-to-refresh so the printer
        // UI feels the same as in the browser people already know.
        allowsBackForwardNavigationGestures
        pullToRefreshEnabled
        // Camera streams (mjpg) auto-play without a user tap.
        allowsInlineMediaPlayback
        mediaPlaybackRequiresUserAction={false}
        startInLoadingState
        renderLoading={() => (
          <View className="flex-1 items-center justify-center bg-white dark:bg-neutral-950">
            <ActivityIndicator />
          </View>
        )}
      />
    </View>
  )
}
