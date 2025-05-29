#!/bin/bash
# (C) Copyright 2025 Google LLC
root="$(realpath "$(dirname "$(dirname "$(dirname "${BASH_SOURCE[0]}")")")")"
. "${root}/utils/sl/setup_local.sh" > /dev/null
unset tmp
trap 'if [ -n "${tmp}" ] && [ -z "${DEBUG:-}" ]; then rm -rf "${tmp}"; fi' EXIT
tmp="$(mktemp -d)"

export install_list=()
export PACKAGE_TYPE="dir"
export dbg_list=()
export EXTERNAL_DEPENDS=()
export DEPENDS=()
export EXTRA_OPTS=()
export FILTER_LIST=()
isa="$(uname -m)"
export isa
export prefix="${PREFIX:-/usr}"
export bindir="${BINDIR:-/usr/bin}"
export libdir="${LIBDIR:-/usr/lib64}"
export includedir="${INCLUDEDIR:-/usr/include}"
export datadir="${DATADIR:-/usr/share}"
export sysconfdir="${SYSCONFDIR:-/etc}"
export sysctldir="${SYSCTLDIR:-/etc/sysctl.d}"
export unitdir="${UNITDIR:-/usr/lib/systemd/system}"
export mandir="${MANDIR:-/usr/share/man}"
daos_version="$(grep "^Version: " "${root}/utils/rpms/daos.spec" | sed 's/^Version: *//')"
export daos_version
daos_release="$(grep "^Release: " "${root}/utils/rpms/daos.spec" | \
  sed 's/^Release: *//' | sed 's/%.*//')"
export daos_release

export libfabric_version="1.22.0"
export mercury_version="2.4.0"
export argobots_version="1.2"
export pmdk_version="2.1.0"
export isal_version="2.30.0"
export isal_crypto_version="2.24.0"

source utils/rpms/package_names.sh

filter_file() {
  for filter in "${FILTER_LIST[@]}"; do
    if [[ "${1}" =~ ${filter} ]]; then
      echo "Filtering ${1}"
      return 0
    fi
  done
  return 1
}

expand_directories() {
  local file
  local -n expanded=$1; shift
  local -n expanddirs=$1; shift
  for file in "$@"; do
    if filter_file "${file}"; then
      continue
    fi
    if [ -d "${file}" ]; then
      local tmparray=()
      readarray -t tmparray <<< "$(ls -1 -d "${file}"/*)"
      for subfile in "${tmparray[@]}"; do
        if filter_file "${subfile}"; then
          continue
        fi
        if [ -d "${subfile}" ]; then
          expanddirs+=("${subfile}")
        else
          expanded+=("${subfile}")
        fi
      done
    else
      expanded+=("${file}")
    fi
  done
}

list_files() {
  local -n listvar=$1; shift
  target_dir="${tmp}${TARGET_PATH}"
  mkdir -p "${target_dir}"
  # shellcheck disable=SC2068
  readarray -t tmparray <<< "$(ls -1 -d $@)"
  listvar=()
  directories=()
  for file in "${tmparray[@]}"; do
    if filter_file "${file}"; then
      continue
    fi
    new_file="${target_dir}/$(basename "${file}")"
    if [ -d "${file}" ]; then
      mkdir -p "${new_file}"
      cp -R "${file}/"* "${new_file}"
      directories+=("${new_file}")
    else
      cp -d "${file}" "${new_file}"
      listvar+=("${new_file}")
    fi
  done
  while [ ${#directories[@]} -gt 0 ]; do
    newfiles=()
    newdirs=()
    expand_directories newfiles newdirs "${directories[@]}";
    directories=("${newdirs[@]}")
    listvar+=("${newfiles[@]}")
  done
  FILTER_LIST=()
}

append_install_list() {
  for file in "$@"; do
    if [ -z "${BASE_PATH:-}" ]; then
      BASE_PATH=$(dirname "${file}")
    fi
    base="$(realpath -s -m "${file}" --relative-to="${BASE_PATH}")"
    install_list+=("${file}=${TARGET_PATH}/${base}")
  done
  unset BASE_PATH
}

clean_bin() {
  for file in "$@"; do
    if [ -L "${file}" ]; then
      continue
    fi
    if ! patchelf --remove-rpath "${file}" > /dev/null 2>&1; then
      continue
    fi
    relative="$(realpath -s -m "${file}" --relative-to="${tmp}${prefix}")"
    base="$(basename "${relative}")"
    dir="$(dirname "${relative}")"
    dname="${base}-${VERSION}-${RELEASE}.${DISTRO:-el8}-${isa}.debug"
    dbgroot="${tmp}/usr/lib/debug/${dir}"
    mkdir -p "${dbgroot}"
    dbgpath="${dbgroot}/${dname}"
    cp "${file}" "${dbgpath}"
    strip --only-keep-debug "${dbgpath}" > /dev/null 2>&1
    strip "${file}" > /dev/null 2>&1 || true
    dbg_list+=("${dbgpath}")
  done
}

replace_paths() {
  old="$1"; shift

  for file in "$@"; do
    sed -i "s#$old/lib64#${libdir}#g" "${file}"
    sed -i "s#$old/bin#${bindir}#g" "${file}"
    sed -i "s#$old/include#${includedir}#g" "${file}"
    sed -i "s#$old#${prefix}#g" "${file}"
  done
}

create_depends() {
  local -n deps=$1; shift
  deps=()
  for dep in "$@"; do
    deps+=( "--depends" "${dep}" )
  done
}

build_package() {
  name="$1"; shift

  output_type="${OUTPUT_TYPE:-rpm}"
  EXTRA_OPTS+=("--rpm-autoprov")
  EXTRA_OPTS+=("--rpm-autoreq")

  depends=()
  create_depends depends "${DEPENDS[@]}" "${EXTERNAL_DEPENDS[@]}"
  pkgname="${name}-${VERSION}-${RELEASE}.${ARCH}.${output_type}"
  rm -f "${pkgname}"
  # shellcheck disable=SC2068
  fpm -s "${PACKAGE_TYPE}" -t "${output_type}" \
  -p "${pkgname}" \
  --name "${name}" \
  --license "${LICENSE}" \
  --version "${VERSION}" \
  --iteration "${RELEASE}" \
  --architecture "${ARCH}" \
  --description "${DESCRIPTION}" \
  --url "${URL}" \
  "${depends[@]}" \
  "${EXTRA_OPTS[@]}" \
  "${install_list[@]}"

  export EXTRA_OPTS=()
  install_list=()

  if [[ ! "${name}" =~ debuginfo ]]; then
    build_debug_package "${name}"
  fi
  DEPENDS=()
  EXTERNAL_DEPENDS=()
}

build_debug_package() {
  local version
  name="$1"; shift
  if [ ${#dbg_list[@]} -eq 0 ]; then
    return
  fi
  new_deps=()
  for dep in "${DEPENDS[@]}"; do
    # shellcheck disable=SC2001
    version="$(echo "${dep}" | sed 's/[^ ]*//')"
    new_dep="${dep// *}-debuginfo${version}"
    if [[ "${new_dep}" =~ -dev ]]; then
      continue
    fi
    new_deps+=("${new_dep}")
  done
  DEPENDS=("${new_deps[@]}")

  for pkg in "${dbg_list[@]}"; do
    install_list+=("${pkg}=${pkg//$tmp/}")
  done
  build_package "${name}-debuginfo"
  dbg_list=()
}
