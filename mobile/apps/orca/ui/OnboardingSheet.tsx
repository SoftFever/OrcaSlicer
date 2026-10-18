import { useRouter } from 'expo-router'
import React from 'react'
import { Image, Modal, Pressable, Text, View } from 'react-native'
import { SafeAreaView } from 'react-native-safe-area-context'

import { markOnboarded } from '@/lib/onboarding'
import { Button } from '@/ui/Button'

// First-launch overlay. Full-screen modal that introduces the three things a
// new user has to do before they can slice — pick a printer, import a model,
// try their first slice — with a single tap that seeds each of them. Dismissed
// (Get started) marks the MMKV flag so it never shows again.

type Props = {
  visible: boolean
  onDismiss: () => void
}

export function OnboardingSheet({ visible, onDismiss }: Props): React.JSX.Element {
  const router = useRouter()
  const done = (nav?: () => void): void => {
    markOnboarded()
    onDismiss()
    nav?.()
  }
  return (
    <Modal visible={visible} animationType="fade" presentationStyle="fullScreen">
      <SafeAreaView className="flex-1 bg-neutral-50 dark:bg-neutral-950">
        <View className="flex-1 justify-between px-6 pb-6 pt-10">
          <View className="gap-8">
            <View className="items-center gap-3">
              <Image source={require('../assets/icon.png')} className="h-16 w-16 rounded-2xl" accessibilityIgnoresInvertColors />
              <Text className="text-title1 font-bold tracking-tight text-neutral-900 dark:text-neutral-100">
                Welcome to Orca
              </Text>
              <Text className="text-center text-subhead leading-6 text-neutral-500 dark:text-neutral-400">
                Slicing on your phone or iPad, with the same core the desktop uses.
              </Text>
            </View>

            <View className="gap-4">
              <Step
                index={1}
                title="Pick a printer"
                body="Choose your machine from the built-in catalog. Presets download automatically."
              />
              <Step
                index={2}
                title="Import a model"
                body="Bring in a .3mf, STL, OBJ or STEP from Files, iCloud, or a URL."
              />
              <Step
                index={3}
                title="Slice and print"
                body="Tap Slice, then send the G-code to any Moonraker or OctoPrint host on your network."
              />
            </View>
          </View>

          <View className="gap-3">
            <Button
              title="Choose a printer"
              size="lg"
              fullWidth
              onPress={() => done(() => router.push('/vendors'))}
            />
            <Button
              title="Read the guide"
              variant="secondary"
              size="lg"
              fullWidth
              onPress={() => done(() => router.push('/help'))}
            />
            <Pressable
              onPress={() => done()}
              hitSlop={12}
              className="items-center py-2 active:opacity-60"
              accessibilityRole="button"
            >
              <Text className="text-subhead font-semibold text-neutral-500 dark:text-neutral-400">
                Skip for now
              </Text>
            </Pressable>
          </View>
        </View>
      </SafeAreaView>
    </Modal>
  )
}

function Step({ index, title, body }: { index: number; title: string; body: string }): React.JSX.Element {
  return (
    <View className="flex-row items-start gap-3">
      <View className="mt-0.5 h-7 w-7 items-center justify-center rounded-full bg-neutral-100 dark:bg-neutral-800">
        <Text className="text-footnote font-bold text-neutral-700 dark:text-neutral-200">{index}</Text>
      </View>
      <View className="flex-1">
        <Text className="text-headline font-semibold text-neutral-900 dark:text-neutral-100">{title}</Text>
        <Text className="mt-0.5 text-callout leading-5 text-neutral-500 dark:text-neutral-400">{body}</Text>
      </View>
    </View>
  )
}
