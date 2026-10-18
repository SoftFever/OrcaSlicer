import Constants from 'expo-constants'
import { useRouter } from 'expo-router'
import React from 'react'
import { Linking, ScrollView, Text, View } from 'react-native'

import { buildNumber, marketingVersion } from '@/lib/appInfo'
import { Button } from '@/ui/Button'
import { Card } from '@/ui/Card'

// About / support screen.
//
// It carries the two things a sideloaded AGPL build has to say out loud: that
// this is an unofficial port rather than anything SoftFever ships, and where the
// corresponding source lives. The AGPL obliges us to offer source to whoever we
// hand a binary to, and for a build installed through AltStore there is no store
// listing to put that on — so it goes here.
//
// Upstream's donation links sit above ours deliberately. The port is a thin shell
// over years of other people's slicer work.

const UPSTREAM_REPO = 'https://github.com/SoftFever/OrcaSlicer'
const FORK_REPO = 'https://github.com/angelo-hub/orcaslicer'
const UPSTREAM_SPONSOR = 'https://github.com/sponsors/SoftFever'
const UPSTREAM_KOFI = 'https://ko-fi.com/G2G5IP3CP'
const PORT_SPONSOR = 'https://github.com/sponsors/angelo-hub'
const LICENSE = 'https://www.gnu.org/licenses/agpl-3.0.html'

const open = (url: string): void => {
  void Linking.openURL(url)
}

/** GitHub's "new issue" URL supports ?title=&body=. Pre-fill the environment
 * strip so users do not have to hand-type version / iOS build / device model
 * — the three fields a maintainer always asks for first. */
function issueUrl(version: string, build: string): string {
  const platform = `iOS (Expo ${Constants.expoConfig?.sdkVersion ?? 'unknown'})`
  const app = `Orca Mobile ${version}${build === '' ? '' : ` (${build})`}`
  const body = `\n\n---\nEnvironment (please leave this):\n- App: ${app}\n- Platform: ${platform}\n`
  const params = new URLSearchParams({ title: '', body })
  return `${FORK_REPO}/issues/new?${params.toString()}`
}

export default function AboutScreen(): React.JSX.Element {
  const router = useRouter()
  const version = marketingVersion
  const build = buildNumber

  return (
    <ScrollView
      className="flex-1 bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="gap-4 p-5 pb-10"
    >
      <Card title="Help" subtitle="Guides for the app and for moving presets">
        <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
          How to get your desktop presets onto the phone and back, and a tour of the app from
          picking a printer to sending G-code.
        </Text>
        <View className="mt-3 gap-2">
          <Button title="Open the guides" fullWidth onPress={() => router.push('/help')} />
        </View>
      </Card>

      <Card title="Orca Mobile" subtitle={build === '' ? version : `${version} (${build})`}>
        <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
          An unofficial iOS port of OrcaSlicer. It is not built, released or endorsed by the
          OrcaSlicer maintainers — please don&apos;t report problems with it to them.
        </Text>
        <View className="mt-3 gap-2">
          <Button
            title="OrcaSlicer project"
            variant="secondary"
            fullWidth
            onPress={() => open(UPSTREAM_REPO)}
          />
        </View>
      </Card>

      <Card title="Support OrcaSlicer" subtitle="The slicer this port is built on">
        <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
          Every bit of slicing here is upstream&apos;s work. If you only support one project, make
          it this one.
        </Text>
        <View className="mt-3 gap-2">
          <Button title="Sponsor SoftFever" fullWidth onPress={() => open(UPSTREAM_SPONSOR)} />
          <Button title="Ko-fi" variant="secondary" fullWidth onPress={() => open(UPSTREAM_KOFI)} />
        </View>
      </Card>

      <Card title="Support the port" subtitle="iOS-specific work only">
        <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
          Tips here fund the mobile port — the native module, the viewport and the app itself.
          Entirely optional; nothing in the app is gated behind it.
        </Text>
        <View className="mt-3 gap-2">
          <Button
            title="Sponsor the port"
            variant="secondary"
            fullWidth
            onPress={() => open(PORT_SPONSOR)}
          />
        </View>
      </Card>

      <Card title="License" subtitle="AGPL-3.0-or-later">
        <Text className="text-subhead leading-5 text-neutral-600 dark:text-neutral-300">
          This app is free software. You are entitled to the complete source for the build you are
          running, and to redistribute and modify it under the same terms.
        </Text>
        <View className="mt-3 gap-2">
          <Button
            title="Source code"
            variant="secondary"
            fullWidth
            onPress={() => open(FORK_REPO)}
          />
          <Button
            title="Read the license"
            variant="ghost"
            fullWidth
            onPress={() => open(LICENSE)}
          />
        </View>
      </Card>
      <Card title="On this device" padded>
        <View className="items-start gap-2">
          <Text className="text-subhead leading-5 text-neutral-500 dark:text-neutral-400">
            See how much space vendor profiles, the autosaved plate, and cached models are using,
            and clear anything that no longer serves you.
          </Text>
          <Button title="Storage" variant="secondary" onPress={() => router.push('/storage')} />
        </View>
      </Card>
      <Card title="Feedback" padded>
        <View className="items-start gap-2">
          <Text className="text-subhead leading-5 text-neutral-500 dark:text-neutral-400">
            Hit a bug or missing feature? Open an issue — the form is pre-filled with your app
            version and platform so we can jump straight to reproduction.
          </Text>
          <Button
            title="Report an issue"
            variant="secondary"
            onPress={() => open(issueUrl(version, build))}
          />
        </View>
      </Card>
    </ScrollView>
  )
}
