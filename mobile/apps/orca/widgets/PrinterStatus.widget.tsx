import { HStack, ProgressView, Spacer, Text, VStack } from '@expo/ui/swift-ui'
import { font, foregroundColor, lineLimit, padding } from '@expo/ui/swift-ui/modifiers'
import React from 'react'

// Home screen / lock screen widget showing one printer.
//
// The `'widget'` directive makes babel-preset-expo lift this function into the
// separate widget bundle, where it runs in an isolated SwiftUI runtime. That
// runtime has no hooks, no async and no access to this module's other bindings,
// so everything the layout needs is either a prop or inlined here — do not
// factor helpers out of this function.

export type PrinterWidgetProps = {
  name: string
  /** Free text from the host: printing, paused, ready, error, offline. */
  state: string
  /** 0..1 while a job runs, null when idle. */
  progress: number | null
  file: string | null
  nozzle: number | null
  bed: number | null
}

export function PrinterStatusWidget(props: PrinterWidgetProps): React.JSX.Element {
  'widget'

  const printing = props.progress !== null
  const percent = props.progress === null ? 0 : Math.round(props.progress * 100)

  return (
    <VStack alignment="leading" spacing={6} modifiers={[padding({ all: 12 })]}>
      <HStack spacing={4}>
        <Text modifiers={[font({ size: 13, weight: 'semibold' }), lineLimit(1)]}>{props.name}</Text>
        <Spacer />
        <Text modifiers={[font({ size: 11 }), foregroundColor('#8e8e93'), lineLimit(1)]}>
          {props.state}
        </Text>
      </HStack>

      {printing ? (
        <VStack alignment="leading" spacing={4}>
          <Text modifiers={[font({ size: 26, weight: 'bold', design: 'rounded' })]}>
            {`${percent}%`}
          </Text>
          <ProgressView value={props.progress ?? 0} />
          <Text modifiers={[font({ size: 11 }), foregroundColor('#8e8e93'), lineLimit(1)]}>
            {props.file ?? ''}
          </Text>
        </VStack>
      ) : (
        <Text modifiers={[font({ size: 15 }), foregroundColor('#8e8e93')]}>Not printing</Text>
      )}

      <Spacer />

      <HStack spacing={10}>
        <Text modifiers={[font({ size: 12, design: 'monospaced' })]}>
          {props.nozzle === null ? 'N —' : `N ${Math.round(props.nozzle)}°`}
        </Text>
        <Text modifiers={[font({ size: 12, design: 'monospaced' })]}>
          {props.bed === null ? 'B —' : `B ${Math.round(props.bed)}°`}
        </Text>
      </HStack>
    </VStack>
  )
}
