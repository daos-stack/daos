#!/bin/bash
# No isolation for building SPDK dependency, since we want to use
# the system-installed (via python virtual environment) versions of
# uv and hatchling to avoid installation of them during SPDK build process.
export UV_NO_BUILD_ISOLATION=1
scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32
