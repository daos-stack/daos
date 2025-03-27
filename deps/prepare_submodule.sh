#!/bin/bash

COMP="$1"
GIT_TAG=""
DAOS_ROOT="$(realpath "$(realpath "$(dirname "${BASH_SOURCE[0]}")")/..")"

get_tag()
{
  pushd "${DAOS_ROOT}" || exit 1
  git submodule update --init --recursive
  submodule="${DAOS_ROOT}/deps/${COMP}"
  git rm -rf "${submodule}"
  # shellcheck disable=SC1090,SC2283
  source <(grep = <(grep -A10 '\[commit_versions\]' "${DAOS_ROOT}/utils/build.config"))
  GIT_TAG="${!COMP}"

  if [ -z "${GIT_TAG}" ]; then
    echo "Empty tag for ${COMP}"
    exit 1
  fi

  eval "${COMP}"=""
  # shellcheck disable=SC1090,SC2283
  source <(grep = <(grep -A10 '\[repos\]' "${DAOS_ROOT}/utils/build.config"))
  REPO="${!COMP}"

  if [ -z "${REPO}" ]; then
    echo "No repo defined for ${COMP}"
    exit 1
  fi

  git submodule add --name "${COMP}" "${REPO}" "deps/${COMP}"
  git submodule udpate --init --recursive
  pushd "${submodule}" || exit 1
  git fetch origin
  git checkout "${GIT_TAG}" --recurse-submodules
  popd || exit 1
  popd || exit 1
}

copy_patches()
{
  eval "${COMP}"=""
  # shellcheck disable=SC1090,SC2283
  source <(grep = <(grep -A10 '\[patch_versions\]' "${DAOS_ROOT}/utils/build.config"))
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
    count=$(( count + 1 ))
  done
  popd || exit 1
  git add "${patch_dir}"/*
}

get_tag
copy_patches

echo "Inspect and commit your changes"
