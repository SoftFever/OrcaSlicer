import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { useRouter } from 'expo-router'
import React, { useState } from 'react'
import { Alert, Pressable, ScrollView, Text, View } from 'react-native'

import { clientFor, type HeatedTool, type PrinterHost, type PrinterStatus } from '@/lib/printers'
import { haptic } from '@/lib/haptics'
import { queryKeys } from '@/lib/queries'
import { Alert as InlineAlert } from '@/ui/Alert'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'
import { Chip } from '@/ui/Chip'
import { PromptSheet } from '@/ui/PromptSheet'
import { TemperaturePanel } from '@/ui/TemperaturePanel'
import { WebcamView } from '@/ui/WebcamView'

// Live-control panel for a single printer host. Used inline in the Device
// pane on the home screen (multiple hosts pick via a chip strip at top)
// and, indirectly, by the host/[id] stack route for direct-link cases.
//
// Polling stops when the panel unmounts because react-query's
// refetchInterval is bound to the query being observed here.

type Props = {
  hosts: PrinterHost[]
  activeId: string | null
  onFocus: (id: string) => void
}

export function DevicePanel({ hosts, activeId, onFocus }: Props): React.JSX.Element {
  if (hosts.length === 0) {
    return (
      <View className="items-center gap-2 rounded-3xl bg-white p-6 shadow-soft dark:bg-neutral-900">
        <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100">
          No printer configured
        </Text>
        <Text className="text-center text-footnote text-neutral-500 dark:text-neutral-400">
          Add a Moonraker or OctoPrint host from Manage destinations to control it live.
        </Text>
      </View>
    )
  }
  const host = hosts.find((h) => h.id === activeId) ?? hosts[0]!
  return (
    <View className="gap-4">
      {hosts.length > 1 ? (
        <ScrollView
          horizontal
          showsHorizontalScrollIndicator={false}
          contentContainerClassName="gap-2"
        >
          {hosts.map((h) => (
            <Chip
              key={h.id}
              label={h.name}
              selected={h.id === host.id}
              onPress={() => onFocus(h.id)}
            />
          ))}
        </ScrollView>
      ) : null}
      <DeviceLive host={host} />
    </View>
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
      onPress={() => {
        haptic('tap')
        onPress?.()
      }}
      className="flex-1 rounded-2xl bg-neutral-100 px-4 py-3 active:opacity-70 dark:bg-neutral-800"
    >
      <Text className="text-label uppercase tracking-wider text-neutral-500 dark:text-neutral-400">
        {label}
      </Text>
      <Text className="mt-1 text-title2 font-semibold tracking-tight text-neutral-900 dark:text-neutral-100">
        {temp !== undefined ? `${Math.round(temp)}°` : '—'}
      </Text>
      {target !== undefined && target > 0 ? (
        <Text className="text-label text-neutral-500 dark:text-neutral-400">
          Target {Math.round(target)}°
        </Text>
      ) : (
        <Text className="text-label text-neutral-500 dark:text-neutral-400">Tap to set</Text>
      )}
    </Pressable>
  )
}

function DeviceLive({ host }: { host: PrinterHost }): React.JSX.Element {
  const client = useQueryClient()
  const router = useRouter()
  const [settingTool, setSettingTool] = useState<HeatedTool | null>(null)

  const status = useQuery<PrinterStatus>({
    queryKey: queryKeys.printerStatus(host.id),
    queryFn: () => clientFor(host).status(host),
    refetchInterval: 4000,
    refetchIntervalInBackground: false,
    staleTime: 3500,
    retry: false,
  })

  const webcam = useQuery<string | null>({
    queryKey: ['printerWebcam', host.id],
    queryFn: () => clientFor(host).webcamUrl(host),
    staleTime: 60_000,
    retry: false,
  })

  const controlMutation = (action: 'pause' | 'resume' | 'cancel') => ({
    mutationFn: async (): Promise<void> => {
      await clientFor(host)[action](host)
    },
    onSuccess: (): Promise<void> =>
      client.invalidateQueries({ queryKey: queryKeys.printerStatus(host.id) }),
    onError: (e: unknown): void => Alert.alert('Control failed', String(e)),
  })
  const pause = useMutation(controlMutation('pause'))
  const resume = useMutation(controlMutation('resume'))
  const cancel = useMutation(controlMutation('cancel'))
  const busy = pause.isPending || resume.isPending || cancel.isPending

  const setTemp = useMutation({
    mutationFn: async (value: string): Promise<void> => {
      const target = Number.parseInt(value, 10)
      if (Number.isNaN(target) || target < 0 || target > 500)
        throw new Error('Enter a number between 0 and 500.')
      if (settingTool === null) return
      await clientFor(host).setTemperature(host, settingTool, target)
    },
    onSuccess: async () => {
      setSettingTool(null)
      haptic('success')
      await client.invalidateQueries({ queryKey: queryKeys.printerStatus(host.id) })
    },
    onError: (e: unknown) => Alert.alert('Set temperature failed', String(e)),
  })

  const s = status.data
  const isPaused = s?.state.toLowerCase().includes('pause') === true
  const isPrinting =
    s !== undefined &&
    (s.state.toLowerCase().includes('print') || s.state.toLowerCase().includes('busy'))

  const askCancel = (): void => {
    Alert.alert('Cancel print?', 'This stops the current job on the printer.', [
      { text: 'Keep printing', style: 'cancel' },
      { text: 'Cancel print', style: 'destructive', onPress: () => cancel.mutate() },
    ])
  }

  return (
    <View className="gap-4">
      {status.isError ? (
        <InlineAlert tone="critical" title="Offline">
          {String(status.error)}
        </InlineAlert>
      ) : null}

      {webcam.data !== null && webcam.data !== undefined ? (
        <Card title="Camera" index={0}>
          <WebcamView streamUrl={webcam.data} />
        </Card>
      ) : null}

      <Card title="Status" padded index={1}>
        <StatePill state={s?.state ?? 'checking'} />
        {s?.file !== undefined && s.file !== '' ? (
          <Text className="text-callout text-neutral-900 dark:text-neutral-100" numberOfLines={2}>
            {s.file}
          </Text>
        ) : null}
        {s?.progress !== undefined ? (
          <View className="mt-1 h-2 overflow-hidden rounded-full bg-neutral-200 dark:bg-neutral-800">
            <View
              className="h-full rounded-full bg-neutral-900 dark:bg-white"
              style={{ width: `${Math.round(s.progress * 100)}%` }}
            />
          </View>
        ) : null}
      </Card>

      <Card
        title="Temperatures"
        padded
        index={2}
        collapsible
        collapseKey={`temps-${host.id}`}
        action={
          <Pressable
            onPress={() => {
              haptic('tap')
              router.push(`/host/web/${host.id}` as never)
            }}
            hitSlop={8}
            className="rounded-full bg-neutral-100 px-2.5 py-1 active:opacity-70 dark:bg-neutral-800"
          >
            <Text className="text-caption font-semibold uppercase tracking-wider text-neutral-700 dark:text-neutral-300">
              {host.kind === 'moonraker' ? '↗ Mainsail' : '↗ OctoPrint'}
            </Text>
          </Pressable>
        }
      >
        {s?.sensors !== undefined && s.sensors.length > 0 ? (
          <TemperaturePanel
            hostId={host.id}
            sensors={s.sensors}
            onSetTarget={(tool) => setSettingTool(tool)}
          />
        ) : (
          <View className="flex-row gap-3">
            <TempTile
              label="Nozzle"
              temp={s?.nozzleTemperature}
              target={s?.nozzleTarget}
              onPress={() => setSettingTool('nozzle')}
            />
            <TempTile
              label="Bed"
              temp={s?.bedTemperature}
              target={s?.bedTarget}
              onPress={() => setSettingTool('bed')}
            />
          </View>
        )}
      </Card>

      <Card title="Controls" padded index={3}>
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

      <PromptSheet
        visible={settingTool !== null}
        title={settingTool === 'nozzle' ? 'Set nozzle target' : 'Set bed target'}
        message={
          settingTool === 'nozzle'
            ? 'In °C. Set 0 to turn the heater off.'
            : 'In °C. Set 0 to turn the bed heater off.'
        }
        placeholder="0"
        defaultValue={String(
          Math.round((settingTool === 'nozzle' ? s?.nozzleTarget : s?.bedTarget) ?? 0),
        )}
        confirmLabel={setTemp.isPending ? 'Setting…' : 'Set'}
        onCancel={() => setSettingTool(null)}
        onConfirm={(value) => setTemp.mutate(value)}
      />
    </View>
  )
}
