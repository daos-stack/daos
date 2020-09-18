#!/bin/bash

set -uex

rm -rf "_build.external${arch:-}"
mkdir -p coverity
rm -f coverity/*
if [ -e cov-int ]; then
  tar czf coverity/daos_coverity.tgz cov-int
fi
