#!/bin/bash

set -eu -o pipefail

if [[ $EUID -ne 0 ]]; then
  	echo "[ERROR] This script must be run as root." >&2
  	exit 1
fi

if [[ $# -lt 1 ]]; then
  	echo "Usage: $0 <debs_dir> [repo_dir]" >&2
  	exit 1
fi

DEBS_DIR="$(realpath "$1")"
REPO_DIR="${2:-/srv/daos/repos}"

for cmd in dpkg-scanpackages gzip apt-get; do
  	command -v $cmd >/dev/null 2>&1 || { echo "[ERROR] $cmd not found."; exit 1; }
done

echo
echo "[INFO] Installing required packages"
apt-get update -qq
apt-get install -y dpkg-dev

echo
echo "[INFO] Create repo directory $REPO_DIR"
mkdir -pv "$REPO_DIR"

echo
echo "[INFO] Populate new local repo"
find "$DEBS_DIR" -type f -name '*.deb' -exec install -v -m 644 {} "$REPO_DIR" \;

echo
echo "[INFO] Create repo metadata"
pushd "$REPO_DIR" >/dev/null
dpkg-scanpackages . /dev/null | gzip -9c > Packages.gz

LIST_FILE="/etc/apt/sources.list.d/daos.list"
if [[ ! -f $LIST_FILE ]]; then
  	echo "deb [trusted=yes] file:$REPO_DIR ./" > "$LIST_FILE"
  	echo "[INFO] Added local repo to $LIST_FILE"
else
  	echo "[INFO] $LIST_FILE already exists, not overwriting."
fi
echo
echo "[INFO] Update package manager metadata"
apt-get update
