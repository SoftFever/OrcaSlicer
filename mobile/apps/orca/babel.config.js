// NativeWind v4 pairs a jsxImportSource with its own babel plugin. Both are
// needed: the jsxImportSource routes JSX creation through NativeWind so
// className is understood, and the plugin injects the className→style
// transform.
module.exports = function (api) {
  api.cache(true)
  return {
    presets: [['babel-preset-expo', { jsxImportSource: 'nativewind' }], 'nativewind/babel'],
  }
}
