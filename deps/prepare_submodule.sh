#!/bin/bash

read_value()
{
  comp_name=$1
  field_name=$2

  # shellcheck disable=SC1090
  source <(awk -v RS= "/\[${field_name}\]/" "${DAOS_ROOT}/utils/build.config" | tail -n+2)
  echo "${!comp_name}"
}

set -x
COMP="$1"
GIT_TAG=""
DAOS_ROOT="$(realpath "$(realpath "$(dirname "${BASH_SOURCE[0]}")")/..")"
export DAOS_ROOT
export read_value

get_tag()
{
  pushd "${DAOS_ROOT}" || exit 1
  git submodule update --init --recursive
  submodule="${DAOS_ROOT}/deps/${COMP}"
  GIT_TAG="$(read_value "${COMP}" commit_versions)"

  if [ -z "${GIT_TAG}" ]; then
    echo "Empty tag for ${COMP}"
    exit 1
  fi
  echo "Tag is ${GIT_TAG}"

  REPO="$(read_value "${COMP}" repos)"
  if [ -z "${REPO}" ]; then
    echo "No repo defined for ${COMP}"
    exit 1
  fi
  echo "Repo is ${REPO}"

  git submodule update --init --recursive
  if ! git submodule status "deps/${COMP}"; then
    git submodule add --name "${COMP}" "${REPO}" "deps/${COMP}"
  fi
  git submodule update --init --recursive
  pushd "${submodule}" || exit 1
  git submodule update --init --recursive
  git fetch origin
  git checkout "${GIT_TAG}" --recurse-submodules
  popd || exit 1
  popd || exit 1
}

copy_patches()
{
  if [ "${SKIP_PATCHES:-0}" = "1" ]; then
    echo "Skipping patches"
    return
  fi
  patches="$(read_value "${COMP}" patch_versions)"
  if [ -z "${patches}" ]; then
    echo "No patches for ${COMP}"
    return
  fi
  all_patches=${patches//,/ }
  patch_dir="${DAOS_ROOT}/deps/patches/${COMP}"
  git rm -rf "${patch_dir}"/*
  mkdir -p "${patch_dir}"
  pushd "${patch_dir}" || exit 1
  count=1
  new_patches=""
  for patch in $all_patches; do
    echo "Preparing patch ${patch}"
    base="$(basename "${patch}")"
    patch_name=$(printf "%04d_%s" ${count} "${base}")
    if [[ "${patch}" =~ ^https ]]; then
      wget "${patch}"
      mv "${base}" "${patch_name}"
    else
      if [[ ! "${patch}" =~ ^deps/patches ]]; then
        cp "${DAOS_ROOT}/${patch}" "${patch_name}"
      else
        patch_name="${base}"
      fi
    fi
    if [ -z "${new_patches}" ]; then
      new_patches="deps/patches/${COMP}/${patch_name}"
    else
      new_patches+=",deps/patches/${COMP}/${patch_name}"
    fi
    if [[ ! "${patch}" =~ ^deps/patches ]]; then
      count=$(( count + 1 ))
    fi
  done
  sed "s#${!COMP}#${new_patches}#" -i "${DAOS_ROOT}/utils/build.config"
  popd || exit 1
  pushd "${DAOS_ROOT}" || exit 1
  git add "deps/patches/${COMP}/"*
  popd || exit 1
}

get_tag
copy_patches

echo "Inspect and commit your changes"
