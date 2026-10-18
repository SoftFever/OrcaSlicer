import React, { useMemo, useState } from 'react'
import { Image, Modal, Pressable, SectionList, Text, TextInput, View } from 'react-native'
import type { PresetInfo, PresetKind } from 'react-native-orca-core'

import { Avatar } from '@/ui/Avatar'
import { colors } from '@/lib/theme'
import { DEFAULT_PROFILE_SOURCE } from '@/lib/profiles'

type Props = {
  kind: PresetKind
  title: string
  presets: PresetInfo[]
  selected: string
  visible: boolean
  onPick: (name: string) => void
  onClose: () => void
}

const KIND_LABEL: Record<PresetKind, string> = {
  printer: 'printer',
  filament: 'filament',
  process: 'process',
}

type Section = { title: string; data: PresetInfo[] }

// A page-sheet Modal that lists presets, filtered by a search field.
//
// For printer presets we group by vendor (matching the desktop wizard's
// mental model, where a vendor bundle contains many machine models). For
// filaments and processes we group by kind: selected, compatible, others.
export function PresetPicker({ kind, title, presets, selected, visible, onPick, onClose }: Props): React.JSX.Element {
  const [query, setQuery] = useState('')
  const needle = query.trim().toLowerCase()

  const sections = useMemo<Section[]>(() => {
    const filtered = presets.filter(
      (p) => needle === '' || p.name.toLowerCase().includes(needle) || p.vendor.toLowerCase().includes(needle),
    )
    if (kind === 'printer') {
      const byVendor = new Map<string, PresetInfo[]>()
      for (const p of filtered) {
        const key = p.vendor !== '' ? p.vendor : 'Other'
        const bucket = byVendor.get(key) ?? []
        bucket.push(p)
        byVendor.set(key, bucket)
      }
      const out: Section[] = []
      for (const [vendor, items] of Array.from(byVendor.entries()).sort((a, b) => a[0].localeCompare(b[0]))) {
        items.sort((a, b) => a.name.localeCompare(b.name))
        out.push({ title: vendor, data: items })
      }
      return out
    }
    const [compat, rest] = [filtered.filter((p) => p.isCompatible), filtered.filter((p) => !p.isCompatible)]
    compat.sort((a, b) => a.name.localeCompare(b.name))
    rest.sort((a, b) => a.name.localeCompare(b.name))
    const out: Section[] = []
    if (compat.length > 0) out.push({ title: 'Compatible', data: compat })
    if (rest.length > 0) out.push({ title: 'Others', data: rest })
    return out
  }, [presets, needle, kind])

  return (
    <Modal visible={visible} animationType="slide" presentationStyle="pageSheet" onRequestClose={onClose}>
      <View className="flex-1 bg-gray-50 dark:bg-black">
        <View className="flex-row items-center justify-between border-b border-gray-100 px-5 pb-3 pt-4 dark:border-neutral-800">
          <View className="flex-1 pr-4">
            <Text className="text-title2 font-bold text-black dark:text-white" numberOfLines={1}>
              {title}
            </Text>
            <Text className="mt-0.5 text-footnote text-gray-500 dark:text-gray-400">
              {presets.length} {KIND_LABEL[kind]} preset{presets.length === 1 ? '' : 's'} available
            </Text>
          </View>
          <Pressable hitSlop={10} onPress={onClose} className="active:opacity-60">
            <Text className="text-body font-semibold text-blue-500">Done</Text>
          </Pressable>
        </View>
        <View className="px-5 pt-3">
          <View className="flex-row items-center rounded-xl bg-gray-100 px-3 py-2 dark:bg-neutral-900">
            <Text className="mr-2 text-subhead text-gray-500 dark:text-gray-400">Search</Text>
            <TextInput
              className="flex-1 text-subhead text-black dark:text-white"
              placeholder={`${KIND_LABEL[kind]} name…`}
              placeholderTextColor={colors.placeholder}
              value={query}
              onChangeText={setQuery}
              autoCorrect={false}
              autoCapitalize="none"
              clearButtonMode="while-editing"
            />
          </View>
        </View>
        <SectionList
          sections={sections}
          keyExtractor={(item) => `${item.vendor}::${item.name}`}
          contentContainerClassName="pb-10"
          stickySectionHeadersEnabled
          renderSectionHeader={({ section }) => (
            <View className="bg-gray-50 px-5 pb-1 pt-4 dark:bg-black">
              <Text className="text-footnote font-semibold uppercase tracking-wider text-gray-500 dark:text-gray-400">
                {section.title}
              </Text>
            </View>
          )}
          ListEmptyComponent={
            <View className="items-center px-5 py-16">
              <Text className="text-subhead text-gray-500 dark:text-gray-400">
                {presets.length === 0
                  ? 'Install a vendor bundle to see presets here.'
                  : 'Nothing matches your search.'}
              </Text>
            </View>
          }
          renderItem={({ item, index, section }) => (
            <View className="px-5">
              <PresetItem
                item={item}
                kind={kind}
                isSelected={item.name === selected || item.isSelected}
                onPick={() => onPick(item.name)}
                first={index === 0}
                last={index === section.data.length - 1}
              />
            </View>
          )}
        />
      </View>
    </Modal>
  )
}

function coverURL(kind: PresetKind, vendor: string, name: string): string | null {
  if (kind !== 'printer' || vendor === '') return null
  const src = DEFAULT_PROFILE_SOURCE
  const path = `resources/profiles/${vendor}/${name}_cover.png`
  return `https://raw.githubusercontent.com/${src.owner}/${src.repo}/${src.ref}/${encodeURI(path)}`
}

function PresetItem({
  item,
  kind,
  isSelected,
  onPick,
  first,
  last,
}: {
  item: PresetInfo
  kind: PresetKind
  isSelected: boolean
  onPick: () => void
  first: boolean
  last: boolean
}): React.JSX.Element {
  const [imageOK, setImageOK] = useState(true)
  const url = coverURL(kind, item.vendor, item.name)
  const rounded = [first ? 'rounded-t-xl' : '', last ? 'rounded-b-xl' : ''].join(' ')
  return (
    <Pressable
      onPress={onPick}
      className={`flex-row items-center gap-3 bg-white px-4 py-3 dark:bg-neutral-900 active:bg-gray-100 dark:active:bg-neutral-800 ${rounded}`}
    >
      {url !== null && imageOK ? (
        <Image
          source={{ uri: url }}
          className="h-11 w-11 rounded-lg bg-gray-50 dark:bg-neutral-800"
          resizeMode="contain"
          onError={() => setImageOK(false)}
        />
      ) : (
        <Avatar text={item.name} size={44} />
      )}
      <View className="flex-1">
        <Text className="text-subhead font-medium text-black dark:text-white" numberOfLines={1}>
          {item.name}
        </Text>
        <Text className="mt-0.5 text-footnote text-gray-500 dark:text-gray-400" numberOfLines={1}>
          {item.isUser ? 'User preset' : item.vendor !== '' ? `${item.vendor} · system` : 'System'}
          {!item.isCompatible ? ' · not compatible' : ''}
        </Text>
      </View>
      {isSelected ? <Text className="text-title3 text-blue-500">✓</Text> : null}
      {!last ? (
        <View className="absolute inset-x-4 bottom-0 h-px bg-gray-100 dark:bg-neutral-800" style={{ left: 71 }} />
      ) : null}
    </Pressable>
  )
}
