#!/bin/sh
# Download private release assets; verify before replacing local firmware.
set -eu
cd "$(dirname "$0")/.."
release_tag=${1:-v0.1.0}
command -v gh >/dev/null || { echo 'Install GitHub CLI and sign in with access to Muness/PearlPod.' >&2; exit 1; }
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir "$staging/dist"
gh release download "$release_tag" --repo Muness/PearlPod --dir "$staging/dist" --pattern '*.bin' --pattern SHA256SUMS --pattern flasher_args.json
(cd "$staging" && shasum -a 256 -c dist/SHA256SUMS)
mkdir -p dist
cp "$staging"/dist/* dist/
echo "Verified firmware downloaded from Muness/PearlPod release $release_tag."
