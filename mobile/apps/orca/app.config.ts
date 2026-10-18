import type { ConfigContext, ExpoConfig } from 'expo/config'

// The app's Expo config. app.json holds only the version, which release-please bumps;
// everything else lives here so one source builds every variant, picked by APP_VARIANT:
//
//   development  local builds (the default): the stable identity, so a device keeps its
//                installed app and data
//   beta         weekly tester builds: their own bundle id, app group, display name and
//                URL scheme, so they install next to the stable app
//   production   releases
//
// The release workflow also sets APP_VERSION (a beta builds the version still to be
// released, ahead of app.json), APP_BUILD_NUMBER (CFBundleVersion) and
// APP_MARKETING_VERSION, the full version with its prerelease label (0.3.0-beta.2);
// CFBundleShortVersionString only takes the numeric part. Both reach the app through
// `extra`, read by lib/appInfo.ts. Xcode evaluates this file again during the build to
// embed the manifest, so the variables must be set for xcodebuild as well as prebuild.

type Variant = 'development' | 'beta' | 'production'

const variant = (process.env.APP_VARIANT ?? 'development') as Variant
if (!['development', 'beta', 'production'].includes(variant)) {
  throw new Error(`APP_VARIANT must be development, beta or production, not '${variant}'`)
}
const beta = variant === 'beta'

const BUNDLE_ID = 'io.github.angelohub.orcaslicer'
const bundleIdentifier = beta ? `${BUNDLE_ID}.beta` : BUNDLE_ID

// Desktop OrcaSlicer's preset exports: a zip of preset JSON files under a
// slicer-specific extension. Same identifiers in every variant.
const PRESET_TYPES = (
  [
    ['orca_printer', 'orca-printer', 'OrcaSlicer printer bundle'],
    ['orca_filament', 'orca-filament', 'OrcaSlicer filament bundle'],
    ['orca_bundle', 'orca-bundle', 'OrcaSlicer preset bundle'],
  ] as const
).map(([extension, id, description]) => ({
  UTTypeIdentifier: `${BUNDLE_ID}.${id}`,
  UTTypeDescription: description,
  UTTypeConformsTo: ['public.data', 'public.zip-archive'],
  UTTypeTagSpecification: {
    'public.filename-extension': [extension],
    'public.mime-type': ['application/zip'],
  },
}))

export default ({ config }: ConfigContext): ExpoConfig => {
  const version = process.env.APP_VERSION ?? config.version ?? '0.0.0'
  return {
    ...config,
    // `name` also names the generated Xcode project and scheme; the variant shows in
    // CFBundleDisplayName instead.
    name: 'OrcaSlicer',
    slug: 'orcaslicer',
    version,
    icon: './assets/icon.png',
    scheme: beta ? 'orcaslicer-beta' : 'orcaslicer',
    orientation: 'default',
    ios: {
      bundleIdentifier,
      buildNumber: process.env.APP_BUILD_NUMBER ?? '1',
      supportsTablet: true,
      // Slicing a large model can exceed the default per-process memory ceiling;
      // these raise it on devices that honour them. Declared here so prebuild's
      // regenerated ios/ keeps them.
      entitlements: {
        'com.apple.developer.kernel.increased-memory-limit': true,
        'com.apple.developer.kernel.extended-virtual-addressing': true,
      },
      infoPlist: {
        CFBundleDisplayName: beta ? 'Orca Beta' : 'Orca Mobile',
        UIFileSharingEnabled: true,
        LSSupportsOpeningDocumentsInPlace: true,
        UIBackgroundModes: ['processing', 'fetch'],
        BGTaskSchedulerPermittedIdentifiers: ['orca-downloads-continue'],
        // Type identifiers stay the same in every variant: they name file formats,
        // not the app, and both apps can open the same files.
        CFBundleDocumentTypes: [
          {
            CFBundleTypeName: '3MF Project',
            LSHandlerRank: 'Owner',
            LSItemContentTypes: [`${BUNDLE_ID}.3mf`],
          },
          {
            // Desktop OrcaSlicer's preset exports (File > Export > Export Preset Bundle).
            CFBundleTypeName: 'OrcaSlicer preset bundle',
            LSHandlerRank: 'Owner',
            LSItemContentTypes: [
              `${BUNDLE_ID}.orca-printer`,
              `${BUNDLE_ID}.orca-filament`,
              `${BUNDLE_ID}.orca-bundle`,
            ],
          },
          {
            CFBundleTypeName: 'Preset JSON',
            LSHandlerRank: 'Alternate',
            LSItemContentTypes: ['public.json'],
          },
          {
            CFBundleTypeName: '3D model',
            LSHandlerRank: 'Alternate',
            LSItemContentTypes: [
              'public.standard-tesselated-geometry-format',
              'public.geometry-definition-format',
              `${BUNDLE_ID}.stl`,
              `${BUNDLE_ID}.obj`,
              `${BUNDLE_ID}.step`,
            ],
          },
        ],
        // Our own formats, so the stable app exports them. The beta installs next to
        // it and must not claim ownership of the same identifiers, so it imports them.
        ...(beta ? {} : { UTExportedTypeDeclarations: PRESET_TYPES }),
        UTImportedTypeDeclarations: [
          ...(beta ? PRESET_TYPES : []),
          {
            UTTypeIdentifier: `${BUNDLE_ID}.3mf`,
            UTTypeDescription: '3D Manufacturing Format',
            UTTypeConformsTo: ['public.data', 'public.item', 'public.zip-archive'],
            UTTypeTagSpecification: {
              'public.filename-extension': ['3mf'],
              'public.mime-type': [
                'model/3mf',
                'application/vnd.ms-package.3dmanufacturing-3dmodel+xml',
              ],
            },
          },
          {
            UTTypeIdentifier: `${BUNDLE_ID}.stl`,
            UTTypeDescription: 'Stereolithography Mesh',
            UTTypeConformsTo: ['public.data', 'public.3d-content'],
            UTTypeTagSpecification: {
              'public.filename-extension': ['stl'],
              'public.mime-type': ['model/stl', 'application/sla'],
            },
          },
          {
            UTTypeIdentifier: `${BUNDLE_ID}.obj`,
            UTTypeDescription: 'Wavefront OBJ Mesh',
            UTTypeConformsTo: ['public.data', 'public.3d-content'],
            UTTypeTagSpecification: {
              'public.filename-extension': ['obj'],
              'public.mime-type': ['model/obj'],
            },
          },
          {
            UTTypeIdentifier: `${BUNDLE_ID}.step`,
            UTTypeDescription: 'STEP CAD Model',
            UTTypeConformsTo: ['public.data', 'public.3d-content'],
            UTTypeTagSpecification: {
              'public.filename-extension': ['step', 'stp'],
              'public.mime-type': ['model/step', 'application/step'],
            },
          },
        ],
      },
    },
    android: {
      package: bundleIdentifier,
      versionCode: Number(process.env.APP_BUILD_NUMBER ?? 1),
      adaptiveIcon: {
        foregroundImage: './assets/adaptive-icon.png',
        backgroundColor: '#e9e9e9',
      },
      intentFilters: [
        {
          action: 'VIEW',
          category: ['DEFAULT', 'BROWSABLE'],
          data: [
            { mimeType: 'model/3mf' },
            { mimeType: 'application/vnd.ms-package.3dmanufacturing-3dmodel+xml' },
            { scheme: 'file', host: '*', pathPattern: '.*\\.3mf' },
            { scheme: 'content', host: '*', mimeType: '*/*' },
          ],
        },
        {
          action: 'VIEW',
          category: ['DEFAULT', 'BROWSABLE'],
          data: [
            { scheme: 'file', host: '*', pathPattern: '.*\\.orca_printer' },
            { scheme: 'file', host: '*', pathPattern: '.*\\.orca_filament' },
            { scheme: 'file', host: '*', pathPattern: '.*\\.orca_bundle' },
          ],
        },
        {
          action: 'VIEW',
          category: ['DEFAULT', 'BROWSABLE'],
          data: [
            { mimeType: 'model/stl' },
            { mimeType: 'application/sla' },
            { scheme: 'file', host: '*', pathPattern: '.*\\.stl' },
          ],
        },
      ],
    },
    plugins: [
      'expo-router',
      './plugins/with-no-push-entitlement',
      'expo-notifications',
      'expo-background-task',
      './plugins/with-swiftui-core-weak-link',
      ['./plugins/with-development-team', { appleTeamId: 'H69V293M7J' }],
      [
        'expo-widgets',
        {
          frequentUpdates: true,
          widgets: [
            {
              name: 'PrinterStatus',
              displayName: 'Printer status',
              description: 'Print progress and nozzle / bed temperatures for a printer.',
              supportedFamilies: ['systemSmall', 'systemMedium', 'accessoryRectangular'],
              contentMarginsDisabled: false,
            },
          ],
        },
      ],
      [
        'react-native-bootsplash',
        {
          logo: './assets/bootsplash_logo.svg',
          background: '#e9e9e9',
          logoWidth: 120,
          android: { darkContentBarsStyle: true },
        },
      ],
    ],
    experiments: {
      typedRoutes: true,
    },
    extra: {
      ...config.extra,
      variant,
      marketingVersion: process.env.APP_MARKETING_VERSION ?? version,
    },
  }
}
