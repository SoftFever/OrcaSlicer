import React from 'react'
import { Text, View } from 'react-native'

type Props = { text: string; size?: number }

// Deterministic hue from a string: a vendor / preset name always shows the
// same colored initial disc, cheaper than fetching an image and it still
// scans as an identity in the list.
function hue(s: string): number {
  let h = 0
  for (let i = 0; i < s.length; ++i) h = (h * 31 + s.charCodeAt(i)) | 0
  return Math.abs(h) % 360
}

export function Avatar({ text, size = 32 }: Props): React.JSX.Element {
  const bg = `hsl(${hue(text)}, 55%, 65%)`
  const initials = text
    .split(/\s+/)
    .slice(0, 2)
    .map((w) => (w[0] ?? '').toUpperCase())
    .join('')
  return (
    <View
      className="items-center justify-center rounded-full"
      style={{ width: size, height: size, backgroundColor: bg }}
    >
      <Text className="font-bold text-white" style={{ fontSize: size * 0.42 }}>
        {initials || '?'}
      </Text>
    </View>
  )
}
