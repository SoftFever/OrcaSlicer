import React, { useEffect, useRef, useState } from 'react'
import { Pressable, Text, View } from 'react-native'

import type { HeatedTool, Sensor } from '@/lib/printers'

// Mainsail-parity temperature panel: one row per sensor (Extruder, Heater Bed,
// Chamber, Toolhead sensors, MCU thermistors) with a rolling sparkline drawn from
// pure Views — no react-native-svg dependency to keep the pod graph frozen.
//
// The parent hands a fresh `sensors` list on every status refetch; we append into
// a per-host, per-sensor ring buffer keyed by name so the sparkline reflects the
// last minute or so of readings without re-mounting when the row order changes.

const HISTORY_SIZE = 60
type Series = number[]

function trimmedName(name: string): string { return name.length > 22 ? name.slice(0, 21) + '…' : name }

function Sparkline({ values, target }: { values: Series; target?: number }): React.JSX.Element {
  // Compute the shared min/max across the visible series and the target line so a
  // rising ramp does not squish an idle heater into a flat baseline.
  if (values.length < 2) {
    return <View className="h-8 w-full rounded bg-neutral-200 dark:bg-neutral-800" />
  }
  const withTarget = target !== undefined ? [...values, target] : values
  const min = Math.min(...withTarget)
  const max = Math.max(...withTarget)
  const span = Math.max(1, max - min)
  return (
    <View className="h-8 w-full flex-row items-end overflow-hidden rounded bg-neutral-100 dark:bg-neutral-800">
      {values.map((v, i) => {
        const h = Math.max(2, Math.round(((v - min) / span) * 30) + 2)
        return (
          <View
            key={i}
            style={{ height: h, width: `${100 / values.length}%` }}
            className="bg-orange-400 dark:bg-orange-500"
          />
        )
      })}
      {target !== undefined && target > 0 ? (
        <View
          className="absolute inset-x-0 h-px bg-red-500/70"
          style={{ bottom: Math.round(((target - min) / span) * 30) + 2 }}
        />
      ) : null}
    </View>
  )
}

function StateLabel({ sensor }: { sensor: Sensor }): React.JSX.Element {
  const isFan = sensor.kind === 'fan'
  const label = isFan && sensor.fanSpeed !== undefined
    ? `${Math.round(sensor.fanSpeed * 100)}%`
    : sensor.target !== undefined && sensor.target > 0
      ? 'heating'
      : sensor.kind === 'nozzle' || sensor.kind === 'bed' || sensor.kind === 'chamber'
        ? 'off'
        : ''
  if (label === '') return <View />
  return <Text className="w-16 text-right text-label text-neutral-500 dark:text-neutral-400">{label}</Text>
}

export interface TemperaturePanelProps {
  hostId: string
  sensors: Sensor[]
  /** Called with a heater kind + sensor name so the sheet can prompt for a target. */
  onSetTarget?: (tool: HeatedTool, sensor: Sensor) => void
}

/**
 * Renders the sensor list plus per-heater sparklines. History is kept in a ref so
 * component re-renders (theme changes, target edits) do not reset the plotted trace.
 */
export function TemperaturePanel({ hostId, sensors, onSetTarget }: TemperaturePanelProps): React.JSX.Element {
  // hostId keys separate hosts' histories: switching between two printers must not
  // stripe one printer's readings into the other's chart.
  const history = useRef<Record<string, Series>>({})
  const [tick, setTick] = useState(0)

  useEffect(() => {
    const key = (s: Sensor): string => `${hostId}:${s.name}`
    for (const s of sensors) {
      const k = key(s)
      const series = history.current[k] ?? []
      series.push(s.current)
      if (series.length > HISTORY_SIZE) series.splice(0, series.length - HISTORY_SIZE)
      history.current[k] = series
    }
    setTick((t) => t + 1)
  }, [hostId, sensors])

  if (sensors.length === 0) {
    return (
      <Text className="text-footnote text-neutral-500 dark:text-neutral-400">
        No temperatures reported by this host.
      </Text>
    )
  }

  const heaterKinds = new Set<Sensor['kind']>(['nozzle', 'bed', 'chamber'])

  return (
    <View className="gap-2">
      {sensors.map((s) => {
        const key = `${hostId}:${s.name}`
        const series = history.current[key] ?? []
        const isHeater = heaterKinds.has(s.kind)
        const tool: HeatedTool | null = s.kind === 'nozzle' ? 'nozzle' : s.kind === 'bed' ? 'bed' : null
        return (
          <View key={s.name} className="gap-1">
            <View className="flex-row items-center gap-3">
              <View className="flex-1">
                <Text className="text-callout font-medium text-neutral-900 dark:text-neutral-100" numberOfLines={1}>
                  {trimmedName(s.name)}
                </Text>
              </View>
              <StateLabel sensor={s} />
              <Text className="w-16 text-right text-callout font-semibold text-neutral-900 dark:text-neutral-100">
                {typeof s.current === 'number' && Number.isFinite(s.current) ? `${s.current.toFixed(1)}°` : '—'}
              </Text>
              {isHeater ? (
                <Pressable
                  onPress={() => { if (tool !== null && onSetTarget !== undefined) onSetTarget(tool, s) }}
                  disabled={tool === null || onSetTarget === undefined}
                  className="w-16 rounded-md bg-neutral-100 px-2 py-1 active:opacity-70 dark:bg-neutral-800"
                >
                  <Text className="text-center text-footnote text-neutral-700 dark:text-neutral-300">
                    {s.target !== undefined && s.target > 0 ? `${Math.round(s.target)}°` : 'off'}
                  </Text>
                </Pressable>
              ) : (
                <View className="w-16" />
              )}
            </View>
            <Sparkline values={series} target={s.target} />
          </View>
        )
      })}
      {/* Re-render trigger from ring-buffer state; kept off the DOM. */}
      <View style={{ height: 0 }} accessibilityElementsHidden testID={`tick-${tick}`} />
    </View>
  )
}
