#!/usr/bin/env node
// Release notes for a mobile tag (mobile-vX.Y.Z[-pre.N]).
//
// The notes are the commits since the previous mobile tag that touched the app, the
// Nitro module, the façade or the iOS build, grouped by kind. A hand-written
// mobile/release-notes/<version>.md, when present, goes on top as the highlights.
//
// The previous tag of a stable release is the previous stable one, so 1.3.0 lists
// everything since 1.2.0 rather than since 1.3.0-beta.2. A prerelease counts from the
// newest tag of either kind.
//
// Usage: node release-notes.mjs <tag> [--ref <commit>] [--format md|text]
//   --ref     the commit the release is built from (default: the tag, or HEAD if the
//             tag does not exist yet)
//   --format  md for the GitHub release, text for AltStore's localizedDescription

import { execFileSync } from 'node:child_process'
import { existsSync, readFileSync } from 'node:fs'
import { join, dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const repo = resolve(here, '..', '..')
const PREFIX = 'mobile-v'
const PATHS = [
  'mobile',
  'src/mobile',
  'scripts/build_ios_core.sh',
  'deps/deps-ios.cmake',
  '.github/workflows/build_ios.yml',
  '.github/workflows/mobile_release.yml',
]

const args = process.argv.slice(2)
const tag = args.find((a) => !a.startsWith('--'))
const opt = (name, fallback) => {
  const i = args.indexOf(`--${name}`)
  return i >= 0 ? args[i + 1] : fallback
}
if (!tag?.startsWith(PREFIX)) {
  console.error(`usage: release-notes.mjs ${PREFIX}X.Y.Z [--ref <commit>] [--format md|text]`)
  process.exit(2)
}
const format = opt('format', 'md')
const version = tag.slice(PREFIX.length)
const isPrerelease = version.includes('-')

const git = (...a) => execFileSync('git', a, { cwd: repo, encoding: 'utf8' }).trim()
const tryGit = (...a) => {
  try {
    return git(...a)
  } catch {
    return ''
  }
}

const ref = opt('ref', tryGit('rev-parse', '-q', '--verify', `${tag}^{commit}`) || 'HEAD')

const previous = tryGit('tag', '--list', `${PREFIX}*`, '--sort=-v:refname', '--merged', ref)
  .split('\n')
  .filter((t) => t && t !== tag)
  .find((t) => isPrerelease || !t.slice(PREFIX.length).includes('-'))

// A first release has no tag to count from; the newest commits are a fair stand-in.
const range = previous ? [`${previous}..${ref}`] : ['-n', '60', ref]
const log = tryGit('log', '--no-merges', '--format=%h%x1f%s', ...range, '--', ...PATHS)

const GROUPS = [
  ['Features', /^(feat|add|adds|added|new|support|implement|introduce|ship)\b/i],
  ['Fixes', /^(fix|fixes|fixed|bugfix|hotfix|correct|repair|stop|prevent)\b/i],
  ['Performance', /^(perf|speed|faster|optimi[sz]e)\b/i],
  ['Maintenance', /^(chore|ci|build|docs?|test|tests|refactor|style|bump|deps?)\b/i],
]
const groups = new Map([...GROUPS.map(([g]) => [g, []]), ['Changes', []]])

for (const line of log ? log.split('\n') : []) {
  const [sha, subject] = line.split('\x1f')
  // Conventional commits: type(scope)!: summary. The type picks the group and is dropped.
  const conv = subject.match(/^(\w+)(\([^)]*\))?!?:\s*(.+)$/)
  const kind = conv ? conv[1] : subject
  const text = conv ? conv[3] : subject
  const group = GROUPS.find(([, re]) => re.test(kind))?.[0] ?? 'Changes'
  groups.get(group).push({ sha, text: text.charAt(0).toUpperCase() + text.slice(1) })
}

const highlightsFile = join(repo, 'mobile', 'release-notes', `${version}.md`)
const highlights = existsSync(highlightsFile) ? readFileSync(highlightsFile, 'utf8').trim() : ''
const server = process.env.GITHUB_SERVER_URL ?? 'https://github.com'
const slug = process.env.GITHUB_REPOSITORY
const out = []

if (format === 'text') {
  // AltStore shows localizedDescription as plain text in a small sheet: highlights and
  // the user-facing groups only, without hashes.
  if (highlights) out.push(highlights.replace(/^#+\s*/gm, ''), '')
  for (const [name, items] of groups) {
    if (!items.length || name === 'Maintenance') continue
    out.push(`${name}:`, ...items.map((i) => `• ${i.text}`), '')
  }
  if (!out.length) out.push('Maintenance release.')
} else {
  if (highlights) out.push(highlights, '')
  for (const [name, items] of groups) {
    if (!items.length) continue
    const link = (sha) => (slug ? `[${sha}](${server}/${slug}/commit/${sha})` : sha)
    out.push(`### ${name}`, '', ...items.map((i) => `- ${i.text} (${link(i.sha)})`), '')
  }
  if (!log) out.push('No changes to the mobile app since the previous release.', '')
  if (slug && previous) out.push(`**Full changelog**: ${server}/${slug}/compare/${previous}...${tag}`)
}

process.stdout.write(out.join('\n').trim() + '\n')
