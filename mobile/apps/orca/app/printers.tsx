import { useQuery, useQueryClient } from '@tanstack/react-query'
import { useRouter } from 'expo-router'
import React, { useCallback, useEffect, useState } from 'react'
import { Alert, FlatList, Modal, Pressable, ScrollView, Text, TextInput, View } from 'react-native'

import { scanLocalNetwork, type DiscoveredHost } from '@/lib/discovery'
import { colors } from '@/lib/theme'
import { clientFor, HOST_KINDS, loadPrinters, normalizeUrl, savePrinters, type PrinterHost, type PrinterHostKind, type PrinterStatus } from '@/lib/printers'
import { queryKeys } from '@/lib/queries'
import { Alert as InlineAlert } from '@/ui/Alert'
import { Avatar } from '@/ui/Avatar'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'
import { Chip } from '@/ui/Chip'

// Destinations: printer hosts the app can send G-code to. Stored in the
// core's data directory via lib/printers/store.

function newId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`
}

interface EditorProps {
  initial: PrinterHost | null
  onDone: (host: PrinterHost | null) => void
}

function PrinterEditor({ initial, onDone }: EditorProps): React.JSX.Element {
  const [name, setName] = useState(initial?.name ?? '')
  const [kind, setKind] = useState<PrinterHostKind>(initial?.kind ?? 'moonraker')
  const [url, setUrl] = useState(initial?.url ?? '')
  const [apiKey, setApiKey] = useState(initial?.apiKey ?? '')
  const [testing, setTesting] = useState(false)
  const [testResult, setTestResult] = useState<{ ok: boolean; text: string } | null>(null)
  const [scanning, setScanning] = useState(false)
  const [found, setFound] = useState<DiscoveredHost[]>([])
  const scanAbort = React.useRef<AbortController | null>(null)

  const runScan = async (): Promise<void> => {
    scanAbort.current?.abort()
    const controller = new AbortController()
    scanAbort.current = controller
    setScanning(true)
    setFound([])
    try {
      await scanLocalNetwork((h) => setFound((prev) => (prev.some((p) => p.url === h.url) ? prev : [...prev, h])), controller.signal)
    } finally {
      if (scanAbort.current === controller) setScanning(false)
    }
  }

  useEffect(() => () => scanAbort.current?.abort(), [])

  const host = (): PrinterHost => ({
    id: initial?.id ?? newId(),
    name: name.trim() === '' ? normalizeUrl(url) : name.trim(),
    kind,
    url: normalizeUrl(url),
    apiKey: apiKey.trim() === '' ? undefined : apiKey.trim(),
  })

  const test = async (): Promise<void> => {
    setTesting(true)
    setTestResult(null)
    try {
      const info = await clientFor(host()).info(host())
      setTestResult({ ok: true, text: info })
    } catch (error) {
      setTestResult({ ok: false, text: String(error) })
    } finally {
      setTesting(false)
    }
  }

  const hint = HOST_KINDS.find((k) => k.kind === kind)?.hint

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <View className="flex-row items-center justify-between border-b border-neutral-100 px-5 pb-3 pt-4 dark:border-neutral-800">
        <Text className="text-body font-semibold text-neutral-900 dark:text-neutral-100">
          {initial === null ? 'Add destination' : 'Edit destination'}
        </Text>
        <Pressable hitSlop={8} onPress={() => onDone(null)}>
          <Text className="text-subhead font-semibold text-neutral-500">Cancel</Text>
        </Pressable>
      </View>
      <ScrollView contentContainerClassName="px-4 pt-4 pb-8 gap-4">
        <Card title="Name" padded>
          <TextInput
            className="text-subhead text-neutral-900 dark:text-neutral-100"
            placeholder="Voron"
            placeholderTextColor={colors.placeholder}
            value={name}
            onChangeText={setName}
            autoCorrect={false}
          />
        </Card>

        <Card title="Host" padded>
          <View className="flex-row gap-1.5">
            {HOST_KINDS.map((k) => (
              <Chip key={k.kind} label={k.label} selected={kind === k.kind} onPress={() => setKind(k.kind)} />
            ))}
          </View>
        </Card>

        {initial === null ? (
          <Card
            title="Discover on network"
            subtitle={scanning ? `Scanning… ${found.length > 0 ? `${found.length} found` : ''}` : 'Sweep the local /24 for Moonraker or OctoPrint'}
            padded
            action={<Button title={scanning ? 'Stop' : 'Scan'} variant="secondary" onPress={scanning ? () => scanAbort.current?.abort() : runScan} />}
          >
            {found.length === 0 && !scanning ? (
              <Text className="text-footnote text-neutral-500 dark:text-neutral-400">
                Tap Scan to look for a printer on your Wi-Fi. Manual entry below still works.
              </Text>
            ) : (
              found.map((h) => (
                <Pressable
                  key={h.url}
                  onPress={() => {
                    setKind(h.kind)
                    setUrl(h.url)
                    if (name.trim() === '') setName(h.ip)
                  }}
                  className="flex-row items-center gap-3 rounded-xl bg-neutral-100 px-3 py-2 active:bg-neutral-200 dark:bg-neutral-800 dark:active:bg-neutral-700"
                >
                  <View className="flex-1">
                    <Text className="text-callout font-medium text-neutral-900 dark:text-neutral-100">{h.ip}</Text>
                    <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400">
                      {h.kind === 'moonraker' ? 'Moonraker' : 'OctoPrint'}
                      {h.info !== '' ? ` · ${h.info}` : ''}
                    </Text>
                  </View>
                  <Text className="text-label font-semibold uppercase tracking-wider text-neutral-500 dark:text-neutral-400">Use</Text>
                </Pressable>
              ))
            )}
          </Card>
        ) : null}

        <Card title="Address" padded>
          <TextInput
            className="text-subhead text-neutral-900 dark:text-neutral-100"
            placeholder={hint}
            placeholderTextColor={colors.placeholder}
            value={url}
            onChangeText={setUrl}
            autoCorrect={false}
            autoCapitalize="none"
            keyboardType="url"
          />
        </Card>

        <Card title={kind === 'octoprint' ? 'Application key' : 'API key (optional)'} padded>
          <TextInput
            className="text-subhead text-neutral-900 dark:text-neutral-100"
            placeholder={kind === 'octoprint' ? 'required' : 'leave empty if none'}
            placeholderTextColor={colors.placeholder}
            value={apiKey}
            onChangeText={setApiKey}
            autoCorrect={false}
            autoCapitalize="none"
            secureTextEntry
          />
        </Card>

        {testResult !== null ? (
          <InlineAlert tone={testResult.ok ? 'success' : 'critical'} title={testResult.ok ? 'Connected' : 'Connection failed'}>
            {testResult.text}
          </InlineAlert>
        ) : null}

        <View className="flex-row gap-2">
          <View className="flex-1">
            <Button
              title={testing ? 'Testing…' : 'Test'}
              variant="secondary"
              onPress={test}
              disabled={testing || url.trim() === ''}
              fullWidth
            />
          </View>
          <View className="flex-1">
            <Button
              title="Save"
              onPress={() => onDone(host())}
              disabled={url.trim() === '' || (kind === 'octoprint' && apiKey.trim() === '')}
              fullWidth
            />
          </View>
        </View>
      </ScrollView>
    </View>
  )
}

function StatusDot({ ok }: { ok: boolean | null }): React.JSX.Element {
  const color = ok === null ? 'bg-neutral-300 dark:bg-neutral-600' : ok ? 'bg-emerald-500' : 'bg-red-500'
  return <View className={`h-2 w-2 rounded-full ${color}`} />
}

function describe(status: PrinterStatus): string {
  const parts: string[] = [status.state]
  if (status.progress !== undefined) parts.push(`${Math.round(status.progress * 100)}%`)
  if (status.nozzleTemperature !== undefined) parts.push(`nozzle ${Math.round(status.nozzleTemperature)}°`)
  if (status.bedTemperature !== undefined) parts.push(`bed ${Math.round(status.bedTemperature)}°`)
  return parts.join(' · ')
}

function HostRow({
  host,
  onEdit,
  onOpen,
  onDelete,
}: {
  host: PrinterHost
  onEdit: () => void
  onOpen: () => void
  onDelete: () => void
}): React.JSX.Element {
  const query = useQuery({
    queryKey: queryKeys.printerStatus(host.id),
    queryFn: () => clientFor(host).status(host),
    refetchInterval: 5000,
    refetchIntervalInBackground: false,
    staleTime: 4000,
    retry: false,
  })

  return (
    <Pressable
      onPress={onOpen}
      className="flex-row items-center gap-3 bg-white px-4 py-3 active:bg-neutral-100 dark:bg-neutral-900 dark:active:bg-neutral-800"
    >
      <Avatar text={host.name} size={40} />
      <View className="flex-1">
        <View className="flex-row items-center gap-2">
          <Text className="text-subhead font-semibold tracking-tight text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
            {host.name}
          </Text>
          <StatusDot ok={query.isError ? false : query.isSuccess ? true : null} />
        </View>
        <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
          {host.url}
        </Text>
        <Text className="mt-0.5 text-label text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
          {query.isError
            ? 'Offline'
            : query.data !== undefined
              ? describe(query.data)
              : 'Checking…'}
        </Text>
      </View>
      <Pressable onPress={onEdit} hitSlop={8} className="rounded-full bg-neutral-100 px-3 py-1.5 active:opacity-60 dark:bg-neutral-800">
        <Text className="text-label font-semibold uppercase tracking-wider text-neutral-600 dark:text-neutral-300">Edit</Text>
      </Pressable>
      <Pressable onPress={onDelete} hitSlop={8} className="ml-1 rounded-full px-2 py-1.5 active:opacity-60">
        <Text className="text-callout font-semibold text-red-500">✕</Text>
      </Pressable>
    </Pressable>
  )
}

export default function PrintersScreen(): React.JSX.Element {
  const router = useRouter()
  const client = useQueryClient()
  const [printers, setPrinters] = useState<PrinterHost[]>([])
  const [editing, setEditing] = useState<{ host: PrinterHost | null } | null>(null)

  useEffect(() => {
    setPrinters(loadPrinters())
  }, [])

  const persist = useCallback((next: PrinterHost[]) => {
    setPrinters(next)
    savePrinters(next)
  }, [])

  const remove = (item: PrinterHost): void => {
    Alert.alert('Remove destination', item.name, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Remove',
        style: 'destructive',
        onPress: () => {
          persist(printers.filter((p) => p.id !== item.id))
          void client.removeQueries({ queryKey: queryKeys.printerStatus(item.id) })
        },
      },
    ])
  }

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <FlatList
        data={printers}
        keyExtractor={(p) => p.id}
        contentContainerClassName="px-4 pt-3 pb-8"
        ItemSeparatorComponent={() => <View className="ml-16 h-px bg-neutral-100 dark:bg-neutral-800" />}
        renderItem={({ item, index }) => (
          <View className={`overflow-hidden ${index === 0 ? 'rounded-t-2xl' : ''} ${index === printers.length - 1 ? 'rounded-b-2xl' : ''}`}>
            <HostRow
              host={item}
              onEdit={() => setEditing({ host: item })}
              onOpen={() => router.push(`/host/${item.id}`)}
              onDelete={() => remove(item)}
            />
          </View>
        )}
        ListEmptyComponent={
          <View className="mt-6">
            <Card padded>
              <Text className="text-subhead text-neutral-500 dark:text-neutral-400">
                No destinations yet. Add a Klipper (Moonraker) or OctoPrint host to send G-code and monitor prints from the app.
              </Text>
            </Card>
          </View>
        }
        ListFooterComponent={
          <View className="mt-4 items-center">
            <Button title="Add destination" onPress={() => setEditing({ host: null })} />
          </View>
        }
      />
      <Modal
        visible={editing !== null}
        animationType="slide"
        presentationStyle="pageSheet"
        onRequestClose={() => setEditing(null)}
      >
        {editing !== null ? (
          <PrinterEditor
            initial={editing.host}
            onDone={(host) => {
              if (host !== null) {
                const others = printers.filter((p) => p.id !== host.id)
                persist([...others, host])
              }
              setEditing(null)
            }}
          />
        ) : null}
      </Modal>
    </View>
  )
}
