#!/usr/bin/env node
// Adds a release to an AltStore source (https://faq.altstore.io/developers/make-a-source).
//
// The source file is created on first use and edited in place after that, so the
// source-level fields (name, description, news, screenshots…) can be curated by hand
// on the altstore branch without the pipeline overwriting them. Per release it updates
// the app's permissions from the build and puts the new version first, replacing an
// earlier entry with the same marketing version.
//
// AltStore Classic installs the .ipa itself. AltStore PAL installs only notarized apps:
// pass --marketplace-id (the Apple ID from App Store Connect) and point --download-url
// at the ADP's manifest.json instead of an .ipa.
//
// Usage: node altstore-source.mjs --source source.json --meta app-meta.json
//          --version 1.2.0 --marketing 1.2.0-beta.1 --build 42 --size <bytes>
//          --download-url <url> --notes-file notes.txt --icon-url <url>
//          [--marketplace-id <id>] [--name "Source name"] [--date <ISO 8601>]
//
// app-meta.json is written by the release workflow's build job from the built .app:
//   { bundleIdentifier, name, minOSVersion, entitlements: [..], privacy: { key: text } }

import { existsSync, readFileSync, writeFileSync } from 'node:fs'

const args = process.argv.slice(2)
const opt = (name, fallback) => {
  const i = args.indexOf(`--${name}`)
  return i >= 0 ? args[i + 1] : fallback
}
const required = (name) => {
  const v = opt(name)
  if (!v) {
    console.error(`altstore-source.mjs: --${name} is required`)
    process.exit(2)
  }
  return v
}

const sourcePath = required('source')
const meta = JSON.parse(readFileSync(required('meta'), 'utf8'))
const version = required('version')
const marketing = opt('marketing', version)
const iconURL = required('icon-url')
const marketplaceID = opt('marketplace-id')

const source = existsSync(sourcePath)
  ? JSON.parse(readFileSync(sourcePath, 'utf8'))
  : {
      name: opt('name', 'OrcaSlicer Mobile'),
      identifier: `${meta.bundleIdentifier}.source`,
      subtitle: 'OrcaSlicer on iPhone and iPad',
      description: 'Builds of the OrcaSlicer mobile app, published from GitHub Actions.',
      iconURL,
      tintColor: '#009688',
      featuredApps: [meta.bundleIdentifier],
      apps: [],
      news: [],
    }

let app = source.apps.find((a) => a.bundleIdentifier === meta.bundleIdentifier)
if (!app) {
  app = {
    name: meta.name,
    bundleIdentifier: meta.bundleIdentifier,
    developerName: 'OrcaSlicer contributors',
    subtitle: 'Slice and send prints from your phone.',
    localizedDescription:
      'OrcaSlicer on iOS: import STL, 3MF, OBJ and STEP models, pick printer, filament and ' +
      'process presets from the OrcaSlicer profile library, slice on device and send the ' +
      'G-code to Moonraker or OctoPrint.',
    iconURL,
    tintColor: '#009688',
    category: 'utilities',
    screenshots: [],
    versions: [],
  }
  source.apps.push(app)
}
if (marketplaceID) app.marketplaceID = marketplaceID
app.appPermissions = { entitlements: meta.entitlements, privacy: meta.privacy }

const entry = {
  version,
  buildVersion: required('build'),
  marketingVersion: marketing,
  date: opt('date', new Date().toISOString()),
  localizedDescription: readFileSync(required('notes-file'), 'utf8').trim(),
  downloadURL: required('download-url'),
  size: Number(required('size')),
  minOSVersion: meta.minOSVersion,
}
// Newest first: AltStore takes versions[0] as the latest.
app.versions = [
  entry,
  ...app.versions.filter((v) => (v.marketingVersion ?? v.version) !== marketing),
]

writeFileSync(sourcePath, JSON.stringify(source, null, 2) + '\n')
console.log(
  `${sourcePath}: ${app.name} ${marketing} (${entry.buildVersion}), ${app.versions.length} version(s)`,
)
