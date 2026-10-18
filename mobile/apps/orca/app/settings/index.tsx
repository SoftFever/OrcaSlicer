import { useRouter } from 'expo-router'
import React from 'react'
import { Pressable, ScrollView, Text, View } from 'react-native'

import { useCloudStore } from '@/lib/cloud/store'
import { useCore } from '@/lib/core'
import { pickAndImportPresets } from '@/lib/presetFiles'
import { relativeTime } from '@/lib/time'
import { Card } from '@/ui/Card'
import { Icon, type IconName } from '@/ui/Icon'

// The settings hub: everything that is not slicing, in one grouped list.
// Account, preset files, printers, help, storage and about each get a row;
// the row's subtitle shows the state that matters at a glance.

type RowProps = {
  icon: IconName
  title: string
  subtitle?: string
  onPress: () => void
  last?: boolean
}

function Row({ icon, title, subtitle, onPress, last = false }: RowProps): React.JSX.Element {
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="button"
      className={`flex-row items-center gap-3 px-4 py-3 active:bg-neutral-100 dark:active:bg-neutral-800 ${last ? '' : 'border-b border-neutral-100 dark:border-neutral-800'}`}
    >
      <View className="h-8 w-8 items-center justify-center rounded-lg bg-neutral-100 dark:bg-neutral-800">
        <Icon name={icon} size={18} />
      </View>
      <View className="flex-1">
        <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100">{title}</Text>
        {subtitle !== undefined ? (
          <Text className="mt-0.5 text-footnote text-neutral-500 dark:text-neutral-400" numberOfLines={1}>
            {subtitle}
          </Text>
        ) : null}
      </View>
      <Icon name="chevron" size={16} />
    </Pressable>
  )
}

export default function SettingsScreen(): React.JSX.Element {
  const router = useRouter()
  const { session, reloadPresets } = useCore()
  const { user, status, lastSyncAt, lastError } = useCloudStore()

  const accountLine =
    user === null ? 'Not signed in'
    : status === 'syncing' ? 'Syncing…'
    : status === 'error' ? `Sync failed: ${lastError}`
    : lastSyncAt > 0 ? `${user.displayName || user.email} · synced ${relativeTime(lastSyncAt)}`
    : user.displayName || user.email

  return (
    <ScrollView className="flex-1 bg-neutral-50 dark:bg-neutral-950" contentContainerClassName="gap-5 p-5 pb-10">
      <Card title="Account" padded={false} index={0}>
        <Row icon="cloud" title="Orca Cloud" subtitle={accountLine} onPress={() => router.push('/account')} last />
      </Card>

      <Card title="Presets" padded={false} index={1}>
        <Row
          icon="import"
          title="Import preset file"
          subtitle=".orca_printer, .orca_filament, .orca_bundle, .zip or .json from the desktop app"
          onPress={() => {
            if (session !== null) void pickAndImportPresets({ session, reloadPresets })
          }}
        />
        <Row icon="export" title="Export presets" subtitle="Share a bundle the desktop app can import" onPress={() => router.push('/settings/export')} />
        <Row icon="printer" title="Printers and profiles" subtitle="Add or remove vendor profiles" onPress={() => router.push('/vendors')} last />
      </Card>

      <Card title="App" padded={false} index={2}>
        <Row icon="help" title="Help" subtitle="Moving presets, using the app" onPress={() => router.push('/help')} />
        <Row icon="storage" title="Storage" subtitle="What is on this device, and clearing it" onPress={() => router.push('/storage')} />
        <Row icon="info" title="About" subtitle="Version, support, licence" onPress={() => router.push('/about')} last />
      </Card>
    </ScrollView>
  )
}
