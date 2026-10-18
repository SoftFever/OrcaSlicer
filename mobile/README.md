# OrcaSlicer mobile

The React Native app and the native module that wraps the slicing core. See
`docs/HLSD/mobile.md` in the repository root for the design.

```
packages/orca-core   react-native-orca-core: Nitro Module over src/mobile/core (C++)
apps/orca            the Expo app (expo-router)
```

## Prerequisites

- macOS with Xcode 16.4 or newer, CMake 3.20+, Ninja (`brew install ninja`)
- Node 22 and pnpm 10 (`corepack enable`)
- The slicing core built for iOS: `./scripts/build_ios_core.sh -a all -x` from the
  repository root, or the `OrcaCore.xcframework` artifact of the "Build iOS core" workflow

## First run

```sh
cp -R build/ios-xcframework/OrcaCore.xcframework mobile/packages/orca-core/ios/
cd mobile
pnpm install
pnpm specs                     # nitrogen: regenerates packages/orca-core/nitrogen/generated
pnpm typecheck
pnpm --filter orca-app test   # unit tests for the app's pure helpers (Node's test runner)
pnpm i18n                      # translation catalogs from localization/i18n (also run by start/ios/prebuild)
cd apps/orca
pnpm expo prebuild --platform ios
pnpm expo run:ios --device     # or open ios/OrcaSlicer.xcworkspace in Xcode
```

The xcframework is never committed; the podspec vendors it from
`packages/orca-core/ios/OrcaCore.xcframework`.

## Checks and hooks

```sh
pnpm lint            # oxlint (.oxlintrc.json)
pnpm format          # oxfmt, in place (.oxfmtrc.json); format:check only reports
pnpm typecheck
pnpm --filter orca-app test
pnpm hooks           # once per clone: installs the lefthook git hooks (lefthook.yml)
```

The hooks lint and format the staged files before a commit and check its message
against Conventional Commits (`commitlint.config.mjs`), which release-please needs.
Typecheck and tests are left to CI. Pull requests run every check in "Mobile app
checks", each as its own status check: require "Lint and format", "Typecheck", "Unit
tests" and "Commit messages" in branch protection.

## Sideloading

`expo run:ios --device` installs a development build with your Apple ID.

## Releases

Versions and releases are driven by [release-please](https://github.com/googleapis/release-please)
from [conventional commits](https://www.conventionalcommits.org) under `mobile/`:
`feat:` bumps the minor version, `fix:` and `perf:` the patch, `feat!:` or a
`BREAKING CHANGE:` footer the major (minor while the version is 0.x). Commits without a
type are left out of the version and the changelog.

The "Mobile release" workflow (`.github/workflows/mobile_release.yml`), on every push to
`main`:

1. keeps a release PR open that bumps `mobile/version.txt`, `app.json` and
   `package.json` and adds the section to `mobile/CHANGELOG.md`;
2. when that PR is merged, tags `mobile-vX.Y.Z` and creates the GitHub release, then in
   the same run builds an ad-hoc signed `OrcaMobile-X.Y.Z.ipa` against the newest
   `OrcaCore.xcframework` from "Build iOS core" (CFBundleVersion from the run number);
3. attaches the `.ipa`, its checksum and install notes to the release, and adds it to
   the AltStore source on the `altstore` branch.

The beta lane runs every Monday and publishes a beta only when something under
`mobile/` changed since the newest mobile tag. It is numbered after the version the open
release PR proposes (`mobile-v0.2.0-beta.3`), gets notes from the commits since the
previous tag (`scripts/release-notes.mjs`) and goes into `source-beta.json`. Run the
workflow by hand with `lane: beta` to cut one now, or `lane: test` (the default) for an
`.ipa` workflow artifact that publishes nothing.

### Variants

`app.config.ts` builds one of three variants, chosen by `APP_VARIANT`; `app.json` holds
only the version release-please bumps.

| `APP_VARIANT` | Used by | Bundle id | Name |
| --- | --- | --- | --- |
| `development` (default) | local builds | `io.github.angelohub.orcaslicer` | Orca Mobile |
| `beta` | beta lane, test builds | `io.github.angelohub.orcaslicer.beta` | Orca Beta |
| `production` | releases | `io.github.angelohub.orcaslicer` | Orca Mobile |

Beta has its own app group and URL scheme (`orcaslicer-beta`) too, so it installs next
to the stable app. On a free Apple ID that is four of AltStore's three app IDs (each
variant is the app plus its widget), so testers on free accounts keep one of the two.
The app reads its variant and full version (`0.2.0-beta.3`, which CFBundleShortVersionString
cannot hold) from `lib/appInfo.ts`. To build a beta locally:

```sh
APP_VARIANT=beta pnpm prebuild
APP_VARIANT=beta pnpm ios
```

The configuration is in `.github/release-please-config.json` and the current version in
`.github/.release-please-manifest.json`. To force a version, add `Release-As: 1.0.0` to
a commit message's footer.

AltStore and SideStore users add one of these sources:

```
https://raw.githubusercontent.com/<owner>/<repo>/altstore/source.json        stable
https://raw.githubusercontent.com/<owner>/<repo>/altstore/source-beta.json   prereleases
https://raw.githubusercontent.com/<owner>/<repo>/altstore/source-pal.json    AltStore PAL
```

The pipeline only appends versions and refreshes permissions, so the source's name,
description, screenshots and news can be edited by hand on the `altstore` branch.

AltStore PAL installs only builds Apple notarized for alternative distribution, which
needs a team-signed upload to App Store Connect. Once its ADP is hosted, the "Mobile
AltStore PAL" workflow adds it to `source-pal.json`; the steps are in its header.

The `.ipa` builds on GitHub's `macos-15` runner unless the `MOBILE_MACOS_RUNNER`
repository variable names another. `scripts/setup-mac-runner.sh` registers a Mac you own
as a self-hosted runner with the `orca-ios` label (it needs Xcode, CocoaPods and `gh`):

```sh
mobile/scripts/setup-mac-runner.sh
gh variable set MOBILE_MACOS_RUNNER --body '["self-hosted","macOS","ARM64","orca-ios"]'
```

## Layout of the native module

- `src/specs/OrcaCore.nitro.ts` is the contract. It mirrors `src/mobile/core/Session.hpp`.
- `nitrogen/generated/` is generated by `pnpm specs` and committed, as Nitro recommends.
- `cpp/` implements the Hybrid Objects over the façade. Every call that can take longer
  than a frame returns a promise and runs on the session's worker thread.
- `NitroOrcaCore.podspec` links the xcframework and adds the generated sources.

## Façade tests on the desktop

The façade (`src/mobile/core`) and its suite (`tests/mobile`) build on macOS or Linux
against a normal desktop dependency prefix, which already contains everything the mobile
list needs:

```sh
cmake -S . -B build/mobile-host -G Ninja -DORCA_MOBILE=ON -DBUILD_TESTS=1 \
  -DCMAKE_PREFIX_PATH="$PWD/deps/build/arm64/OrcaSlicer_dep/usr/local"
cmake --build build/mobile-host --target mobile_tests
ctest --test-dir build/mobile-host/tests/mobile --output-on-failure
```

This is the quickest way to compile and exercise the façade before an iOS build.
