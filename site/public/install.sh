#!/bin/sh
# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# Installs one, the uione compiler, from its release on GitHub:
#
#   curl -fsSL https://uione.io/install.sh | sh
#
# It picks the build for this machine, checks it against the release's SHA256SUMS,
# and puts it in ~/.local/bin, and the libraries the desktop and terminal apps it
# writes are built on in ~/.local/share/uione/<version>. Nothing else is changed: no
# profile is edited, and nothing runs as root.
#
#   UIONE_VERSION=0.10.1   a particular release, rather than the latest
#   UIONE_INSTALL=dir     where to put one, rather than ~/.local/bin
#   UIONE_LIBRARIES=dir   where to put the libraries, rather than ~/.local/share/uione
#   UIONE_DOWNLOAD=url    where the release's files are, for trying a release first

set -eu

fail() {
	echo "uione: $*" >&2
	exit 1
}

case "$(uname -s)" in
Linux) system=linux ;;
Darwin) system=macos ;;
*) fail "there's no build of one for $(uname -s) yet; see https://uione.io/install/source to build it" ;;
esac
case "$(uname -m)" in
x86_64 | amd64) machine=x86_64 ;;
aarch64 | arm64) machine=$([ "$system" = macos ] && echo arm64 || echo aarch64) ;;
*) fail "there's no build of one for $(uname -m) yet; see https://uione.io/install/source to build it" ;;
esac
if [ "$system-$machine" = macos-x86_64 ]; then
	fail "one is built for Apple silicon only; see https://uione.io/install/source to build it on this Mac"
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

# Whether a file downloaded is the one the release's SHA256SUMS lists.
checks() {
	expected=$(awk -v name="$1" '$2 == name || $2 == "*" name { print $1 }' "$work/SHA256SUMS")
	[ -n "$expected" ] || fail "SHA256SUMS doesn't list $1"
	if command -v sha256sum >/dev/null; then
		actual=$(sha256sum "$work/$1" | awk '{ print $1 }')
	else
		actual=$(shasum -a 256 "$work/$1" | awk '{ print $1 }')
	fi
	[ "$actual" = "$expected" ] || fail "$1 doesn't match its checksum, so it wasn't installed"
}
checks "$archive"

tar -xzf "$work/$archive" -C "$work"
mkdir -p "$destination"
mv "$work/one" "$destination/one"
chmod 755 "$destination/one"

echo "Installed $("$destination/one" --version) in $destination"

# The libraries a desktop or terminal app one build writes is built on, beside the
# release's other versions. A release from before there were any has none.
version=$("$destination/one" --version | awk '{ print $2 }')
libraries=${UIONE_LIBRARIES:-$HOME/.local/share/uione}/$version
if awk '$2 == "uione-libraries.tar.gz" || $2 == "*uione-libraries.tar.gz" { found = 1 } END { exit !found }' "$work/SHA256SUMS"; then
	curl -fsSL "$release/uione-libraries.tar.gz" -o "$work/uione-libraries.tar.gz" || fail "couldn't download $release/uione-libraries.tar.gz"
	checks uione-libraries.tar.gz
	rm -rf "$libraries"
	mkdir -p "$libraries"
	tar -xzf "$work/uione-libraries.tar.gz" -C "$libraries"
	echo "Installed the libraries desktop and terminal apps are built on in $libraries"
fi
case ":$PATH:" in
*":$destination:"*) ;;
*) echo "Add it to your PATH to run it as one:  export PATH=\"$destination:\$PATH\"" ;;
esac
