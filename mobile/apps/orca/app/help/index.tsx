import { useRouter } from 'expo-router'
import React from 'react'
import { Pressable, ScrollView, Text, View } from 'react-native'

import { HELP_TOPICS } from '@/lib/help/content'
import { Card } from '@/ui/Card'
import { Icon } from '@/ui/Icon'

// The guide list. Each topic opens help/[topic].

export default function HelpScreen(): React.JSX.Element {
  const router = useRouter()
  return (
    <ScrollView
      className="flex-1 bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="gap-4 p-5 pb-10"
    >
      {HELP_TOPICS.map((topic, i) => (
        <Pressable
          key={topic.id}
          onPress={() => router.push(`/help/${topic.id}`)}
          className="active:opacity-80"
        >
          <Card index={i}>
            <View className="flex-row items-center gap-3">
              <View className="flex-1">
                <Text className="text-body font-semibold text-neutral-900 dark:text-neutral-100">
                  {topic.title}
                </Text>
                <Text className="mt-1 text-callout leading-5 text-neutral-500 dark:text-neutral-400">
                  {topic.summary}
                </Text>
              </View>
              <Icon name="chevron" size={16} />
            </View>
          </Card>
        </Pressable>
      ))}
      <Text className="px-1 text-footnote leading-5 text-neutral-500 dark:text-neutral-400">
        The same guides are in the repository under docs/mobile.
      </Text>
    </ScrollView>
  )
}
