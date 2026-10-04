#!/bin/sh
# Download private release assets; verify before replacing local firmware.
set -eu
cd "$(dirname "$0")/.."
release_tag=${1:-v0.3.1}
command -v gh >/dev/null || { echo 'Install GitHub CLI and sign in with access to open-horizon-labs/PearlPod.' >&2; exit 1; }
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir "$staging/dist"
gh release download "$release_tag" --repo open-horizon-labs/PearlPod --dir "$staging/dist" --pattern '*.bin' --pattern SHA256SUMS --pattern flasher_args.json
(cd "$staging" && shasum -a 256 -c dist/SHA256SUMS)
mkdir -p dist
cp "$staging"/dist/* dist/
echo "Verified firmware downloaded from open-horizon-labs/PearlPod release $release_tag."
