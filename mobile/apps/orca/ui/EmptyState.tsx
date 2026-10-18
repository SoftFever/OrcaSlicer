import React from 'react'
import { Text, View } from 'react-native'

import { Button } from '@/ui/Button'
import { Icon, type IconName } from '@/ui/Icon'

// A card body for "nothing here yet": an icon, a short title, one line of
// guidance and the action that fills it. Used for the empty plate, the
// un-sliced preview and the destination list so all three read the same.

type Props = {
  icon: IconName
  title: string
  body: string
  action?: { title: string; onPress: () => void }
  /** A second, quieter action. */
  secondary?: { title: string; onPress: () => void }
}

export function EmptyState({ icon, title, body, action, secondary }: Props): React.JSX.Element {
  return (
    <View className="items-center gap-2 px-6 py-8">
      <View className="mb-1 h-12 w-12 items-center justify-center rounded-2xl bg-neutral-100 dark:bg-neutral-800">
        <Icon name={icon} size={24} />
      </View>
      <Text className="text-center text-headline font-semibold text-neutral-900 dark:text-neutral-100">
        {title}
      </Text>
      <Text className="text-center text-callout leading-5 text-neutral-500 dark:text-neutral-400">
        {body}
      </Text>
      {action !== undefined || secondary !== undefined ? (
        <View className="mt-3 w-full flex-row justify-center gap-2">
          {action !== undefined ? <Button title={action.title} onPress={action.onPress} /> : null}
          {secondary !== undefined ? (
            <Button title={secondary.title} variant="secondary" onPress={secondary.onPress} />
          ) : null}
        </View>
      ) : null}
    </View>
  )
}
