#!/bin/bash
set -uex

: "${REPO_FILE_URL:=}"

if [ -n "$REPO_FILE_URL" ]; then
    cd /etc/dnf/repos.d/ &&                 \
    mv daos_ci-leap15-artifactory.repo{,.tmp}
    for file in *.repo; do
        true > "$file"
    done
    mv daos_ci-leap15-artifactory.repo{.tmp,}
fi
