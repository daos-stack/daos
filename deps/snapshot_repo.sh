#!/bin/bash

CREATED=0
CHANGES=0
COMP="$1"
GIT_TAG=""
GIT_BRANCH=""
DAOS_ROOT="$(realpath "$(realpath "$(dirname "${BASH_SOURCE[0]}")")/..")"

get_tag()
{
  mkdir -p /tmp/clones
  source <(grep = <(grep -A10 '\[commit_versions\]' ${DAOS_ROOT}/utils/build.config))
  GIT_TAG=${!COMP}

  if [ -z "${GIT_TAG}" ]; then
    echo "Empty tag for ${COMP}"
    exit 1
  fi

  eval "${COMP}"=""
  source <(grep = <(grep -A10 '\[repos\]' ${DAOS_ROOT}/utils/build.config))
  REPO=${!COMP}

  if [ -z "${REPO}" ]; then
    echo "No repo defined for ${COMP}"
    exit 1
  fi

  echo "Component ${COMP} tag=\"${GIT_TAG}\""
  rm -rf "/tmp/clones/${COMP}"
  pushd /tmp/clones || exit 1
  git clone "${REPO}" --recurse-submodules "${COMP}"
  pushd "${COMP}" || exit 1
  git fetch origin
  if ! git checkout "${GIT_TAG}" --recurse-submodules; then
    echo "Could not get the ${GIT_TAG}"
    exit 1
  fi
  find . -type d -name ".git*" -exec rm -rf {}
  rm -rf .gitignore
  popd || exit 1
  popd || exit 1
}

copy_patches()
{
  eval "${COMP}"=""
  source <(grep = <(grep -A10 '\[patch_versions\]' ${DAOS_ROOT}/utils/build.config))
  if [ -z "${!COMP}" ]; then
    echo "No patches for ${COMP}"
    return
  fi
  all_patches=${!COMP//,/ }

  patch_dir="${DAOS_ROOT}/deps/patches/${COMP}"
  git rm -rf "${patch_dir}"/*
  mkdir -p "${patch_dir}"
  pushd "${patch_dir}" || exit 1
  count=1
  for patch in $all_patches; do
    base="$(basename "${patch}")"
    patch_name=$(printf "%04d_%s" ${count} "${base}")
    if [[ "${patch}" =~ ^https ]]; then
      wget "${patch}"
      mv "${base}" "${patch_name}"
    else
      cp "${DAOS_ROOT}/${patch}" "${patch_name}"
    fi
    count=$[ $count + 1 ]
  done
  popd || exit 1
  git add "${patch_dir}"/*
}

trap "rm -rf \"/tmp/clones/${COMP}\" /tmp/clones" EXIT

get_tag
copy_patches
pushd "${DAOS_ROOT}" || exit 1
git rm -rf "deps/${COMP}"/*
mkdir -p "deps/${COMP}"
cp -r "/tmp/clones/${COMP}"/* deps/${COMP}
git add "deps/${COMP}/"*
popd || exit 1

echo "Inspect and commit your changes"
