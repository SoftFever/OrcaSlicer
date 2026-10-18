// Expo config plugin: drop the aps-environment entitlement.
//
// expo-notifications declares aps-environment because it also covers remote
// push, but this app only ever calls scheduleNotificationAsync — local
// notifications, which need no entitlement at all.
//
// Leaving it in costs us on every signing path that is not a paid team with
// automatic signing: a free Apple ID cannot provision push, so AltStore and
// SideStore fail to re-sign the app, and manual ad-hoc profiles fail unless
// the capability is enabled on the App ID for a feature we do not ship.
//
// Must be listed BEFORE expo-notifications in the plugins array: Expo composes
// mods so the last-registered one runs first, so listing it earlier is what
// makes it run after expo-notifications has written the key. If remote push is
// ever added, remove this plugin rather than working around it.
// `expo/config-plugins` is a thin re-export that lives inside the `expo` package's
// own resolvable dep graph, so it works under pnpm's isolated node_modules where a
// direct `require('@expo/config-plugins')` fails during Xcode's `[CP-User] Generate
// app.config for prebuilt Constants.manifest` device-build phase.
const { withEntitlementsPlist } = require('expo/config-plugins')

module.exports = function withNoPushEntitlement(config) {
  return withEntitlementsPlist(config, (mod) => {
    delete mod.modResults['aps-environment']
    return mod
  })
}
