#!/bin/bash
# (C) Copyright 2025 Google LLC
root="$(realpath "$(dirname "$(dirname "$(dirname "${BASH_SOURCE[0]}")")")")"
. "${root}/utils/sl/setup_local.sh" > /dev/null
unset tmp
trap 'if [ -n "${tmp}" ] && [ -z "${DEBUG:-}" ]; then rm -rf "${tmp}"; fi' EXIT
tmp="$(mktemp -d)"

export DEPENDS=()
export EXTRA_OPS=()
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
if [[ "${DISTRO:-el8}" =~ "suse" ]]; then
  libfabric_lib="libfabric1"
else
  libfabric_lib="libfabric"
fi
export libfabric_lib
export libfabric_dev="${libfabric_lib}-devel"

export mercury_version="2.4.0"

export argobots_version="1.2"
if [[ "${DISTRO:-}" =~ "suse" ]]; then
  argobots_lib="libabt0"
  argobots_dev="libabt-devel"
else
  argobots_lib="argobots"
  argobots_dev="argobots-devel"
fi
export argobots_lib
export argobots_dev

export pmdk_version="2.1.0"
export isal_version="2.30.0"
export isal_crypto_version="2.24.0"

expand_directories() {
  local -n expanded=$1; shift
  found=0
  expanded=()
  for file in "$@"; do
    if [ -d "${file}" ]; then
      local tmparray=()
      readarray -t tmparray <<< "$(ls -1 -d "${file}"/*)"
      found=1
      expanded+=("${tmparray[@]}")
    else
      expanded+=("${file}")
    fi
  done
  return ${found}
}

list_files() {
  local -n listvar=$1; shift
  target_dir="${tmp}${TARGET_PATH}"
  mkdir -p "${target_dir}"
  # shellcheck disable=SC2068
  readarray -t tmparray <<< "$(ls -1 -d $@)"
  listvar=()
  for file in "${tmparray[@]}"; do
    new_file="${target_dir}/$(basename "${file}")"
    if [ -d "${file}" ]; then
      mkdir -p "${new_file}"
      cp -R "${file}/"* "${new_file}"
    else
      cp -d "${file}" "${new_file}"
    fi
    listvar+=("${new_file}")
  done
  while true; do
    newlist=()
    if expand_directories newlist "${listvar[@]}"; then
      break
    fi
    listvar=("${newlist[@]}")
  done
}

create_install_list() {
  local -n installs=$1; shift
  installs=()
  for file in "$@"; do
    if [ -z "${BASE_PATH:-}" ]; then
      BASE_PATH=$(dirname "${file}")
    fi
    base="$(realpath -s -m "${file}" --relative-to="${BASE_PATH}")"
    installs+=("${file}=${TARGET_PATH}/${base}")
  done
  unset BASE_PATH
}

clean_bin() {
  local -n dbglist=$1; shift
  dbglist=()
  for file in "$@"; do
    if [ -L "${file}" ]; then
      continue
    fi
    if ! patchelf --remove-rpath "${file}"; then
      continue
    fi
    base="$(basename "${file}")"
    dir="$(basename "${base}")"
    dname="${base}-${VERSION}-${RELEASE}.${DISTRO:-el8}-${isa}.debug"
    dbgroot="${tmp}/usr/lib/debug/${dir}"
    mkdir -p "${dbgroot}"
    dbgpath="${dbgroot}/${dname}"
    cp "${file}" "${dbgpath}"
    strip --only-keep-debug "${dbgpath}"
    strip "${file}" || true
    dbglist+=("${dbgpath}")
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
  local -n deps=$1
  deps=()
  for dep in "${DEPENDS[@]}"; do
    deps+=( "--depends" "${dep}" )
  done
}

build_package() {
  name="$1"; shift
  depends=()
  create_depends depends
  pkgname="${name}-${VERSION}-${RELEASE}.${DISTRO:-el8}.${ARCH}.rpm"
  rm -f "${pkgname}"
  # shellcheck disable=SC2068
  fpm -s dir -t rpm \
  -p "${pkgname}" \
  --name "${name}" \
  --license "${LICENSE}" \
  --version "${VERSION}" \
  --iteration "${RELEASE}" \
  --architecture "${ARCH}" \
  --description "${DESCRIPTION}" \
  --url "${URL}" \
  "${depends[@]}" \
  "${EXTRA_OPS[@]}" \
  $@

  EXTRA_OPTS=()
}

build_debug_package() {
  local version
  name="$1"; shift
  dbgpkgs=()
  if [ $# -eq 0 ]; then
    return
  fi
  new_deps=()
  for dep in "${DEPENDS[@]}"; do
    version="$(echo "${dep}" | sed 's/[^ ]*//')"
    new_deps+=("${dep// */}-debuginfo${version}")
  done
  DEPENDS=("${new_deps[@]}")

  for pkg in "$@"; do
    dbgpkgs+=("${pkg}=${pkg//$tmp/}")
  done
  build_package "${name}-debuginfo" "${dbgpkgs[@]}"
}
