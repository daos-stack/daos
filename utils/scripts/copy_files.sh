#!/bin/bash
srcdir="$1"; shift
destdir="$1"; shift

mkdir -p "${destdir}"
cp -R "${srcdir}/"* "${destdir}"
