// Expo config plugin: pin DEVELOPMENT_TEAM on the app target.
//
// `expo prebuild --clean` regenerates ios/ from scratch, so a team selected by
// hand in Xcode is gone on the next run. Expo has no ios.appleTeamId of its own
// for prebuild — EAS injects the team from stored credentials, which does
// nothing for a local build — so we write it into the Xcode project ourselves.
//
// The team id is not a secret (it ships inside every signed build), so keeping
// it in app.json is fine. APPLE_TEAM_ID overrides it, and with neither set the
// plugin is a no-op: CI builds unsigned, and a contributor without access to
// this team still gets a project that prebuilds.
const { withXcodeProject } = require('@expo/config-plugins')

module.exports = function withDevelopmentTeam(config, props) {
  const teamId = props?.appleTeamId ?? process.env.APPLE_TEAM_ID
  if (!teamId) return config

  return withXcodeProject(config, (mod) => {
    const configurations = mod.modResults.pbxXCBuildConfigurationSection()
    let applied = 0

    for (const key of Object.keys(configurations)) {
      const entry = configurations[key]
      // The section interleaves objects with `<uuid>_comment` string entries.
      if (typeof entry !== 'object' || entry === null) continue
      const settings = entry.buildSettings
      // Only the app target's configurations carry a bundle identifier; the
      // project-level ones do not, and neither does anything in Pods.
      if (!settings || settings.PRODUCT_BUNDLE_IDENTIFIER === undefined) continue

      settings.DEVELOPMENT_TEAM = teamId
      settings.CODE_SIGN_STYLE = 'Automatic'
      applied += 1
    }

    if (applied === 0) {
      throw new Error(
        'with-development-team: found no app target build configuration to set DEVELOPMENT_TEAM on',
      )
    }
    return mod
  })
}
