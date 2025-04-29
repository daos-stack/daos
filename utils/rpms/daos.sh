#!/bin/bash
# (C) Copyright 2025 Google LLC
# WORK IN PROGRESS
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_PREFIX}" ]; then
  echo "daos is not built"
  exit 1
fi

bins=()
dbg_bin=()
dbg_lib=()
dbg_internal_lib=()
files=()
libs=()
internal_libs=()
data=()

daoshome="${prefix}/lib/daos"
server_svc_name="daos_server.service"
agent_svc_name="daos_agent.service"
sysctl_script_name="10-daos_server.conf"

VERSION=${daos_version}
RELEASE=${daos_release}
LICENSE="BSD-2-Clause-Patent"
ARCH="${isa}"
DESCRIPTION="The Distributed Asynchronous Object Storage (DAOS) is an open-source
software-defined object store designed from the ground up for
massively distributed Non Volatile Memory (NVM). DAOS takes advantage
of next generation NVM technology like Storage Class Memory (SCM) and
NVM express (NVMe) while presenting a key-value storage interface and
providing features such as transactional non-blocking I/O, advanced
data protection with self healing on top of commodity hardware, end-
to-end data integrity, fine grained data control and elastic storage
to optimize performance and cost."
URL="https://daos.io"

# Some extra "install" steps

# common files
supp=()
TARGET_PATH="${sysconfdir}/daos"
list_files files "${SL_PREFIX}/etc/memcheck*.supp"
create_install_list supp "${files[@]}"

comp=()
TARGET_PATH="${sysconfdir}/bash_completion.d"
list_files files "${SL_PREFIX}/etc/bash_completion.d/daos.bash"
create_install_list comp "${files[@]}"

certs=()
TARGET_PATH="${libdir}/daos/certgen"
list_files files "${SL_PREFIX}/lib64/daos/certgen/*"
create_install_list certs "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libgurt.so.*" \
  "${SL_PREFIX}/lib64/libcart.so.*" \
  "${SL_PREFIX}/lib64/libdaos_common.so"
clean_bin dbg_lib "${files[@]}"
create_install_list libs "${files[@]}"

extras=()
TARGET_PATH="${libdir}/daos"
list_files files "${SL_PREFIX}/lib64/daos/VERSION"
create_install_list version "${extras[@]}"

mkdir -p "${tmp}${sysconfdir}/daos/certs"
extras+=("${tmp}${sysconfdir}/daos/certs=${sysconfdir}/daos/certs")

EXTRA_OPTS=("rpm-defattr-file" "(-, root, root, -)")
EXTRA_OPTS+=("rpm-defattr-dir" "(-, root, root, -)")
EXTRA_OPTS+=("rpm-attr" "0755,root,root:${sysconfdir}/daos/certs")

DEPENDS=( "mercury >= ${mercury_version}" "libfabric >= ${libfabric_version}" )
build_package "daos" "${supp[@]}" "${comp[@]}" "${libs[@]}" "${certs[@]}" "${extras[@]}"
build_debug_package "daos" "${dbg_lib[@]}"

# Only build server RPMs if we built the server
if [ -f "${SL_PREFIX}/bin/daos_server" ]; then
  echo "Creating server packages"
  mkdir -p "${tmp}/${sysconfdir}/ld.so.conf.d"
  echo "${libdir}/daos_srv" > "${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf"
  extra_files=("${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf=${sysconfdir}/ld.so.conf.d/daos.conf")
  mkdir -p "${tmp}/${sysctldir}"
  install -m 644 "utils/rpms/${sysctl_script_name}" "${tmp}/${sysctldir}"
  extra_files+=("${tmp}/${sysctldir}/${sysctl_script_name}=${sysctldir}/${sysctl_script_name}")
  mkdir -p "${tmp}/${unitdir}"
  install -m 644 "utils/systemd/${server_svc_name}" "${tmp}/${unitdir}"
  extra_files+=("${tmp}/${unitdir}/${server_svc_name}=${unitdir}/${server_svc_name}")
  mkdir -p "${tmp}/${sysconfdir}/daos/certs"

  TARGET_PATH="${bindir}"
  list_files files "${SL_PREFIX}/bin/daos_engine" \
                   "${SL_PREFIX}/bin/daos_metrics" \
                   "${SL_PREFIX}/bin/ddb" \
                   "${SL_PREFIX}/bin/daos_server_helper" \
                   "${SL_PREFIX}/bin/daos_storage_estimator.py" \
                   "${SL_PREFIX}/bin/daos_server"
  clean_bin dbg_bin "${files[@]}"
  create_install_list bins "${files[@]}"
  sed -i -e '1s/env //' "${tmp}/${bindir}/daos_storage_estimator.py"

  TARGET_PATH="${libdir}"
  list_files files "${SL_PREFIX}/lib64/libdaos_common_pmem.so" \
    "${SL_PREFIX}/lib64/libdav_v2.so"
  clean_bin dbg_lib "${files[@]}"
  create_install_list libs "${files[@]}"

  TARGET_PATH="${libdir}/daos_srv"
  list_files files "${SL_PREFIX}/lib64/daos_srv/libchk.so" \
    "${SL_PREFIX}/lib64/daos_srv/libcont.so" \
    "${SL_PREFIX}/lib64/daos_srv/libddb.so" \
    "${SL_PREFIX}/lib64/daos_srv/libdtx.so" \
    "${SL_PREFIX}/lib64/daos_srv/libmgmt.so" \
    "${SL_PREFIX}/lib64/daos_srv/libobj.so" \
    "${SL_PREFIX}/lib64/daos_srv/libpool.so" \
    "${SL_PREFIX}/lib64/daos_srv/librdb.so" \
    "${SL_PREFIX}/lib64/daos_srv/librdbt.so" \
    "${SL_PREFIX}/lib64/daos_srv/librebuild.so" \
    "${SL_PREFIX}/lib64/daos_srv/librsvc.so" \
    "${SL_PREFIX}/lib64/daos_srv/libsecurity.so" \
    "${SL_PREFIX}/lib64/daos_srv/libvos_srv.so" \
    "${SL_PREFIX}/lib64/daos_srv/libvos_size.so" \
    "${SL_PREFIX}/lib64/daos_srv/libvos.so" \
    "${SL_PREFIX}/lib64/daos_srv/libbio.so" \
    "${SL_PREFIX}/lib64/daos_srv/libplacement.so" \
    "${SL_PREFIX}/lib64/daos_srv/libpipeline.so"
  clean_bin dbg_internal_lib "${files[@]}"
  create_install_list internal_libs "${files[@]}"

  conf=()
  TARGET_PATH="${sysconfdir}/daos"
  list_files files "${SL_PREFIX}/etc/daos_server.yml" \
  "${SL_PREFIX}/etc/vos_size_input.yaml"
  create_install_list conf "${files[@]}"

  TARGET_PATH="${datadir}/daos/control"
  list_files files "${SL_PREFIX}/share/daos/control/*"
  create_install_list data "${files[@]}"

  estimator="$(find "${SL_PREFIX}" -name storage_estimator | sed "s#${SL_PREFIX}/lib64##")"
  scripts=()
  TARGET_PATH="${libdir}${estimator}"
  list_files files "${SL_PREFIX}/lib64${estimator}/*"
  create_install_list scripts "${files[@]}"

  EXTRA_OPTS=()
  cat << EOF  > "${tmp}/pre_install_server"
getent group daos_metrics >/dev/null || groupadd -r daos_metrics
getent group daos_server >/dev/null || groupadd -r daos_server
getent group daos_daemons >/dev/null || groupadd -r daos_daemons
getent passwd daos_server >/dev/null || useradd -s /sbin/nologin -r -g daos_server -G daos_metrics,daos_daemons daos_server
EOF
  EXTRA_OPTS+=("--before-install" "${tmp}/pre_install_server")

cat << EOF  > "${tmp}/post_install_server"
ldconfig
sysctl -p "${sysctldir}/${sysctl_script_name}"
systemctl daemon-reload
EOF
  EXTRA_OPTS+=("--after-install" "${tmp}/post_install_server")

cat << EOF  > "${tmp}/pre_uninstall_server"
# TODO: workout what %systemd_preun %{server_svc_name} does
EOF
  EXTRA_OPTS+=("--before-remove" "${tmp}/pre_uninstall_server")

  if [[ "${DISTRO:-el8}" =~ suse ]]; then
    cat << EOF  > "${tmp}/post_uninstall_server"
# TODO: work out what %postun server does
ldconfig
# TODO: workout what %systemd_postun %{server_svc_name} does
EOF
  EXTRA_OPTS+=("--after-remove" "${tmp}/post_uninstall_server")
  fi
  EXTRA_OPTS+=("rpm-attr" "0644,root,root:${sysconfdir}/daos/daos_server.yml")
  EXTRA_OPTS+=("rpm-attr" "0700,daos_server,daos_server:${sysconfdir}/daos/certs/clients")
  EXTRA_OPTS+=("rpm-attr" "4750,root,daos_server:${bindir}/daos_server_helper")

  DEPENDS=( "daos = ${daos_version}" "daos-spdk = ${daos_version}" "libpmemobj = ${pmdk_version}" )
  DEPENDS+=( "argobots = ${argobots_version}" )
  build_package "daos-server" "${bins[@]}" "${extra_files[@]}" "${libs[@]}" "${conf[@]}" "${data[@]}" \
	               "${internal_libs[@]}" "${scripts[@]}"
  build_debug_package "daos-server" "${dbg_bin[@]}" "${dbg_lib[@]}" "${dbg_internal_lib[@]}"
else
  echo "Skipping server packaging because server is not built"
fi

