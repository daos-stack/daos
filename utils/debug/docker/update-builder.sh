#!/bin/bash

set -u -e -o pipefail

CWD="$(realpath "${0%}")"
CWD="${CWD%/*}"

for dist in el.8 el.9 leap.15 ubuntu ; do
	rsync --verbose "$CWD/../../docker/Dockerfile.$dist" $CWD/daos-builder/$(tr -d . <<< $dist)/Dockerfile
done
