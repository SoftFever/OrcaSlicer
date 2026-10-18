#!/usr/bin/env bash
# Registers this Mac as a self-hosted GitHub Actions runner for the mobile release.
#
# A GitHub-hosted macOS runner bills at 10x the Linux rate; a Mac you already own
# builds the .ipa for free and keeps the pnpm store and CocoaPods cache warm between
# releases. After this script, point the release at it:
#
#   gh variable set MOBILE_MACOS_RUNNER --body '["self-hosted","macOS","ARM64","orca-ios"]'
#
# and unset the variable to go back to GitHub's runners. The runner runs as a launchd
# service under the current user, so the Xcode it builds with is the one xcode-select
# points at.
#
# Usage: mobile/scripts/setup-mac-runner.sh [owner/repo] [runner directory]
# Needs gh logged in with admin rights on the repository.
set -euo pipefail

repo=${1:-$(gh repo view --json nameWithOwner -q .nameWithOwner)}
dir=${2:-$HOME/actions-runner/${repo//\//-}}
labels=orca-ios

for tool in xcodebuild pod gh curl; do
  command -v "$tool" >/dev/null || { echo "$tool is required" >&2; exit 1; }
done
xcodebuild -version | head -1

case $(uname -m) in
  arm64) arch=arm64 ;;
  x86_64) arch=x64 ;;
  *) echo "Unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "$dir"
cd "$dir"
if [ ! -x ./config.sh ]; then
  version=$(gh api repos/actions/runner/releases/latest -q .tag_name)
  version=${version#v}
  echo "Downloading actions/runner $version for osx-$arch into $dir"
  curl -fsSL -o runner.tar.gz \
    "https://github.com/actions/runner/releases/download/v$version/actions-runner-osx-$arch-$version.tar.gz"
  tar xzf runner.tar.gz
  rm runner.tar.gz
fi

if [ -f .runner ]; then
  echo "Already configured in $dir"
else
  token=$(gh api -X POST "repos/$repo/actions/runners/registration-token" -q .token)
  ./config.sh --unattended --replace \
    --url "https://github.com/$repo" \
    --token "$token" \
    --name "$(scutil --get LocalHostName)-orca-ios" \
    --labels "$labels"
fi

./svc.sh install || true
./svc.sh start
./svc.sh status
echo
echo "Runner up with labels self-hosted, macOS, $( [ "$arch" = arm64 ] && echo ARM64 || echo X64 ), $labels."
echo "Remove it with: cd $dir && ./svc.sh uninstall && ./config.sh remove --token \$(gh api -X POST repos/$repo/actions/runners/remove-token -q .token)"
