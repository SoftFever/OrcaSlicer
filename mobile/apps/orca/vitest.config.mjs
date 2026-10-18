import { defineConfig } from 'vitest/config'

// Pure-logic tests only — no React Native, no native modules. Vitest picks
// up files matching `lib/**/*.test.ts` and runs them in a plain Node ESM
// context. Anything that needs the RN runtime lives in a device test.
export default defineConfig({
  test: {
    include: ['lib/**/*.test.ts'],
    environment: 'node',
    // The tests import `.ts` extensions explicitly to work with Node's
    // strip-types loader; Vitest handles both forms so no change needed.
  },
})
