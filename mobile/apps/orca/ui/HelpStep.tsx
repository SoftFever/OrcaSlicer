import React from 'react'
import { Image, Text, View } from 'react-native'

import { helpImages } from '@/lib/help/images'
import type { HelpStep as Step } from '@/lib/help/content'

// One numbered step of a guide: title, body and, when the screenshot has been
// captured, the image at its own aspect ratio. Images are bundled assets, so
// `Image.resolveAssetSource` knows their size without a round trip.

type Props = { index: number; step: Step }

export function HelpStep({ index, step }: Props): React.JSX.Element {
  const asset = step.image !== undefined ? helpImages[step.image] : undefined
  const size = asset !== undefined ? Image.resolveAssetSource(asset) : undefined
  const ratio = size !== undefined && size.width > 0 && size.height > 0 ? size.width / size.height : undefined
  return (
    <View className="gap-2">
      <View className="flex-row items-start gap-3">
        <View className="mt-0.5 h-7 w-7 items-center justify-center rounded-full bg-neutral-100 dark:bg-neutral-800">
          <Text className="text-footnote font-bold text-neutral-700 dark:text-neutral-200">{index}</Text>
        </View>
        <View className="flex-1">
          <Text className="text-headline font-semibold text-neutral-900 dark:text-neutral-100">{step.title}</Text>
          <Text className="mt-1 text-subhead leading-6 text-neutral-600 dark:text-neutral-300">{step.body}</Text>
        </View>
      </View>
      {asset !== undefined ? (
        <View className="ml-10 overflow-hidden rounded-2xl border border-neutral-200 bg-neutral-100 dark:border-neutral-800 dark:bg-neutral-900">
          <Image
            source={asset}
            resizeMode="contain"
            accessibilityLabel={step.alt ?? step.title}
            style={{ width: '100%', aspectRatio: ratio ?? 16 / 10 }}
          />
        </View>
      ) : null}
    </View>
  )
}
