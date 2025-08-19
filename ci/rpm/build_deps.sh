#!/bin/bash
cd /home/pre || exit 1
scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32
