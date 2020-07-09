#!/bin/bash

set -uex

rm -rf "_build.external${arch:-}"
mkdir -p coverity
rm -f coverity/*

if [ -f "config${arch:-}.log" ]; then
  mv "config${arch}.log" coverity/config.log-centos7-cov
fi
if [ -f cov-int/build-log.txt ]; then
  mv cov-int/build-log.txt coverity/cov-build-log.txt
fi
