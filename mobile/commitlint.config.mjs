// Commit messages follow Conventional Commits (https://www.conventionalcommits.org):
// release-please reads feat/fix/perf and breaking changes from them to pick the next
// mobile version and write the changelog. Checked by the commit-msg hook and in CI.
export default {
  extends: ['@commitlint/config-conventional'],
  rules: {
    // Bodies carry explanations, logs and co-author trailers; do not wrap them.
    'body-max-line-length': [0],
    'footer-max-line-length': [0],
  },
}
