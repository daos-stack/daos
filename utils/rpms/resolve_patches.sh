#!/bin/bash

DAOS_ROOT="$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")"
COMP="$1"
apply_patches()
{
  echo "Applying patches to ${COMP}"

  patch_dir="${DAOS_ROOT}/utils"
  source <(grep = <(grep -A10 '\[patch_versions\]' "${patch_dir}"/build.config))
  if [ -z "${!COMP}" ]; then
    echo "No patches for ${COMP}"
    return
  fi
  all_patches=${!COMP//,/ }

  pushd "${patch_dir}" || exit 1
  for patch in $all_patches; do
    base="$(basename "${patch}")"
    if [[ ! "${patch}" =~ ^https ]]; then
      continue
    fi
    wget "${patch}"
    sed -i "s!${patch}!utils/${base}!" build.config
    git add "${base}" build.config
    git commit -s -m "Commit new patch and submodule"
    popd || exit 1
  done
  popd || exit 1
}
