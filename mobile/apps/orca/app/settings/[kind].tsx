import { File } from 'expo-file-system'
import * as Sharing from 'expo-sharing'
import { Stack, useLocalSearchParams } from 'expo-router'
import React, { useCallback, useMemo, useState } from 'react'
import { Alert, FlatList, Modal, Pressable, ScrollView, Switch, Text, TextInput, View } from 'react-native'
import { getOptionDefinitions, type OptionDefinition, type OrcaSession, type PresetKind } from 'react-native-orca-core'

import { useCore, useSession } from '@/lib/core'
import { colors } from '@/lib/theme'
import { scheduleSync } from '@/lib/cloud/sync'
import { exportPresetBundle } from '@/lib/presetFiles'
import { t } from '@/lib/i18n'
import {
  decodeBool,
  decodePercent,
  decodeScalar,
  editorKind,
  encodeBool,
  encodePercent,
  encodeScalar,
  groupByCategory,
  matchesQuery,
  visibleInMode,
  type OptionMode,
} from '@/lib/options'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'
import { Chip } from '@/ui/Chip'
import { PromptSheet } from '@/ui/PromptSheet'

// The parameter editor. Rows are generated from the option definitions the
// core exports, grouped by category and filtered by mode and search; every
// edit goes through Session.set_option in serialized form.

const TITLES: Record<PresetKind, string> = {
  printer: 'Printer settings',
  filament: 'Filament settings',
  process: 'Process settings',
}
const MODES: OptionMode[] = ['simple', 'advanced', 'expert']

function isPresetKind(value: string | string[] | undefined): value is PresetKind {
  return value === 'printer' || value === 'filament' || value === 'process'
}

interface RowProps {
  def: OptionDefinition
  session: OrcaSession
  modified: boolean
  onChanged: () => void
  last?: boolean
}

function OptionRow({ def, session, modified, onChanged, last = false }: RowProps): React.JSX.Element {
  const serialized = session.option(def.key)
  const kind = editorKind(def, serialized)
  const [draft, setDraft] = useState<string | null>(null)
  const [error, setError] = useState(false)
  const [picking, setPicking] = useState(false)

  const commit = useCallback(
    (value: string) => {
      const ok = session.setOption(def.key, value)
      setError(!ok)
      if (ok) onChanged()
    },
    [def.key, session, onChanged],
  )

  const scalar = decodeScalar(def, serialized)
  const label = def.label !== '' ? t(def.label) : def.key

  let editor: React.JSX.Element
  switch (kind) {
    case 'bool':
      editor = (
        <Switch
          value={decodeBool(scalar)}
          disabled={def.readonly}
          onValueChange={(v) => commit(encodeScalar(def, serialized, encodeBool(v)))}
        />
      )
      break
    case 'enum': {
      const index = def.enumValues?.indexOf(scalar) ?? -1
      const shown = index >= 0 ? t(def.enumLabels?.[index] ?? scalar) : scalar
      editor = (
        <>
          <Pressable
            onPress={def.readonly ? undefined : () => setPicking(true)}
            className={`rounded-lg bg-neutral-100 px-3 py-1.5 dark:bg-neutral-800 ${def.readonly ? 'opacity-40' : 'active:opacity-60'}`}
          >
            <Text className="text-callout font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
              {shown === '' ? 'Choose' : shown}
            </Text>
          </Pressable>
          <Modal
            visible={picking}
            animationType="slide"
            presentationStyle="pageSheet"
            onRequestClose={() => setPicking(false)}
          >
            <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
              <View className="flex-row items-center justify-between border-b border-neutral-100 px-5 pb-3 pt-4 dark:border-neutral-800">
                <Text className="text-body font-semibold text-neutral-900 dark:text-neutral-100">{label}</Text>
                <Pressable hitSlop={8} onPress={() => setPicking(false)}>
                  <Text className="text-subhead font-semibold text-neutral-500">Cancel</Text>
                </Pressable>
              </View>
              <FlatList
                data={def.enumValues ?? []}
                keyExtractor={(v) => v}
                contentContainerClassName="px-5 pt-4 pb-8"
                renderItem={({ item, index: i }) => {
                  const isSelected = item === scalar
                  const isLast = i === (def.enumValues?.length ?? 0) - 1
                  return (
                    <Pressable
                      onPress={() => {
                        setPicking(false)
                        commit(encodeScalar(def, serialized, item))
                      }}
                      className={`flex-row items-center gap-3 bg-white px-4 py-3 dark:bg-neutral-900 ${i === 0 ? 'rounded-t-2xl' : ''} ${isLast ? 'rounded-b-2xl' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
                    >
                      <Text className="flex-1 text-subhead text-neutral-900 dark:text-neutral-100">
                        {t(def.enumLabels?.[i] ?? item)}
                      </Text>
                      {isSelected ? <Text className="text-lead text-neutral-900 dark:text-white">✓</Text> : null}
                    </Pressable>
                  )
                }}
              />
            </View>
          </Modal>
        </>
      )
      break
    }
    case 'number':
    case 'percent':
    case 'float_or_percent':
    case 'text':
    case 'code':
    case 'raw': {
      const isPercent = kind === 'percent'
      const shownValue = draft ?? (isPercent ? decodePercent(scalar) : kind === 'raw' ? serialized : scalar)
      const inputClasses = [
        'rounded-lg bg-neutral-100 px-3 py-2 text-callout text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100',
        kind === 'code' ? 'min-h-[80px] text-left font-mono text-label' : 'min-w-[64px] text-right',
        error ? 'border border-red-500' : '',
      ].join(' ')
      editor = (
        <View className="flex-row items-center gap-1">
          <TextInput
            className={inputClasses}
            value={shownValue}
            editable={!def.readonly}
            multiline={kind === 'code'}
            autoCorrect={false}
            autoCapitalize="none"
            keyboardType={kind === 'number' || kind === 'percent' ? 'numbers-and-punctuation' : 'default'}
            onChangeText={setDraft}
            onBlur={() => {
              if (draft === null) return
              const value = isPercent ? encodePercent(draft) : draft
              commit(kind === 'raw' ? value : encodeScalar(def, serialized, value))
              setDraft(null)
            }}
          />
          {def.unit !== '' ? <Text className="text-footnote text-neutral-500">{t(def.unit)}</Text> : null}
          {isPercent ? <Text className="text-footnote text-neutral-500">%</Text> : null}
        </View>
      )
      break
    }
  }

  return (
    <View
      className={`flex-row items-start gap-3 px-4 py-3 ${last ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
    >
      <View className="flex-1">
        <View className="flex-row items-center gap-2">
          {modified ? <View className="h-1.5 w-1.5 rounded-full bg-accent-500" /> : null}
          <Text className="text-callout font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={2}>
            {label}
          </Text>
        </View>
        {def.tooltip !== '' ? (
          <Text className="mt-1 text-label leading-4 text-neutral-500 dark:text-neutral-400" numberOfLines={3}>
            {t(def.tooltip)}
          </Text>
        ) : null}
        {error ? (
          <Text className="mt-1 text-label text-red-500">Not a valid value for this option.</Text>
        ) : null}
      </View>
      <View className="min-w-[110px] items-end justify-center">{editor}</View>
    </View>
  )
}

export default function SettingsScreen(): React.JSX.Element {
  const params = useLocalSearchParams<{ kind: string }>()
  const kind: PresetKind = isPresetKind(params.kind) ? params.kind : 'process'
  const session = useSession()
  const { reloadPresets } = useCore()
  const [mode, setMode] = useState<OptionMode>('simple')
  const [query, setQuery] = useState('')
  const [category, setCategory] = useState<string | null>(null)
  const [revision, setRevision] = useState(0)
  const [saving, setSaving] = useState(false)
  const [renaming, setRenaming] = useState(false)

  const onChanged = useCallback(() => setRevision((r) => r + 1), [])

  const saveAs = useCallback(
    async (name: string) => {
      if (session === null) return
      setSaving(true)
      try {
        const ok = session.savePresetAs(kind, name)
        if (!ok) {
          Alert.alert('Could not save', `A system preset called "${name}" already exists.`)
          return
        }
        await reloadPresets()
        scheduleSync()
        setRenaming(false)
        onChanged()
      } finally {
        setSaving(false)
      }
    },
    [session, kind, reloadPresets, onChanged],
  )

  const currentIsUser = useMemo(() => {
    if (session === null) return false
    const name = session.selectedPreset(kind)
    const info = session.presets(kind).find((p) => p.name === name)
    return info?.isUser === true
  }, [session, kind, revision])

  const deleteCurrent = useCallback(() => {
    if (session === null) return
    const name = session.selectedPreset(kind)
    Alert.alert('Delete preset', `Delete "${name}"? This cannot be undone.`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Delete',
        style: 'destructive',
        onPress: async () => {
          const ok = session.deletePreset(kind, name)
          if (!ok) {
            Alert.alert('Could not delete', 'System presets cannot be removed.')
            return
          }
          await reloadPresets()
          scheduleSync()
          onChanged()
        },
      },
    ])
  }, [session, kind, reloadPresets, onChanged])

  const exportCurrent = useCallback(() => {
    if (session === null) return
    const name = session.selectedPreset(kind)
    const path = session.presetFile(kind, name)
    if (path === '') {
      Alert.alert('Nothing to export', 'Built-in presets have no file of their own. Save the preset under your own name first.')
      return
    }
    const shareJson = () => Sharing.shareAsync(new File(path).uri, { mimeType: 'application/json', dialogTitle: `${name}.json` })
    // A printer bundle carries the printer plus every user filament and process
    // that is compatible with it: what desktop's "Printer config bundle" writes.
    const shareBundle = () => {
      const refs = kind === 'printer'
        ? [
            { kind: 'printer' as const, name },
            ...session.presets('filament').filter((p) => p.isUser && p.isCompatible).map((p) => ({ kind: 'filament' as const, name: p.name })),
            ...session.presets('process').filter((p) => p.isUser && p.isCompatible).map((p) => ({ kind: 'process' as const, name: p.name })),
          ]
        : [{ kind, name }]
      void exportPresetBundle(session, refs, `Export ${name}`)
    }
    Alert.alert('Export preset', 'Share as a single JSON file, or as a bundle the desktop app imports with File ▸ Import ▸ Import Configs.', [
      { text: 'Cancel', style: 'cancel' },
      { text: 'JSON file', onPress: () => void shareJson() },
      { text: kind === 'printer' ? 'Printer bundle (.orca_printer)' : 'Bundle (.orca_bundle)', onPress: shareBundle },
    ])
  }, [session, kind])

  const definitions = useMemo(
    () => getOptionDefinitions().filter((d) => d.kind === kind && visibleInMode(d, mode) && matchesQuery(d, query)),
    [kind, mode, query],
  )
  const groups = useMemo(() => groupByCategory(definitions), [definitions])
  const shownGroups = category === null ? groups : groups.filter((g) => g.category === category)
  const modified = useMemo(() => new Set(session?.modifiedOptions(kind) ?? []), [session, kind, revision])

  if (session === null) {
    return <View className="flex-1 bg-neutral-50 dark:bg-neutral-950" />
  }

  const currentPreset = session.selectedPreset(kind)

  return (
    <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
      <Stack.Screen options={{ title: TITLES[kind] }} />

      <View className="border-b border-neutral-100 bg-white px-5 pb-3 pt-3 dark:border-neutral-800 dark:bg-neutral-900">
        <Text className="mb-2 text-footnote text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
          Editing <Text className="font-semibold text-neutral-900 dark:text-neutral-100">{currentPreset}</Text>
        </Text>
        <View className="flex-row items-center rounded-2xl bg-neutral-100 px-3 py-2 dark:bg-neutral-800">
          <Text className="mr-2 text-subhead text-neutral-400">Search</Text>
          <TextInput
            className="flex-1 text-subhead text-neutral-900 dark:text-neutral-100"
            placeholder="setting name…"
            placeholderTextColor={colors.placeholder}
            value={query}
            onChangeText={setQuery}
            autoCorrect={false}
            autoCapitalize="none"
            clearButtonMode="while-editing"
          />
        </View>
        <View className="mt-3 flex-row items-center gap-2">
          <View className="flex-row gap-1.5">
            {MODES.map((m) => (
              <Chip key={m} label={m.charAt(0).toUpperCase() + m.slice(1)} selected={mode === m} onPress={() => setMode(m)} />
            ))}
          </View>
          <View className="ml-auto flex-row gap-1">
            {modified.size > 0 ? (
              <Button
                title={`Discard ${modified.size}`}
                variant="ghost"
                onPress={() => {
                  session.discardModifiedOptions(kind)
                  onChanged()
                }}
              />
            ) : null}
            <Button title="Save as" variant="secondary" onPress={() => setRenaming(true)} />
            <Button title="Export" variant="ghost" onPress={exportCurrent} />
            {currentIsUser ? (
              <Button title="Delete" variant="ghost" onPress={deleteCurrent} />
            ) : null}
          </View>
        </View>
        <ScrollView horizontal showsHorizontalScrollIndicator={false} className="mt-3">
          <View className="flex-row gap-1.5">
            <Chip label="All" selected={category === null} onPress={() => setCategory(null)} />
            {groups.map((g) => (
              <Chip
                key={g.category}
                label={t(g.category)}
                selected={category === g.category}
                onPress={() => setCategory(g.category)}
              />
            ))}
          </View>
        </ScrollView>
      </View>

      <PromptSheet
        visible={renaming}
        title="Save as new preset"
        message={`Fork "${currentPreset}" under a new user preset name. The new preset is selected right away.`}
        placeholder="e.g. My PLA Fast"
        defaultValue={currentPreset.replace(/^Default /, '')}
        confirmLabel={saving ? 'Saving…' : 'Save'}
        onCancel={() => setRenaming(false)}
        onConfirm={saveAs}
      />

      <ScrollView contentContainerClassName="px-4 pt-4 pb-8">
        {query === '' && category === null ? (
          <CustomGCodeCard kind={kind} session={session} onChanged={onChanged} />
        ) : null}
        {shownGroups.length === 0 ? (
          <Text className="px-4 py-8 text-center text-subhead text-neutral-500">No settings match.</Text>
        ) : (
          shownGroups.map((group, groupIdx) => (
            <Card
              key={group.category}
              title={t(group.category)}
              className="mb-4"
              padded={false}
              index={groupIdx}
              collapsible
              collapseKey={`settings:${kind}:${group.category}`}
            >
              {group.options.map((def, i) => (
                <OptionRow
                  key={def.key}
                  def={def}
                  session={session}
                  modified={modified.has(def.key)}
                  onChanged={onChanged}
                  last={i === group.options.length - 1}
                />
              ))}
            </Card>
          ))
        )}
      </ScrollView>
    </View>
  )
}

// Keys grouped per preset kind. Editing these through the generic option
// browser is painful because they are multi-line and dense; a dedicated
// card with a code-style TextInput is faster.
const CUSTOM_GCODE_KEYS: Record<PresetKind, Array<{ key: string; label: string }>> = {
  printer: [
    { key: 'machine_start_gcode', label: 'Start G-code' },
    { key: 'machine_end_gcode', label: 'End G-code' },
  ],
  process: [
    { key: 'before_layer_change_gcode', label: 'Before layer change' },
    { key: 'layer_change_gcode', label: 'After layer change' },
  ],
  filament: [
    { key: 'filament_start_gcode', label: 'Filament start G-code' },
    { key: 'filament_end_gcode', label: 'Filament end G-code' },
  ],
}

function CustomGCodeCard({
  kind,
  session,
  onChanged,
}: {
  kind: PresetKind
  session: OrcaSession
  onChanged: () => void
}): React.JSX.Element | null {
  const entries = CUSTOM_GCODE_KEYS[kind]
  if (entries.length === 0) return null
  return (
    <Card title="Custom G-code" className="mb-4" collapsible defaultCollapsed collapseKey={`settings:${kind}:custom-gcode`}>
      <View className="gap-4">
        {entries.map((entry) => (
          <GCodeField key={entry.key} label={entry.label} field={entry.key} session={session} onChanged={onChanged} />
        ))}
      </View>
    </Card>
  )
}

function GCodeField({
  label,
  field,
  session,
  onChanged,
}: {
  label: string
  field: string
  session: OrcaSession
  onChanged: () => void
}): React.JSX.Element {
  const stored = session.option(field)
  const [draft, setDraft] = useState<string | null>(null)
  const value = draft ?? stored
  return (
    <View>
      <Text className="mb-2 text-label font-semibold uppercase tracking-caps text-neutral-500 dark:text-neutral-400">
        {label}
      </Text>
      <TextInput
        multiline
        value={value}
        onChangeText={setDraft}
        onBlur={() => {
          if (draft !== null && draft !== stored) {
            session.setOption(field, draft)
            onChanged()
          }
          setDraft(null)
        }}
        placeholder="; empty"
        placeholderTextColor={colors.placeholder}
        autoCorrect={false}
        autoCapitalize="none"
        style={{ fontFamily: 'Menlo', fontSize: 12 }}
        className="min-h-[92px] rounded-xl bg-neutral-100 px-3 py-2 text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100"
      />
    </View>
  )
}
