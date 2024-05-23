#!/bin/bash

# Creates a repository based on the BUILD_URL environmnent variable.

set -uex

if [[ $REPOS != *daos@PR-* ]]; then
    mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
    # shellcheck source=utils/scripts/helpers/distro_info.sh
    source "$mydir/distro_info.sh"

    : "{BUILD_URL:=}"
    if command -v dnf; then
      if [ -n "${BUILD_URL}" ]; then
        repo_url="${BUILD_URL}artifact/artifacts/${PUBLIC_DISTRO}${MAJOR_VERSION}/"
        dnf --assumeyes config-manager --add-repo="$repo_url"
        repo="${repo_url#*://}"
        repo="${repo//%/}"
        repo="${repo//\//_}"
        dnf config-manager --save --setopt="$repo".gpgcheck=0
      fi
    elif command -v apt-get; then
      echo "Ubuntu list files not yet implemented."
    fi
  fi
