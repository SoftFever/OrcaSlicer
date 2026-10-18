import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Stack, useLocalSearchParams, useRouter } from 'expo-router'
import React, { useEffect, useState } from 'react'
import { Alert, Pressable, ScrollView, Text, View } from 'react-native'

import { clientFor, loadPrinters, type HeatedTool, type PrinterHost, type PrinterStatus } from '@/lib/printers'
import { queryClient, queryKeys } from '@/lib/queries'
import { Alert as InlineAlert } from '@/ui/Alert'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'
import { PromptSheet } from '@/ui/PromptSheet'
import { WebcamView } from '@/ui/WebcamView'

function formatEta(progress: number | undefined): string | null {
  // OctoPrint / Moonraker do not always expose remaining time cleanly, so we
  // leave ETA off unless it is derivable. Placeholder for a richer version.
  if (progress === undefined) return null
  const pct = Math.round(progress * 100)
  return `${pct}%`
}

function TempTile({
  label,
  temp,
  target,
  onPress,
}: {
  label: string
  temp?: number
  target?: number
  onPress?: () => void
}): React.JSX.Element {
  return (
    <Pressable
      onPress={onPress}
      className="flex-1 rounded-2xl bg-neutral-100 px-4 py-3 active:opacity-70 dark:bg-neutral-800"
    >
      <Text className="text-label uppercase tracking-wider text-neutral-500 dark:text-neutral-400">{label}</Text>
      <Text className="mt-1 text-title2 font-semibold tracking-tight text-neutral-900 dark:text-neutral-100">
        {temp !== undefined ? `${Math.round(temp)}°` : '—'}
      </Text>
      {target !== undefined && target > 0 ? (
        <Text className="text-label text-neutral-500 dark:text-neutral-400">Target {Math.round(target)}°</Text>
      ) : (
        <Text className="text-label text-neutral-500 dark:text-neutral-400">Tap to set</Text>
      )}
    </Pressable>
  )
}

function StatePill({ state }: { state: string }): React.JSX.Element {
  const s = state.toLowerCase()
  const [bg, text] =
    s.includes('print') || s.includes('busy') || s.includes('running')
      ? ['bg-emerald-100 dark:bg-emerald-500/20', 'text-emerald-700 dark:text-emerald-200']
      : s.includes('pause')
        ? ['bg-amber-100 dark:bg-amber-500/20', 'text-amber-700 dark:text-amber-200']
        : s.includes('error') || s.includes('offline')
          ? ['bg-red-100 dark:bg-red-500/20', 'text-red-700 dark:text-red-200']
          : ['bg-neutral-100 dark:bg-neutral-800', 'text-neutral-700 dark:text-neutral-300']
  return (
    <View className={`self-start rounded-full px-3 py-1 ${bg}`}>
      <Text className={`text-label font-semibold uppercase tracking-wider ${text}`}>{state}</Text>
    </View>
  )
}

export default function HostDetailScreen(): React.JSX.Element {
  const router = useRouter()
  const { id } = useLocalSearchParams<{ id: string }>()
  const client = useQueryClient()
  const [host, setHost] = useState<PrinterHost | null>(null)

  useEffect(() => {
    const found = loadPrinters().find((h) => h.id === id) ?? null
    setHost(found)
  }, [id])

  const status = useQuery<PrinterStatus>({
    queryKey: queryKeys.printerStatus(id ?? ''),
    queryFn: () => (host !== null ? clientFor(host).status(host) : Promise.reject(new Error('no host'))),
    enabled: host !== null,
    refetchInterval: 4000,
    refetchIntervalInBackground: false,
    staleTime: 3500,
    retry: false,
  })

  const controlMutation = (action: 'pause' | 'resume' | 'cancel') => ({
    mutationFn: async (): Promise<void> => {
      if (host === null) throw new Error('no host')
      await clientFor(host)[action](host)
    },
    onSuccess: (): Promise<void> => client.invalidateQueries({ queryKey: queryKeys.printerStatus(id ?? '') }),
    onError: (e: unknown): void => Alert.alert('Control failed', String(e)),
  })
  const pause = useMutation(controlMutation('pause'))
  const resume = useMutation(controlMutation('resume'))
  const cancel = useMutation(controlMutation('cancel'))
  const busy = pause.isPending || resume.isPending || cancel.isPending

  const askCancel = (): void => {
    Alert.alert('Cancel print?', 'This stops the current job on the printer.', [
      { text: 'Keep printing', style: 'cancel' },
      { text: 'Cancel print', style: 'destructive', onPress: () => cancel.mutate() },
    ])
  }

  if (host === null) {
    return (
      <View className="flex-1 items-center justify-center bg-neutral-50 dark:bg-neutral-950">
        <Stack.Screen options={{ title: 'Destination' }} />
        <Text className="text-subhead text-neutral-500">Not found.</Text>
        <View className="mt-3">
          <Button title="Back" variant="secondary" onPress={() => router.back()} />
        </View>
      </View>
    )
  }

  const s = status.data
  const isPaused = s?.state.toLowerCase().includes('pause') === true
  const isPrinting = s !== undefined && (s.state.toLowerCase().includes('print') || s.state.toLowerCase().includes('busy'))

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <Stack.Screen options={{ title: host.name }} />
      <ScrollView contentContainerClassName="px-4 pt-3 pb-8 gap-4">
        {status.isError ? (
          <InlineAlert tone="critical" title="Offline">
            {String(status.error)}
          </InlineAlert>
        ) : null}

        <Card title="Status" padded index={0}>
          <StatePill state={s?.state ?? 'checking'} />
          {s?.file !== undefined && s.file !== '' ? (
            <Text className="text-callout text-neutral-900 dark:text-neutral-100" numberOfLines={2}>
              {s.file}
            </Text>
          ) : null}
          {formatEta(s?.progress) !== null ? (
            <View className="mt-1 h-2 overflow-hidden rounded-full bg-neutral-200 dark:bg-neutral-800">
              <View
                className="h-full rounded-full bg-neutral-900 dark:bg-white"
                style={{ width: `${Math.round((s?.progress ?? 0) * 100)}%` }}
              />
            </View>
          ) : null}
        </Card>

        <Card title="Temperatures" padded index={1}>
          <View className="flex-row gap-3">
            <TempTile label="Nozzle" temp={s?.nozzleTemperature} target={s?.nozzleTarget} />
            <TempTile label="Bed" temp={s?.bedTemperature} target={s?.bedTarget} />
          </View>
        </Card>

        <Card title="Controls" padded index={2}>
          <View className="flex-row gap-2">
            {isPaused ? (
              <View className="flex-1">
                <Button
                  title="Resume"
                  onPress={() => resume.mutate()}
                  disabled={busy || !isPrinting}
                  loading={resume.isPending}
                  fullWidth
                />
              </View>
            ) : (
              <View className="flex-1">
                <Button
                  title="Pause"
                  variant="secondary"
                  onPress={() => pause.mutate()}
                  disabled={busy || !isPrinting}
                  loading={pause.isPending}
                  fullWidth
                />
              </View>
            )}
            <View className="flex-1">
              <Button
                title="Cancel"
                variant="destructive"
                onPress={askCancel}
                disabled={busy || !isPrinting}
                loading={cancel.isPending}
                fullWidth
              />
            </View>
          </View>
        </Card>

        <Card title="Connection" padded index={3}>
          <Text className="text-footnote text-neutral-500 dark:text-neutral-400">Address</Text>
          <Text className="text-subhead text-neutral-900 dark:text-neutral-100" numberOfLines={2}>
            {host.url}
          </Text>
          <Text className="mt-2 text-footnote text-neutral-500 dark:text-neutral-400">Kind</Text>
          <Text className="text-subhead text-neutral-900 dark:text-neutral-100">
            {host.kind === 'moonraker' ? 'Klipper (Moonraker)' : 'OctoPrint'}
          </Text>
        </Card>
      </ScrollView>
    </View>
  )
}
