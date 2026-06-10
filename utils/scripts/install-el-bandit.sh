#!/usr/bin/env bash
# Copyright 2026 Hewlett Packard Enterprise Development LP

# Install OS updates and packages as required for running python bandit.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

set -e

dnf_install_args="${1:-}"

: "${PYTHON_VERSION:=}"
: "${VIRTUAL_ENV:=}"

# shellcheck disable=SC2086
dnf --nodocs install ${dnf_install_args} \
    git \
    python${PYTHON_VERSION} \
    python${PYTHON_VERSION}-pip

# Setup a virtual environment if requested
if [ -n "$VIRTUAL_ENV" ]; then
    python"${PYTHON_VERSION}" -m venv "$VIRTUAL_ENV"
    # shellcheck disable=SC1091
    . "$VIRTUAL_ENV/bin/activate"
fi

# Install Python Bandit scanner
python3 -m pip --no-cache-dir install --upgrade pip
python3 -m pip --no-cache-dir install bandit
