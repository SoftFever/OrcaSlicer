import { beforeEach, describe, expect, test, vi } from 'vitest'

// react-native-mmkv is a native module, so mock lib/storage before importing
// the module under test.
const mem = new Map<string, string>()
vi.mock('./storage', () => ({
  storage: {
    getString: (key: string) => mem.get(key),
    set: (key: string, value: string) => {
      mem.set(key, value)
    },
    remove: (key: string) => {
      mem.delete(key)
    },
  },
}))

import { clearHistory, history, pushHistory, removeHistory } from './history.ts'

const stubStats = {
  printTimeSeconds: 3600,
  filamentGrams: 12.3,
  filamentMm3: 4200,
  filamentCost: 1.5,
  layerCount: 100,
}

describe('history', () => {
  beforeEach(() => {
    mem.clear()
  })

  test('starts empty', () => {
    expect(history()).toEqual([])
  })

  test('pushHistory prepends and preserves order', () => {
    pushHistory({
      printer: 'A',
      filament: 'PLA',
      process: '0.2',
      objectNames: ['a'],
      sourcePaths: [],
      stats: stubStats,
      gcodePath: null,
    })
    pushHistory({
      printer: 'B',
      filament: 'PLA',
      process: '0.2',
      objectNames: ['b'],
      sourcePaths: [],
      stats: stubStats,
      gcodePath: null,
    })
    const all = history()
    expect(all).toHaveLength(2)
    expect(all[0]?.printer).toBe('B')
    expect(all[1]?.printer).toBe('A')
  })

  test('cap at 20 entries', () => {
    for (let i = 0; i < 25; i += 1) {
      pushHistory({
        printer: `P${i}`,
        filament: '',
        process: '',
        objectNames: [],
        sourcePaths: [],
        stats: stubStats,
        gcodePath: null,
      })
    }
    expect(history()).toHaveLength(20)
    expect(history()[0]?.printer).toBe('P24')
  })

  test('removeHistory drops the matching id', () => {
    const a = pushHistory({
      printer: 'A',
      filament: '',
      process: '',
      objectNames: [],
      sourcePaths: [],
      stats: stubStats,
      gcodePath: null,
    })
    pushHistory({
      printer: 'B',
      filament: '',
      process: '',
      objectNames: [],
      sourcePaths: [],
      stats: stubStats,
      gcodePath: null,
    })
    removeHistory(a.id)
    expect(history()).toHaveLength(1)
    expect(history()[0]?.printer).toBe('B')
  })

  test('clearHistory empties the list', () => {
    pushHistory({
      printer: 'A',
      filament: '',
      process: '',
      objectNames: [],
      sourcePaths: [],
      stats: stubStats,
      gcodePath: null,
    })
    clearHistory()
    expect(history()).toEqual([])
  })
})
