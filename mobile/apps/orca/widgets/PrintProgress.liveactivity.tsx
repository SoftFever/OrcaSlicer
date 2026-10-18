import { HStack, ProgressView, Spacer, Text, VStack } from '@expo/ui/swift-ui'
import { font, foregroundColor, lineLimit, padding } from '@expo/ui/swift-ui/modifiers'
import type { LiveActivityLayout } from 'expo-widgets'
import React from 'react'

// Live Activity for a running print: lock screen banner plus the Dynamic Island
// presentations. Same isolated-runtime rules as the widget — no hooks, no async,
// nothing referenced from outside this function.
//
// Teal (#009789) is the logo's accent and reads on the Dynamic Island's black.

export type PrintActivityProps = {
  printer: string
  file: string
  /** 0..1. */
  progress: number
  state: string
  nozzle: number | null
  bed: number | null
}

export function PrintProgressActivity(props: PrintActivityProps): LiveActivityLayout {
  'widget'

  const percent = Math.round(props.progress * 100)

  return {
    banner: (
      <VStack alignment="leading" spacing={6} modifiers={[padding({ all: 14 })]}>
        <HStack spacing={4}>
          <Text modifiers={[font({ size: 13, weight: 'semibold' }), lineLimit(1)]}>
            {props.printer}
          </Text>
          <Spacer />
          <Text modifiers={[font({ size: 12 }), foregroundColor('#8e8e93')]}>{props.state}</Text>
        </HStack>
        <Text modifiers={[font({ size: 12 }), foregroundColor('#8e8e93'), lineLimit(1)]}>
          {props.file}
        </Text>
        <ProgressView value={props.progress} />
        <HStack spacing={10}>
          <Text modifiers={[font({ size: 20, weight: 'bold', design: 'rounded' })]}>
            {`${percent}%`}
          </Text>
          <Spacer />
          <Text modifiers={[font({ size: 12, design: 'monospaced' }), foregroundColor('#8e8e93')]}>
            {props.nozzle === null ? 'N —' : `N ${Math.round(props.nozzle)}°`}
          </Text>
          <Text modifiers={[font({ size: 12, design: 'monospaced' }), foregroundColor('#8e8e93')]}>
            {props.bed === null ? 'B —' : `B ${Math.round(props.bed)}°`}
          </Text>
        </HStack>
      </VStack>
    ),

    compactLeading: (
      <Text modifiers={[font({ size: 13, weight: 'semibold' }), foregroundColor('#009789')]}>
        3D
      </Text>
    ),
    compactTrailing: (
      <Text modifiers={[font({ size: 13, weight: 'semibold' })]}>{`${percent}%`}</Text>
    ),
    minimal: (
      <Text modifiers={[font({ size: 12, weight: 'semibold' }), foregroundColor('#009789')]}>
        {`${percent}`}
      </Text>
    ),

    expandedLeading: (
      <VStack alignment="leading" spacing={2}>
        <Text modifiers={[font({ size: 12 }), foregroundColor('#8e8e93'), lineLimit(1)]}>
          {props.printer}
        </Text>
        <Text modifiers={[font({ size: 22, weight: 'bold', design: 'rounded' })]}>
          {`${percent}%`}
        </Text>
      </VStack>
    ),
    expandedTrailing: (
      <VStack alignment="trailing" spacing={2}>
        <Text modifiers={[font({ size: 12, design: 'monospaced' })]}>
          {props.nozzle === null ? 'N —' : `N ${Math.round(props.nozzle)}°`}
        </Text>
        <Text modifiers={[font({ size: 12, design: 'monospaced' })]}>
          {props.bed === null ? 'B —' : `B ${Math.round(props.bed)}°`}
        </Text>
      </VStack>
    ),
    expandedBottom: (
      <VStack alignment="leading" spacing={4}>
        <ProgressView value={props.progress} />
        <Text modifiers={[font({ size: 11 }), foregroundColor('#8e8e93'), lineLimit(1)]}>
          {props.file}
        </Text>
      </VStack>
    ),
  }
}
