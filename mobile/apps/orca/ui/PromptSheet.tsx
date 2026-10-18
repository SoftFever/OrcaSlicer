import React, { useEffect, useState } from 'react'

import { colors } from '@/lib/theme'
import { Modal, Pressable, Text, TextInput, View } from 'react-native'

import { Button } from './Button'

type Props = {
  visible: boolean
  title: string
  message?: string
  placeholder?: string
  defaultValue?: string
  confirmLabel?: string
  onCancel: () => void
  onConfirm: (value: string) => void
}

// Small text-input prompt sheet. iOS system dialog with a text field would
// be nicer but is not exposed by Alert.prompt; a page-sheet Modal with the
// same chrome as the picker sheet does the job.
export function PromptSheet({
  visible,
  title,
  message,
  placeholder,
  defaultValue = '',
  confirmLabel = 'Save',
  onCancel,
  onConfirm,
}: Props): React.JSX.Element {
  const [value, setValue] = useState(defaultValue)
  useEffect(() => {
    if (visible) setValue(defaultValue)
  }, [visible, defaultValue])
  const trimmed = value.trim()
  return (
    <Modal visible={visible} animationType="slide" presentationStyle="pageSheet" onRequestClose={onCancel}>
      <View className="flex-1 bg-neutral-50 dark:bg-neutral-950">
        <View className="flex-row items-center justify-between border-b border-neutral-100 px-5 pb-3 pt-4 dark:border-neutral-800">
          <Text className="text-body font-semibold text-neutral-900 dark:text-neutral-100">{title}</Text>
          <Pressable hitSlop={8} onPress={onCancel}>
            <Text className="text-subhead font-semibold text-neutral-500">Cancel</Text>
          </Pressable>
        </View>
        <View className="gap-3 px-5 pt-4">
          {message !== undefined ? (
            <Text className="text-subhead text-neutral-600 dark:text-neutral-400">{message}</Text>
          ) : null}
          <TextInput
            className="rounded-xl bg-neutral-100 px-3 py-3 text-headline text-neutral-900 dark:bg-neutral-900 dark:text-neutral-100"
            value={value}
            onChangeText={setValue}
            placeholder={placeholder}
            placeholderTextColor={colors.placeholder}
            autoCorrect={false}
            autoCapitalize="none"
            autoFocus
          />
          <View className="pt-2">
            <Button title={confirmLabel} onPress={() => onConfirm(trimmed)} disabled={trimmed === ''} fullWidth />
          </View>
        </View>
      </View>
    </Modal>
  )
}
