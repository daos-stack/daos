#!/bin/bash

# Creates a repository based on the BUILD_URL environmnent variable.

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# at some point we want to use: shellcheck source=ci/rpm/distro_info.sh
# shellcheck disable=SC1091
source "$mydir/distro_info.sh"

: "{BUILD_URL:=}"
if command -v dnf; then
  if [ -n "${BUILD_URL}" ]; then
    repo_url="${BUILD_URL}artifact/artifacts/${PUBLIC_DISTRO}${MAJOR_VERSION}/"
    dnf --assumeyes config-manager --add-repo="$repo_url"
    repo="${repo_url#*://}"
    repo="${repo//%/}"
    repo="${repo//\//_}"
    # bug in EL7 DNF: this needs to be enabled before it can be disabled
    dnf config-manager --save --setopt="$repo".gpgcheck=1
    dnf config-manager --save --setopt="$repo".gpgcheck=0
    # but even that seems to be not enough, so just brute-force it
    if [ -d /etc/yum.repos.d ] &&
       ! grep gpgcheck /etc/yum.repos.d/"$repo".repo; then
        echo "gpgcheck=0" >> /etc/yum.repos.d/"$repo".repo
    fi
  fi
elif command -v apt-get; then
  echo "Ubuntu list files not yet implemented."
fi
