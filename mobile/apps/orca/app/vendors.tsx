import { useQuery } from '@tanstack/react-query'
import { useRouter } from 'expo-router'
import React, { useMemo, useState } from 'react'
import { ActivityIndicator, Image, Pressable, SectionList, Text, TextInput, View } from 'react-native'

import { Avatar } from '@/ui/Avatar'
import { colors } from '@/lib/theme'
import { Button } from '@/ui/Button'
import { useCore } from '@/lib/core'
import { useDownloadsStore } from '@/lib/downloads'
import { pickAndImportPresets } from '@/lib/presetFiles'
import {
  availablePrinters,
  DEFAULT_PROFILE_SOURCE,
  installedVendors,
  type AvailablePrinter,
} from '@/lib/profiles'
import { queryKeys } from '@/lib/queries'

// Printer-first browser. Installations run through the shared downloads store,
// so leaving this screen does not stop the fetch — the in-flight download shows
// up in the header pill and completes on its own.
export default function VendorsScreen(): React.JSX.Element {
  const router = useRouter()
  const { reloadPresets, session } = useCore()
  const [query, setQuery] = useState('')

  const printers = useQuery({
    queryKey: queryKeys.availablePrinters,
    queryFn: () => availablePrinters(),
  })
  const installed = useQuery({
    queryKey: queryKeys.installedVendors,
    queryFn: () => installedVendors(),
    staleTime: Infinity,
  })

  const byId = useDownloadsStore((s) => s.byId)
  const install = useDownloadsStore((s) => s.install)
  const start = (p: AvailablePrinter): void => {
    install(p, {
      session,
      reloadPresets,
      onCompleted: () => {
        // Pop back to home once the install finishes so the user sees the
        // freshly selected printer without needing to press Back.
        if (router.canGoBack()) router.back()
      },
    })
  }

  const needle = query.trim().toLowerCase()
  const sections = useMemo(() => {
    const list = (printers.data ?? []).filter(
      (p) =>
        needle === '' ||
        p.model.toLowerCase().includes(needle) ||
        p.vendorName.toLowerCase().includes(needle) ||
        p.vendor.toLowerCase().includes(needle),
    )
    const byVendor = new Map<string, AvailablePrinter[]>()
    for (const p of list) {
      const items = byVendor.get(p.vendorName) ?? []
      items.push(p)
      byVendor.set(p.vendorName, items)
    }
    return Array.from(byVendor.entries())
      .sort((a, b) => a[0].localeCompare(b[0]))
      .map(([title, data]) => ({ title, data: data.sort((a, b) => a.model.localeCompare(b.model)) }))
  }, [printers.data, needle])

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <View className="border-b border-neutral-100 px-5 pb-3 pt-3 dark:border-neutral-800">
        <View className="flex-row items-center gap-2">
          <View className="flex-1 flex-row items-center rounded-2xl bg-neutral-100 px-3 py-2 dark:bg-neutral-900">
            <Text className="mr-2 text-subhead text-neutral-400">Search</Text>
            <TextInput
              className="flex-1 text-subhead text-neutral-900 dark:text-neutral-100"
              placeholder="Printer or manufacturer…"
              placeholderTextColor={colors.placeholder}
              value={query}
              onChangeText={setQuery}
              autoCorrect={false}
              autoCapitalize="none"
              clearButtonMode="while-editing"
            />
          </View>
          <Pressable
            onPress={() => {
              if (session === null) return
              void pickAndImportPresets({ session, reloadPresets })
            }}
            className="rounded-full bg-neutral-100 px-3 py-2 active:bg-neutral-200 dark:bg-neutral-800 dark:active:bg-neutral-700"
          >
            <Text className="text-label font-semibold uppercase tracking-wider text-neutral-700 dark:text-neutral-200">
              Import
            </Text>
          </Pressable>
        </View>
        {printers.isError ? <Text className="pt-2 text-footnote text-red-500">{String(printers.error)}</Text> : null}
      </View>
      {printers.isPending ? (
        <View className="flex-1 items-center justify-center gap-2">
          <ActivityIndicator />
          <Text className="text-footnote text-neutral-500">Loading printer catalog…</Text>
        </View>
      ) : (
        <SectionList
          sections={sections}
          keyExtractor={(item) => `${item.vendor}::${item.model}`}
          stickySectionHeadersEnabled
          contentContainerClassName="pb-10"
          renderSectionHeader={({ section }) => (
            <View className="bg-neutral-50 px-5 pb-1 pt-5 dark:bg-neutral-950">
              <Text className="text-caption font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
                {section.title}
              </Text>
            </View>
          )}
          renderItem={({ item, index, section }) => {
            const id = `${item.vendor}::${item.model}`
            const download = byId[id]
            const isBusy = download !== undefined && (download.state === 'downloading' || download.state === 'installing')
            const isInstalled = installed.data?.includes(item.vendor) === true
            const first = index === 0
            const last = index === section.data.length - 1
            return (
              <View className="px-5">
                <View
                  className={`flex-row items-center gap-3 bg-white px-4 py-3 dark:bg-neutral-900 ${first ? 'rounded-t-2xl' : ''} ${last ? 'rounded-b-2xl' : ''}`}
                >
                  <PrinterCover vendor={item.vendor} model={item.model} />
                  <View className="flex-1">
                    <Text className="text-subhead font-semibold tracking-tight text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                      {item.model}
                    </Text>
                    <Text className="mt-0.5 text-footnote text-neutral-500 dark:text-neutral-400">
                      {isBusy
                        ? download.state === 'installing'
                          ? 'Installing…'
                          : download.total > 0
                            ? `Downloading · ${download.done} / ${download.total}`
                            : 'Downloading…'
                        : isInstalled
                          ? 'Installed · tap to select'
                          : `${item.vendorName} · downloads on tap`}
                    </Text>
                  </View>
                  {isBusy ? (
                    <ActivityIndicator />
                  ) : (
                    <Button
                      title={isInstalled ? 'Select' : 'Install'}
                      variant={isInstalled ? 'secondary' : 'primary'}
                      onPress={() => start(item)}
                    />
                  )}
                </View>
                {!last ? <View className="ml-16 h-px bg-neutral-100 dark:bg-neutral-800" /> : null}
              </View>
            )
          }}
          ListEmptyComponent={
            <View className="items-center px-5 py-16">
              <Text className="text-subhead text-neutral-500">Nothing matches your search.</Text>
            </View>
          }
        />
      )}
    </View>
  )
}

function PrinterCover({ vendor, model }: { vendor: string; model: string }): React.JSX.Element {
  const [ok, setOk] = useState(true)
  const url = `https://raw.githubusercontent.com/${DEFAULT_PROFILE_SOURCE.owner}/${DEFAULT_PROFILE_SOURCE.repo}/${DEFAULT_PROFILE_SOURCE.ref}/${encodeURI(`resources/profiles/${vendor}/${model}_cover.png`)}`
  if (!ok) return <Avatar text={model} size={44} />
  return (
    <Image
      source={{ uri: url }}
      className="h-11 w-11 rounded-xl bg-neutral-100 dark:bg-neutral-800"
      resizeMode="contain"
      onError={() => setOk(false)}
    />
  )
}
