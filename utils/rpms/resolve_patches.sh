#!/bin/bash
set -ex

DAOS_ROOT="$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")"
COMP="$1"

apply_patches()
{
  echo "Applying patches to ${COMP}"

  patch_dir="${DAOS_ROOT}/utils"
  # shellcheck disable=SC2283,SC1090
  source <(grep = <(grep -A10 '\[patch_versions\]' "${patch_dir}"/build.config))
  if [ -z "${!COMP}" ]; then
    echo "No patches for ${COMP}"
    return
  fi
  all_patches=${!COMP//,/ }
  echo "${all_patches}"

  pushd "${patch_dir}" || exit 1
  for patch in $all_patches; do
    if [[ ! "${patch}" =~ ^https ]]; then
      continue
    fi
    no_slash="${patch//\//_}"
    resolved="${no_slash/:/}"
    curl -sSfL --retry 10 --retry-max-time 60  -o "${resolved}" "${patch}"
    sed -i "s!${patch}!utils/${resolved}!" build.config
    git add "${resolved}" build.config
    # actual emails don't matter, this is only local commit
    git config --global user.email "daos@daos.io"
    git config --global user.name "Automation"
    git commit -m "Commit ${patch} to ${resolved} in build.config"
  done
  popd || exit 1
}

apply_patches
