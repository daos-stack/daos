#!/bin/bash

set -e
set -o pipefail

VERSION=0.1
CWD="$(realpath $(dirname $0))"
SRC_DIR="$(realpath "$CWD/../../ftest")"

yamllint --format colored --config-file "$CWD/daos-yamllint.yml" "$SRC_DIR"
