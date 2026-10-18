import { Stack, useLocalSearchParams, useRouter } from 'expo-router'
import React, { useEffect } from 'react'
import { ScrollView, Text, View } from 'react-native'

import { helpTopic } from '@/lib/help/content'
import { Card } from '@/ui/Card'
import { HelpStep } from '@/ui/HelpStep'

// One guide: its sections as cards, steps numbered through the whole topic so
// the numbers match the markdown in docs/mobile.

export default function HelpTopicScreen(): React.JSX.Element {
  const router = useRouter()
  const { topic: id } = useLocalSearchParams<{ topic: string }>()
  const topic = helpTopic(id)

  useEffect(() => {
    if (topic === undefined) router.replace('/help')
  }, [topic, router])

  if (topic === undefined) return <View className="flex-1 bg-neutral-50 dark:bg-neutral-950" />

  // Steps are numbered through the whole topic; each section starts where the previous ended.
  const firstStep = topic.sections.map((_, i) =>
    topic.sections.slice(0, i).reduce((n, s) => n + s.steps.length, 1),
  )
  return (
    <ScrollView
      className="flex-1 bg-neutral-50 dark:bg-neutral-950"
      contentContainerClassName="gap-4 p-5 pb-10"
    >
      <Stack.Screen options={{ title: topic.title }} />
      <Text className="px-1 text-subhead leading-6 text-neutral-600 dark:text-neutral-300">
        {topic.summary}
      </Text>
      {topic.sections.map((section, i) => (
        <Card key={section.title} title={section.title} index={i}>
          {section.intro !== undefined ? (
            <Text className="mb-4 text-subhead leading-6 text-neutral-600 dark:text-neutral-300">
              {section.intro}
            </Text>
          ) : null}
          <View className="gap-5">
            {section.steps.map((step, j) => (
              <HelpStep key={step.title} index={firstStep[i]! + j} step={step} />
            ))}
          </View>
        </Card>
      ))}
    </ScrollView>
  )
}
