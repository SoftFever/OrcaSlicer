import { useRouter } from 'expo-router'
import React, { useCallback, useState } from 'react'
import {
  ActivityIndicator,
  Alert,
  Linking,
  Pressable,
  ScrollView,
  Switch,
  Text,
  TextInput,
  View,
} from 'react-native'

import { CloudAuthError, signInWithPassword, signOut } from '@/lib/cloud/auth'
import { colors } from '@/lib/theme'
import { CLOUD_WEB } from '@/lib/cloud/config'
import { useCloudStore } from '@/lib/cloud/store'
import { syncNow } from '@/lib/cloud/sync'
import { useCore } from '@/lib/core'
import { relativeTime } from '@/lib/time'
import { Avatar } from '@/ui/Avatar'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'

// Orca Cloud account. Signed out: email + password, or the web sign-in for
// Google / Apple / GitHub. Signed in: who, the sync switch, Sync now, status.

export default function AccountScreen(): React.JSX.Element {
  const router = useRouter()
  const { setCloudUser } = useCore()
  const { user, enabled, status, lastSyncAt, lastError, notSynced, setEnabled } = useCloudStore()
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)

  const signIn = useCallback(async () => {
    if (email.trim() === '' || password === '') return
    setBusy(true)
    try {
      const session = await signInWithPassword(email, password)
      setPassword('')
      await setCloudUser(session.user)
    } catch (e) {
      const message = e instanceof CloudAuthError ? e.message : String(e)
      Alert.alert('Could not sign in', message)
    } finally {
      setBusy(false)
    }
  }, [email, password, setCloudUser])

  const leave = useCallback(() => {
    Alert.alert(
      'Sign out?',
      'Your presets stay on this device. Presets you make while signed out are kept separately until you sign in again.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Sign out',
          style: 'destructive',
          onPress: async () => {
            setBusy(true)
            try {
              await signOut()
              await setCloudUser(null)
            } finally {
              setBusy(false)
            }
          },
        },
      ],
    )
  }, [setCloudUser])

  if (user === null) {
    return (
      <ScrollView
        className="flex-1 bg-neutral-50 dark:bg-neutral-950"
        contentContainerClassName="gap-4 p-5 pb-10"
        keyboardShouldPersistTaps="handled"
      >
        <Card title="Orca Cloud" subtitle="Keep presets in step with the desktop">
          <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
            Sign in with the account you use in desktop OrcaSlicer and your printer, filament and
            process presets sync both ways. Vendor profiles are not synced; each device downloads
            its own.
          </Text>
        </Card>

        <Card title="Sign in">
          <View className="gap-3">
            <TextInput
              className="rounded-2xl bg-neutral-100 px-4 py-3 text-headline text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100"
              placeholder="Email"
              placeholderTextColor={colors.placeholder}
              autoCapitalize="none"
              autoCorrect={false}
              keyboardType="email-address"
              textContentType="username"
              value={email}
              onChangeText={setEmail}
              editable={!busy}
            />
            <TextInput
              className="rounded-2xl bg-neutral-100 px-4 py-3 text-headline text-neutral-900 dark:bg-neutral-800 dark:text-neutral-100"
              placeholder="Password"
              placeholderTextColor={colors.placeholder}
              secureTextEntry
              textContentType="password"
              value={password}
              onChangeText={setPassword}
              onSubmitEditing={() => void signIn()}
              editable={!busy}
            />
            <Button
              title={busy ? 'Signing in…' : 'Sign in'}
              size="lg"
              fullWidth
              disabled={busy || email.trim() === '' || password === ''}
              onPress={() => void signIn()}
            />
            <Button
              title="Google, Apple or GitHub"
              variant="secondary"
              fullWidth
              disabled={busy}
              onPress={() => router.push('/account/login' as never)}
            />
            <Pressable
              onPress={() => void Linking.openURL(CLOUD_WEB)}
              hitSlop={8}
              className="items-center py-1 active:opacity-60"
            >
              <Text className="text-callout font-semibold text-neutral-500 dark:text-neutral-400">
                Create an account at cloud.orcaslicer.com
              </Text>
            </Pressable>
          </View>
        </Card>
      </ScrollView>
    )
  }

  const statusLine =
    status === 'syncing'
      ? 'Syncing…'
      : status === 'offline'
        ? 'Offline; will sync when back online'
        : status === 'error'
          ? `Last sync failed: ${lastError}`
          : lastSyncAt > 0
            ? `Last synced ${relativeTime(lastSyncAt)}`
            : 'Not synced yet'

  return (
    <ScrollView
      className="flex-1 bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="gap-4 p-5 pb-10"
    >
      <Card>
        <View className="flex-row items-center gap-3">
          <Avatar text={user.displayName || user.email || 'Orca'} size={44} />
          <View className="flex-1">
            <Text className="text-body font-semibold text-neutral-900 dark:text-neutral-100">
              {user.displayName || user.email}
            </Text>
            {user.email !== '' && user.displayName !== '' ? (
              <Text className="text-footnote text-neutral-500 dark:text-neutral-400">
                {user.email}
              </Text>
            ) : null}
          </View>
        </View>
      </Card>

      <Card title="Preset sync" subtitle={statusLine}>
        <View className="flex-row items-center justify-between py-1">
          <View className="flex-1 pr-4">
            <Text className="text-subhead font-medium text-neutral-900 dark:text-neutral-100">
              Sync automatically
            </Text>
            <Text className="text-footnote leading-4 text-neutral-500 dark:text-neutral-400">
              When the app opens and after you save or delete a preset.
            </Text>
          </View>
          <Switch value={enabled} onValueChange={setEnabled} />
        </View>
        <View className="mt-3 flex-row items-center gap-2">
          <Button
            title="Sync now"
            fullWidth
            disabled={status === 'syncing'}
            onPress={() => void syncNow()}
          />
          {status === 'syncing' ? <ActivityIndicator /> : null}
        </View>
        {notSynced.length > 0 ? (
          <Text className="mt-3 text-footnote leading-4 text-amber-600 dark:text-amber-400">
            Too large to sync, kept on this device only: {notSynced.join(', ')}
          </Text>
        ) : null}
      </Card>

      <Card title="Account">
        <Button title="Sign out" variant="destructive" fullWidth disabled={busy} onPress={leave} />
      </Card>
    </ScrollView>
  )
}
