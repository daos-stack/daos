#!/bin/bash

set -e
set -o pipefail

VERSION=0.1
CWD="$(realpath $(dirname $0))"
SRC_DIR="$(realpath "$CWD/../../ftest")"

cd "$SRC_DIR"
ansible-lint --parseable-severity -c "$CWD/daos-ansible-lint.yml"
