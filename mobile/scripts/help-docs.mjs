#!/usr/bin/env node
// Writes the in-app guides (apps/orca/lib/help/content.ts) as markdown under
// docs/mobile/ and copies their screenshots next to them, so the repository and
// GitHub carry the same tutorial the app shows. Also refreshes the image map
// first (help-images.mjs). Run with `pnpm help-docs` from mobile/.
//
// content.ts is TypeScript; Node 22 strips the types when told to.

import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { execFileSync } from 'node:child_process'

const here = dirname(fileURLToPath(import.meta.url))
const repo = resolve(here, '..', '..')
const app = join(repo, 'mobile', 'apps', 'orca')
const assets = join(app, 'assets', 'help')
const docs = join(repo, 'docs', 'mobile')
const images = join(docs, 'images')

execFileSync(process.execPath, [join(here, 'help-images.mjs')], { stdio: 'inherit' })

const { HELP_TOPICS, imageKeys } = await import(
  pathToFileURL(join(app, 'lib', 'help', 'content.ts')).href
)

mkdirSync(images, { recursive: true })
// Replace the image folder wholesale so renamed or dropped shots do not linger.
for (const stale of existsSync(images) ? readdirSync(images) : [])
  rmSync(join(images, stale), { recursive: true, force: true })

const missing = []
for (const topic of HELP_TOPICS) {
  const lines = [`# ${topic.title}`, '', topic.summary, '']
  lines.push(
    '> This page is generated from the guide inside the Orca iOS app (`mobile/apps/orca/lib/help/content.ts`). Edit that file and run `pnpm help-docs` in `mobile/`.',
    '',
  )
  let n = 0
  for (const section of topic.sections) {
    lines.push(`## ${section.title}`, '')
    if (section.intro) lines.push(section.intro, '')
    for (const step of section.steps) {
      n += 1
      lines.push(`### ${n}. ${step.title}`, '', step.body, '')
      if (step.image) {
        const src = join(assets, `${step.image}.png`)
        if (existsSync(src)) {
          const dst = join(images, `${step.image}.png`)
          mkdirSync(dirname(dst), { recursive: true })
          copyFileSync(src, dst)
          lines.push(`![${step.alt ?? step.title}](images/${step.image}.png)`, '')
        } else {
          missing.push(step.image)
        }
      }
    }
  }
  writeFileSync(join(docs, `${topic.id}.md`), lines.join('\n'))
}

const index = [
  '# Orca Mobile guides',
  '',
  'User guides for the iOS port. Each page is generated from the guide shown inside the app.',
  '',
  ...HELP_TOPICS.map((t) => `- [${t.title}](${t.id}.md): ${t.summary}`),
  '',
]
writeFileSync(join(docs, 'README.md'), index.join('\n'))

const total = HELP_TOPICS.reduce((n, t) => n + imageKeys(t).length, 0)
console.log(
  `help-docs: ${HELP_TOPICS.length} page(s) -> docs/mobile, ${total - missing.length}/${total} screenshot(s) present`,
)
if (missing.length > 0) console.log('help-docs: not captured yet:\n  ' + missing.join('\n  '))
