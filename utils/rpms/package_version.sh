#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/package_info.sh"

group=$1
if [[ "$2" = "debug" ]]; then
  addon="-debuginfo"
  pkgtype="lib"
else
  pkgtype="$2"
fi
if [ $# -eq 3 ]; then
  name="$3"
else
  name="$group"
fi
package="${name}_${pkgtype}"
full="${group}_full"
echo "${!package:-}${addon:-}-${!full}"
