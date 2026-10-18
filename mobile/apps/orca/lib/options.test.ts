import { describe, expect, test } from 'vitest'

import type { OptionDefinition } from 'react-native-orca-core'

import {
  decodeScalar,
  editorKind,
  encodeScalar,
  escapeCStyle,
  groupByCategory,
  splitStrings,
  unescapeCStyle,
  visibleInMode,
} from './options.ts'

function def(partial: Partial<OptionDefinition>): OptionDefinition {
  return {
    key: 'k',
    kind: 'process',
    type: 'float',
    vector: false,
    nullable: false,
    readonly: false,
    label: 'Label',
    fullLabel: '',
    category: 'Quality',
    tooltip: '',
    unit: 'mm',
    mode: 0,
    ...partial,
  }
}

describe('options helpers', () => {
  test('C-style escapes round-trip', () => {
    const text = 'M104 S{temperature}\n; "quoted" \\ tab\t'
    expect(unescapeCStyle(escapeCStyle(text))).toBe(text)
    expect(escapeCStyle('a\nb')).toBe('"a\\nb"')
    expect(unescapeCStyle('plain')).toBe('plain')
  })

  test('string vectors split on unquoted semicolons only', () => {
    expect(splitStrings('"a;b";"c"')).toEqual(['"a;b"', '"c"'])
    expect(splitStrings('"x\\";y";"z"')).toEqual(['"x\\";y"', '"z"'])
    expect(splitStrings('')).toEqual([])
  })

  test('single-element vectors get scalar editors, longer ones stay raw', () => {
    const temps = def({ key: 'nozzle_temperature', type: 'int', vector: true })
    expect(editorKind(temps, '220')).toBe('number')
    expect(editorKind(temps, '220,215')).toBe('raw')
    expect(editorKind(def({ type: 'bool' }), '1')).toBe('bool')
    expect(editorKind(def({ type: 'enum' }), 'grid')).toBe('enum')
    expect(
      editorKind(def({ key: 'filament_start_gcode', type: 'string', vector: true }), '"M104"'),
    ).toBe('code')
  })

  test('scalar encode keeps the vector shape', () => {
    const temps = def({ key: 'nozzle_temperature', type: 'int', vector: true })
    expect(decodeScalar(temps, '220,215')).toBe('220')
    expect(encodeScalar(temps, '220,215', '230')).toBe('230,215')
    expect(encodeScalar(temps, '220', '230')).toBe('230')

    const gcode = def({ key: 'filament_start_gcode', type: 'string', vector: true })
    expect(decodeScalar(gcode, '"M104 S200\\nG28"')).toBe('M104 S200\nG28')
    expect(encodeScalar(gcode, '"old"', 'M104\n')).toBe('"M104\\n"')
  })

  test('mode filter and category grouping', () => {
    const simple = def({ key: 'a', mode: 0 })
    const expert = def({ key: 'b', mode: 2, category: 'Speed' })
    expect(visibleInMode(expert, 'simple')).toBe(false)
    expect(visibleInMode(expert, 'expert')).toBe(true)
    expect(visibleInMode(simple, 'simple')).toBe(true)
    const groups = groupByCategory([simple, expert, def({ key: 'c', category: '' })])
    expect(groups.map((g) => [g.category, g.options.map((o) => o.key)])).toEqual([
      ['Quality', ['a']],
      ['Speed', ['b']],
      ['Other', ['c']],
    ])
  })
})
