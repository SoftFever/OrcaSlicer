#!/usr/bin/env bash
# Adds a release to an AltStore source on the `altstore` branch.
#
# The branch holds only the sources and the icon they point at, so their raw URLs stay
# stable across releases:
#   https://raw.githubusercontent.com/<owner>/<repo>/altstore/source.json        Classic
#   https://raw.githubusercontent.com/<owner>/<repo>/altstore/source-beta.json   prereleases
#   https://raw.githubusercontent.com/<owner>/<repo>/altstore/source-pal.json    PAL
#
# The commit is made through the REST Git Data API rather than git push: GitHub signs
# commits it creates for the workflow's token, so the branch accepts them when signed
# commits are required. A local commit would be unsigned.
#
# Usage: publish-altstore.sh <source file name> <altstore-source.mjs arguments…>
# Runs from anywhere inside the repository checkout; needs GITHUB_REPOSITORY and a
# GH_TOKEN allowed to write contents.
set -euo pipefail

file=$1
shift
branch=altstore
repo_root=$(git rev-parse --show-toplevel)
api=repos/$GITHUB_REPOSITORY
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Fetches the branch's copy of a file into $work, or nothing if either is missing.
fetch() {
  gh api -H 'Accept: application/vnd.github.raw' "$api/contents/$1?ref=$head" > "$work/$1" 2>/dev/null \
    || rm -f "$work/$1"
}

blob() {
  jq -n --rawfile c <(base64 < "$1" | tr -d '\n') '{content: $c, encoding: "base64"}' \
    | gh api "$api/git/blobs" --input - -q .sha
}

# Two releases finishing together race on the branch; the ref update below refuses a
# non-fast-forward, and the whole edit is redone on the new head.
for _ in 1 2 3; do
  head=$(gh api "$api/git/ref/heads/$branch" -q .object.sha 2>/dev/null || true)
  files=("$file")
  if [ -n "$head" ]; then
    fetch "$file"
    fetch icon.png
  fi
  if [ ! -f "$work/icon.png" ]; then
    cp "$repo_root/mobile/apps/orca/assets/icon.png" "$work/icon.png"
    files+=(icon.png)
  fi
  before=$( [ -f "$work/$file" ] && shasum "$work/$file" || true)

  node "$repo_root/mobile/scripts/altstore-source.mjs" --source "$work/$file" \
    --icon-url "https://raw.githubusercontent.com/$GITHUB_REPOSITORY/$branch/icon.png" "$@"

  if [ "$before" = "$(shasum "$work/$file")" ]; then
    echo "$file unchanged"
    exit 0
  fi

  entries=$(for f in "${files[@]}"; do
    jq -n --arg p "$f" --arg s "$(blob "$work/$f")" '{path: $p, mode: "100644", type: "blob", sha: $s}'
  done | jq -s .)
  if [ -n "$head" ]; then
    base=$(gh api "$api/git/commits/$head" -q .tree.sha)
    tree=$(jq -n --arg b "$base" --argjson t "$entries" '{base_tree: $b, tree: $t}' \
      | gh api "$api/git/trees" --input - -q .sha)
    parents=$(jq -n --arg h "$head" '[$h]')
  else
    echo "No $branch branch yet; starting it"
    tree=$(jq -n --argjson t "$entries" '{tree: $t}' | gh api "$api/git/trees" --input - -q .sha)
    parents='[]'
  fi
  # No author or committer: GitHub fills in the token's identity and signs the commit.
  commit=$(jq -n --arg m "Update $file" --arg t "$tree" --argjson p "$parents" \
    '{message: $m, tree: $t, parents: $p}' | gh api "$api/git/commits" --input - -q .sha)

  if [ -n "$head" ]; then
    gh api -X PATCH "$api/git/refs/heads/$branch" -f sha="$commit" -F force=false >/dev/null && exit 0
  else
    gh api "$api/git/refs" -f ref="refs/heads/$branch" -f sha="$commit" >/dev/null && exit 0
  fi
  echo "$branch moved while publishing; retrying"
  rm -f "$work"/*
done
exit 1
