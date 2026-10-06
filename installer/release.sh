#!/bin/sh
# Publishes a new version: sets the version, commits, tags and pushes. The tag starts the build
# (see .github/workflows/build.yml), which makes the Windows and Mac installers and puts them on the
# repository's Releases page.
#
#   installer/release.sh 0.6.0
#   REMOTE=gitea installer/release.sh 0.6.0     # push somewhere other than "origin"
#
set -eu
cd "$(dirname "$0")/.."

die() { echo "release: $*" >&2; exit 1; }

new="${1:-}"
[ -n "$new" ] || die "usage: installer/release.sh <version>   e.g. 0.6.0"
echo "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die "version must look like 1.2.3, not \"$new\""

remote="${REMOTE:-origin}"
git remote get-url "$remote" >/dev/null 2>&1 || die "no git remote called \"$remote\". Add one first:  git remote add origin <url>"

[ -z "$(git status --porcelain --untracked-files=no)" ] || die "there are uncommitted changes. Commit or stash them first."
git rev-parse "v$new" >/dev/null 2>&1 && die "the tag v$new already exists"

current=$(perl -ne 'print $1 if /project\(VibeCheck VERSION (\d+\.\d+\.\d+)/' CMakeLists.txt)
[ -n "$current" ] || die "could not read the current version from CMakeLists.txt"
newest=$(printf '%s\n%s\n' "$current" "$new" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
[ "$newest" = "$new" ] && [ "$current" != "$new" ] || [ "$current" = "$new" ] || die "$new is not newer than the current version $current"

echo "Releasing $new (was $current), pushing to \"$remote\""

perl -pi -e "s/(project\(VibeCheck VERSION )\d+\.\d+\.\d+/\${1}$new/" CMakeLists.txt
git add CMakeLists.txt
# The pre-commit hook leaves a version that has been set by hand alone.
git commit -q -m "Release $new" --allow-empty
git tag -a "v$new" -m "VibeCheck $new"
git push "$remote" HEAD "v$new"

echo
echo "Pushed. The build takes about 15 minutes. When it finishes the installers are on the Releases page."
