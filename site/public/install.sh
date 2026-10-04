#!/bin/sh
# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# Installs one, the uione compiler, from its release on GitHub:
#
#   curl -fsSL https://www.uione.io/install.sh | sh
#
# It picks the build for this machine, checks it against the release's SHA256SUMS,
# and puts it in ~/.local/bin. Nothing else is changed: no profile is edited, and
# nothing runs as root.
#
#   UIONE_VERSION=0.4.1   a particular release, rather than the latest
#   UIONE_INSTALL=dir     where to put one, rather than ~/.local/bin
#   UIONE_DOWNLOAD=url    where the release's files are, for trying a release first

set -eu

fail() {
	echo "uione: $*" >&2
	exit 1
}

case "$(uname -s)" in
Linux) system=linux ;;
Darwin) system=macos ;;
*) fail "there's no build of one for $(uname -s) yet; see https://www.uione.io/install/source to build it" ;;
esac
case "$(uname -m)" in
x86_64 | amd64) machine=x86_64 ;;
aarch64 | arm64) machine=$([ "$system" = macos ] && echo arm64 || echo aarch64) ;;
*) fail "there's no build of one for $(uname -m) yet; see https://www.uione.io/install/source to build it" ;;
esac
if [ "$system-$machine" = macos-x86_64 ]; then
	fail "one is built for Apple silicon only; see https://www.uione.io/install/source to build it on this Mac"
fi

if [ -n "${UIONE_VERSION:-}" ]; then
	release="https://github.com/da0x/uione/releases/download/v${UIONE_VERSION#v}"
else
	release="https://github.com/da0x/uione/releases/latest/download"
fi
release=${UIONE_DOWNLOAD:-$release}
archive="one-$system-$machine.tar.gz"
destination=${UIONE_INSTALL:-$HOME/.local/bin}

command -v curl >/dev/null || fail "curl is needed to download one"
command -v tar >/dev/null || fail "tar is needed to unpack one"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "Downloading $archive"
curl -fsSL "$release/$archive" -o "$work/$archive" || fail "couldn't download $release/$archive"
curl -fsSL "$release/SHA256SUMS" -o "$work/SHA256SUMS" || fail "couldn't download $release/SHA256SUMS"

expected=$(awk -v name="$archive" '$2 == name || $2 == "*" name { print $1 }' "$work/SHA256SUMS")
[ -n "$expected" ] || fail "SHA256SUMS doesn't list $archive"
if command -v sha256sum >/dev/null; then
	actual=$(sha256sum "$work/$archive" | awk '{ print $1 }')
else
	actual=$(shasum -a 256 "$work/$archive" | awk '{ print $1 }')
fi
[ "$actual" = "$expected" ] || fail "$archive doesn't match its checksum, so it wasn't installed"

tar -xzf "$work/$archive" -C "$work"
mkdir -p "$destination"
mv "$work/one" "$destination/one"
chmod 755 "$destination/one"

echo "Installed $("$destination/one" --version) in $destination"
case ":$PATH:" in
*":$destination:"*) ;;
*) echo "Add it to your PATH to run it as one:  export PATH=\"$destination:\$PATH\"" ;;
esac
