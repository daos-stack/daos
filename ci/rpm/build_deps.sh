#!/bin/bash

cat /etc/pip.conf || true
cat /etc/gemrc || true
cat /etc/uv/uv.toml || true

env | grep -i proxy || true

scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32
