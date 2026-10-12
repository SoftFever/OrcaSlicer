#!/usr/bin/env bash
# Lets a pull request's macOS deps build wait for a push build of the base
# branch that builds the same deps, from a Linux job. `check` runs after a cache
# miss and names a queued or running base build with the same deps/ and .github/
# to wait for, unless the pull request changes either. `wait` waits until that
# build saves KEY. actions/cache then confirms the entry can be restored.
#
# Environment: GH_TOKEN, REPO, WORKFLOW_REF and BASE_REF, plus KEY_INPUTS,
# WAIT_FOR, WAIT_SINCE, WAIT_MINUTES, KEY and ARCH for `wait`.
# Writes key-inputs, wait-for and wait-since to $GITHUB_OUTPUT.
set -uo pipefail

workflow=${WORKFLOW_REF%%@*}
workflow=${workflow##*/}

# Must match the job names in build_all.yml, build_check_cache.yml and build_deps.yml.
check_job_name="build_macos_arch (${ARCH:-}) / Check Cache"
deps_job_name="build_macos_arch (${ARCH:-}) / Build Deps / Build Deps"

out() { echo "$1" >> "${GITHUB_OUTPUT:-/dev/stdout}"; }

# The cache key depends on deps/ and on the workflows under .github/.
local_key_inputs() { echo "$(git rev-parse "$1:.github") $(git rev-parse "$1:deps")"; }
remote_key_inputs() {
  gh api "repos/$REPO/git/trees/$1" < /dev/null \
    --jq '[.tree[] | select(.path == ".github" or .path == "deps") | {key: .path, value: .sha}] | from_entries | "\(.[".github"]) \(.deps)"'
}

# Prints the id of a push build of the base branch that has not finished, is not
# one of the ids in $1, and has KEY_INPUTS.
matching_base_build() {
  local status runs id sha
  runs=$(for status in in_progress queued pending waiting requested; do
    gh api -X GET "repos/$REPO/actions/workflows/$workflow/runs" \
      -f branch="$BASE_REF" -f event=push -f status="$status" -f per_page=10 < /dev/null \
      | jq -r '.workflow_runs[] | "\(.id) \(.head_sha)"'
  done)
  while read -r id sha; do
    [ -n "$id" ] || continue
    [[ " ${1:-} " == *" $id "* ]] && continue
    if [ "$(remote_key_inputs "$sha")" = "$KEY_INPUTS" ]; then
      echo "$id"
      return
    fi
  done <<< "$runs"
}

check() {
  KEY_INPUTS=$(local_key_inputs HEAD)
  out "key-inputs=$KEY_INPUTS"
  if [ "$KEY_INPUTS" != "$(local_key_inputs HEAD^1)" ]; then
    echo "This pull request changes deps/ or .github/, so it builds its own deps."
    return
  fi
  local id
  id=$(matching_base_build)
  if [ -n "$id" ]; then
    echo "Build $id of $BASE_REF has the same deps/ and .github/. Waiting for its cache."
    out "wait-for=$id"
    out "wait-since=$(date +%s)"
  else
    echo "No build of $BASE_REF is building these deps."
  fi
}

key_saved() {
  gh cache list -R "$REPO" --ref "refs/heads/$BASE_REF" --key "$KEY" --json key < /dev/null \
    | jq -e --arg key "$KEY" 'any(.[]; .key == $key)' > /dev/null
}

wait_for() {
  # Counts the limit from the check, including time queued behind other waiting
  # pull requests.
  local id=$WAIT_FOR seen=$WAIT_FOR deadline=$((${WAIT_SINCE:-$(date +%s)} + WAIT_MINUTES * 60)) jobs job check warned=
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if key_saved; then
      echo "Found $KEY."
      return
    fi
    jobs=$(gh api -X GET "repos/$REPO/actions/runs/$id/jobs" -f filter=latest -f per_page=100 < /dev/null)
    job=$(jq -r --arg name "$deps_job_name" 'first(.jobs[] | select(.name == $name) | "\(.status) \(.conclusion)") // ""' <<< "$jobs")
    case "$job" in
      "completed success" | "completed skipped")
        # The new entry can take a moment to be listed, and a skipped job means
        # the build found the cache, possibly under the default branch.
        for _ in 1 2 3; do
          key_saved && { echo "Found $KEY."; return; }
          sleep 20
        done
        echo "Build $id of $BASE_REF finished its deps without saving $KEY."
        return ;;
      "completed cancelled") ;;
      completed*)
        echo "The deps job of build $id of $BASE_REF did not succeed."
        return ;;
      # The deps job exists only once the build's cache check has succeeded.
      "")
        case "$(gh api "repos/$REPO/actions/runs/$id" --jq '"\(.status) \(.conclusion)"' < /dev/null)" in
          "completed cancelled") ;;
          completed*)
            check=$(jq -r --arg name "$check_job_name" 'first(.jobs[] | select(.name == $name) | .conclusion) // ""' <<< "$jobs")
            if [ -z "$warned" ] && { [ -z "$check" ] || [ "$check" = success ]; }; then
              echo "::warning title=Deps wait::Build $id of $BASE_REF has no job named \"$deps_job_name\". If the build jobs were renamed, update scripts/ci_deps_cache.sh. This does not affect this pull request."
              warned=1
            fi ;;
          *)
            sleep 120
            continue ;;
        esac ;;
      *)
        sleep 120
        continue ;;
    esac
    # Follows another unfinished build with the same key inputs, such as the
    # push that replaced a cancelled one.
    id=$(matching_base_build "$seen")
    [ -n "$id" ] || break
    seen="$seen $id"
    echo "Following build $id of $BASE_REF."
  done
  echo "No build of $BASE_REF saved $KEY."
}

case "${1:-}" in
  check) check ;;
  wait) wait_for ;;
  *) echo "usage: $0 check|wait" >&2; exit 2 ;;
esac
