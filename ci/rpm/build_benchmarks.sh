#!/bin/bash
# Copyright 2025 Hewlett Packard Enterprise Development LP
cd /home/daos/pre || exit 1
module avail
module load mpich
scons benchmarks --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32
