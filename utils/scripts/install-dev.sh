#!/bin/bash -ue

# This script sets up a new development node with all dependencies needed to
# build DAOS from source.

# Requirements:
# - Run as a non-root user with sudo access.

if [ "$(whoami)" == "root" ]; then
	echo "Run this script as the non-root user you will use to build DAOS"
	exit 1
fi

distro=$1
case "$distro" in
	centos7 | el8 | leap15 | ubuntu)
		echo "Setting up node for $distro...";;

	*)
		echo "Invalid distro option: \"$distro\""
		echo "syntax: $0 <el8|leap15|ubuntu|centos7>"
		exit 1
esac

# Install dependencies from the OS repos.
SCRIPT_DIR="$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"
sudo "$SCRIPT_DIR/install-$distro.sh"
# Install user dependencies
python3 -m pip install -r "$SCRIPT_DIR/../../requirements.txt"
