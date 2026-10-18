// Metro + pnpm + monorepo. React Native's default resolver looks for a
// module by walking node_modules upward from the importing file, which works
// for a package's own transitive deps in pnpm's virtual store, but it does
// not cover the case where a package's build output (nativewind's, mostly)
// synthesizes `import 'react-native-css-interop/jsx-runtime'` from a file
// inside the app tree — those imports need `react-native-css-interop` to be
// reachable from the app, and pnpm's isolated store leaves it inside
// .pnpm/nativewind@…/node_modules/ where the app cannot see it.
//
// The workaround is a `resolveRequest` that maps a small allowlist of
// synthesized-import package names to their location inside pnpm's virtual
// store. Nothing else about the resolver is customised.
const { getDefaultConfig } = require('expo/metro-config')
const { withNativeWind } = require('nativewind/metro')
const fs = require('node:fs')
const path = require('node:path')

const projectRoot = __dirname
const workspaceRoot = path.resolve(projectRoot, '../..')

const config = getDefaultConfig(projectRoot)

config.watchFolders = [workspaceRoot]
config.resolver.nodeModulesPaths = [
  path.resolve(projectRoot, 'node_modules'),
  path.resolve(workspaceRoot, 'node_modules'),
]
config.resolver.disableHierarchicalLookup = false
config.resolver.unstable_enableSymlinks = true

// Cache pnpm store lookups: a scan across .pnpm/ is cheap once but not per
// resolution.
const pnpmDir = path.join(workspaceRoot, 'node_modules', '.pnpm')
const pnpmPackageCache = new Map()
function findInPnpmStore(name) {
  if (pnpmPackageCache.has(name)) return pnpmPackageCache.get(name)
  if (!fs.existsSync(pnpmDir)) return null
  // pnpm folder names are `<name>@<version>_<peer-hash>`; the scope form
  // uses `+` instead of `/`. Prefix-match on either style.
  const scoped = name.startsWith('@') ? name.replace('/', '+') : name
  const prefix = scoped + '@'
  const entry = fs.readdirSync(pnpmDir).find((d) => d.startsWith(prefix))
  const resolved = entry ? path.join(pnpmDir, entry, 'node_modules', name) : null
  pnpmPackageCache.set(name, resolved)
  return resolved
}

// Packages whose subpaths are imported from files inside the app tree
// (nativewind's className transform, expo-router's runtime entry, etc.) and
// are not direct deps of the app, so the standard walk misses them.
const shimmed = new Set(['react-native-css-interop', '@expo/metro-runtime'])

const upstreamResolveRequest = config.resolver.resolveRequest
config.resolver.resolveRequest = (context, moduleName, platform) => {
  const [head, ...rest] = moduleName.startsWith('@')
    ? [moduleName.split('/').slice(0, 2).join('/'), moduleName.split('/').slice(2).join('/')]
    : [moduleName.split('/')[0], moduleName.split('/').slice(1).join('/')]
  if (shimmed.has(head)) {
    const base = findInPnpmStore(head)
    if (base !== null && base !== undefined) {
      const target = rest.length > 0 && rest[0] !== '' ? path.join(base, ...rest) : base
      return context.resolveRequest(context, target, platform)
    }
  }
  return typeof upstreamResolveRequest === 'function'
    ? upstreamResolveRequest(context, moduleName, platform)
    : context.resolveRequest(context, moduleName, platform)
}

module.exports = withNativeWind(config, { input: './global.css' })
