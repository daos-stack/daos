#!/bin/bash

set -eux

# shellcheck disable=SC1091
. ci/gha_functions.sh

repo_path=$(get_repo_path)
mkdir -p "$repo_path$TARGET"
cp -a mock_result/*.rpm "$repo_path$TARGET"
cd "$repo_path$TARGET"
createrepo .
