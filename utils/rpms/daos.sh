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
# daos package
files=()
TARGET_PATH="${sysconfdir}/daos"
list_files files "${SL_PREFIX}/etc/memcheck*.supp"
append_install_list "${files[@]}"

TARGET_PATH="${sysconfdir}/bash_completion.d"
list_files files "${SL_PREFIX}/etc/bash_completion.d/daos.bash"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos/certgen"
list_files files "${SL_PREFIX}/lib64/daos/certgen/*"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libgurt.so.*" \
  "${SL_PREFIX}/lib64/libcart.so.*" \
  "${SL_PREFIX}/lib64/libdaos_common.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos"
list_files files "${SL_PREFIX}/lib64/daos/VERSION"
append_install_list "${extras[@]}"

mkdir -p "${tmp}${sysconfdir}/daos/certs"
extras+=("${tmp}${sysconfdir}/daos/certs=${sysconfdir}/daos/certs")

EXTRA_OPTS=("--rpm-attr" "0755,root,root:${sysconfdir}/daos/certs")

DEPENDS=( "mercury >= ${mercury_version}" "${libfabric_lib} >= ${libfabric_version}" )
build_package "daos"

# Only build server RPMs if we built the server
if [ -f "${SL_PREFIX}/bin/daos_server" ]; then
  echo "Creating server packages"
  # daos-server package
  mkdir -p "${tmp}/${sysconfdir}/ld.so.conf.d"
  echo "${libdir}/daos_srv" > "${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf"
  install_list+=("${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf=${sysconfdir}/ld.so.conf.d/daos.conf")
  mkdir -p "${tmp}/${sysctldir}"
  install -m 644 "utils/rpms/${sysctl_script_name}" "${tmp}/${sysctldir}"
  install_list+=("${tmp}/${sysctldir}/${sysctl_script_name}=${sysctldir}/${sysctl_script_name}")
  mkdir -p "${tmp}/${unitdir}"
  install -m 644 "utils/systemd/${server_svc_name}" "${tmp}/${unitdir}"
  install_list+=("${tmp}/${unitdir}/${server_svc_name}=${unitdir}/${server_svc_name}")
  mkdir -p "${tmp}/${sysconfdir}/daos/certs"

  TARGET_PATH="${bindir}"
  list_files files "${SL_PREFIX}/bin/daos_engine" \
                   "${SL_PREFIX}/bin/daos_metrics" \
                   "${SL_PREFIX}/bin/ddb" \
                   "${SL_PREFIX}/bin/daos_server_helper" \
                   "${SL_PREFIX}/bin/daos_storage_estimator.py" \
                   "${SL_PREFIX}/bin/daos_server"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"
  sed -i -e '1s/env //' "${tmp}/${bindir}/daos_storage_estimator.py"

  TARGET_PATH="${libdir}"
  list_files files "${SL_PREFIX}/lib64/libdaos_common_pmem.so" \
    "${SL_PREFIX}/lib64/libdav_v2.so"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

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
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${sysconfdir}/daos"
  list_files files "${SL_PREFIX}/etc/daos_server.yml" \
  "${SL_PREFIX}/etc/vos_size_input.yaml"
  append_install_list "${files[@]}"

  TARGET_PATH="${datadir}/daos/control"
  list_files files "${SL_PREFIX}/share/daos/control/*"
  append_install_list "${files[@]}"

  estimator="$(find "${SL_PREFIX}" -name storage_estimator | sed "s#${SL_PREFIX}/lib64##")"
  TARGET_PATH="${libdir}${estimator}"
  list_files files "${SL_PREFIX}/lib64${estimator}/*"
  append_install_list "${files[@]}"

  EXTRA_OPTS=()
  cat << EOF  > "${tmp}/pre_install_server"
#!/bin/bash
getent group daos_metrics >/dev/null || groupadd -r daos_metrics
getent group daos_server >/dev/null || groupadd -r daos_server
getent group daos_daemons >/dev/null || groupadd -r daos_daemons
getent passwd daos_server >/dev/null || useradd -s /sbin/nologin -r -g daos_server -G daos_metrics,daos_daemons daos_server
EOF
  EXTRA_OPTS+=("--before-install" "${tmp}/pre_install_server")

cat << EOF  > "${tmp}/post_install_server"
#!/bin/bash
set -x
ldconfig
sysctl -p "${sysctldir}/${sysctl_script_name}"
systemctl daemon-reload || true
EOF
  EXTRA_OPTS+=("--after-install" "${tmp}/post_install_server")

cat << EOF  > "${tmp}/pre_uninstall_server"
#!/bin/bash
# TODO: workout what %systemd_preun %{server_svc_name} does
EOF
  EXTRA_OPTS+=("--before-remove" "${tmp}/pre_uninstall_server")

  if [[ "${DISTRO:-el8}" =~ suse ]]; then
    cat << EOF  > "${tmp}/post_uninstall_server"
#!/bin/bash
# TODO: work out what %postun server does
ldconfig
# TODO: workout what %systemd_postun %{server_svc_name} does
EOF
    EXTRA_OPTS+=("--after-remove" "${tmp}/post_uninstall_server")
  fi
  EXTRA_OPTS+=("--rpm-attr" "0644,root,root:${sysconfdir}/daos/daos_server.yml")
  EXTRA_OPTS+=("--rpm-attr" "0700,daos_server,daos_server:${sysconfdir}/daos/certs/clients")
  EXTRA_OPTS+=("--rpm-attr" "4750,root,daos_server:${bindir}/daos_server_helper")

  DEPENDS=( "daos = ${VERSION}-${RELEASE}" "daos-spdk = ${VERSION}-${RELEASE}" )
  DEPENDS+=( "${pmemobj_lib} >= ${pmdk_version}" "${argobots_lib} >= ${argobots_version}" )
  build_package "daos-server"

  TARGET_PATH="${bindir}"
  list_files files "${SL_PREFIX}/bin/dtx_tests" \
                   "${SL_PREFIX}/bin/dtx_ut" \
                   "${SL_PREFIX}/bin/evt_ctl" \
                   "${SL_PREFIX}/bin/rdbt" \
                   "${SL_PREFIX}/bin/smd_ut" \
                   "${SL_PREFIX}/bin/bio_ut" \
                   "${SL_PREFIX}/bin/vea_ut" \
                   "${SL_PREFIX}/bin/vos_tests" \
                   "${SL_PREFIX}/bin/vea_stress" \
                   "${SL_PREFIX}/bin/ddb_tests" \
                   "${SL_PREFIX}/bin/ddb_ut" \
                   "${SL_PREFIX}/bin/obj_ctl" \
                   "${SL_PREFIX}/bin/vos_perf"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

  DEPENDS=("daos-server = ${VERSION}-${RELEASE}" "daos-admin = ${VERSION}-${RELEASE}")
  build_package "daos-server-tests"
else
  echo "Skipping server packaging because server is not built"
fi

# daos-admin package
TARGET_PATH="${bindir}"
list_files files "${SL_PREFIX}/bin/dmg"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man8"
list_files files "${SL_PREFIX}/share/man/man8/dmg.8*"
append_install_list "${files[@]}"

TARGET_PATH="${sysconfdir}/daos"
list_files files "${SL_PREFIX}/etc/daos_control.yml"
append_install_list "${files[@]}"

DEPENDS=( "daos = ${VERSION}-${RELEASE}" )
build_package "daos-admin"

# daos-client package
TARGET_PATH="${bindir}"
list_files files "${SL_PREFIX}/bin/cart_ctl" \
                 "${SL_PREFIX}/bin/self_test" \
                 "${SL_PREFIX}/bin/daos_agent" \
                 "${SL_PREFIX}/bin/dfuse" \
                 "${SL_PREFIX}/bin/daos"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libdaos.so.*" \
                 "${SL_PREFIX}/lib64/libdaos_cmd_hdlrs.so" \
                 "${SL_PREFIX}/lib64/libdaos_self_test.so" \
                 "${SL_PREFIX}/lib64/libdfs.so" \
                 "${SL_PREFIX}/lib64/libds3.so" \
                 "${SL_PREFIX}/lib64/libduns.so" \
                 "${SL_PREFIX}/lib64/libdfuse.so" \
                 "${SL_PREFIX}/lib64/libioil.so" \
                 "${SL_PREFIX}/lib64/libpil4dfs.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos"
list_files files "${SL_PREFIX}/lib64/daos/API_VERSION"
append_install_list "${files[@]}"

pydaos="$(find "${SL_PREFIX}/lib64" -name pydaos | sed "s#${SL_PREFIX}/lib64##")"
TARGET_PATH="${libdir}${pydaos}"
list_files files "${SL_PREFIX}/lib64${pydaos}/*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man8"
list_files files "${SL_PREFIX}/share/man/man8/daos.8*"
append_install_list "${files[@]}"

TARGET_PATH="${sysconfdir}/daos"
list_files files "${SL_PREFIX}/etc/daos_agent.yml"
append_install_list "${files[@]}"

mkdir -p "${tmp}/${unitdir}"
install -m 644 "utils/systemd/${agent_svc_name}" "${tmp}/${unitdir}"
install_list+=("${tmp}/${unitdir}/${agent_svc_name}=${unitdir}/${agent_svc_name}")

TARGET_PATH="${datadir}/daos"
list_files files "${SL_PREFIX}/share/daos/ioil-ld-opts"
append_install_list "${files[@]}"

EXTRA_OPTS=()
cat << EOF  > "${tmp}/pre_install_client"
getent group agent >/dev/null || groupadd -r daos_agent
getent group daos_daemons >/dev/null || groupadd -r daos_daemons
getent passwd daos_agent >/dev/null || useradd -s /sbin/nologin -r -g daos_agent -G daos_daemons daos_agent
EOF
EXTRA_OPTS+=("--before-install" "${tmp}/pre_install_client")

cat << EOF  > "${tmp}/post_install_client"
systemctl daemon-reload || true
EOF
EXTRA_OPTS+=("--after-install" "${tmp}/post_install_client")

cat << EOF  > "${tmp}/pre_uninstall_client"
# TODO: workout what %systemd_preun %{agent_svc_name} does
EOF
EXTRA_OPTS+=("--before-remove" "${tmp}/pre_uninstall_client")

if [[ "${DISTRO:-el8}" =~ suse ]]; then
  cat << EOF  > "${tmp}/post_uninstall_client"
# TODO: workout what %systemd_postun %{agent_svc_name} does
EOF
  EXTRA_OPTS+=("--after-remove" "${tmp}/post_uninstall_client")
fi

EXTERNAL_DEPENDS=("fuse3")
DEPENDS=("daos = ${VERSION}-${RELEASE}")
build_package "daos-client"

# client-tests
TARGET_PATH="${daoshome}/TESTING"
FILTER_LIST=("TESTING/ftest/avocado_tests.yaml")
list_files files "${SL_PREFIX}/lib/daos/TESTING/*"
BASE_PATH="${tmp}${daoshome}/TESTING"
append_install_list "${files[@]}"

TARGET_PATH="${bindir}"
list_files files "${SL_PREFIX}/bin/hello_drpc" \
	   "${SL_PREFIX}/bin/acl_dump_test" \
	   "${SL_PREFIX}/bin/agent_tests" \
	   "${SL_PREFIX}/bin/drpc_engine_test" \
	   "${SL_PREFIX}/bin/drpc_test" \
	   "${SL_PREFIX}/bin/dfuse_test" \
	   "${SL_PREFIX}/bin/eq_tests" \
	   "${SL_PREFIX}/bin/job_tests" \
	   "${SL_PREFIX}/bin/jump_pl_map" \
	   "${SL_PREFIX}/bin/pl_bench" \
	   "${SL_PREFIX}/bin/ring_pl_map" \
	   "${SL_PREFIX}/bin/security_test" \
	   "${SL_PREFIX}/bin/fault_status" \
	   "${SL_PREFIX}/bin/crt_launch" \
	   "${SL_PREFIX}/bin/daos_perf" \
	   "${SL_PREFIX}/bin/daos_test" \
	   "${SL_PREFIX}/bin/daos_debug_set_params" \
	   "${SL_PREFIX}/bin/dfs_test" \
	   "${SL_PREFIX}/bin/jobtest" \
	   "${SL_PREFIX}/bin/daos_gen_io_conf" \
	   "${SL_PREFIX}/bin/daos_run_io_conf"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libdaos_tests.so" \
	   "${SL_PREFIX}/lib64/libdpar.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${sysconfdir}/daos"
list_files files "${SL_PREFIX}/etc/fault-inject-cart.yaml"
append_install_list "${files[@]}"

#todo add external depends
EXTERNAL_DEPENDS=("${protobufc_lib}")
EXTERNAL_DEPENDS+=("fio")
EXTERNAL_DEPENDS+=("git")
EXTERNAL_DEPENDS+=("dbench")
EXTERNAL_DEPENDS+=("lbzip2")
EXTERNAL_DEPENDS+=("attr")
EXTERNAL_DEPENDS+=("ior")
EXTERNAL_DEPENDS+=("go >= 1.21")
if [ "${OUTPUT_TYPE:-rpm}" = "rpm" ]; then
  EXTERNAL_DEPENDS+=("${lmod}")
fi
EXTERNAL_DEPENDS+=("${capstone_lib}")
EXTERNAL_DEPENDS+=("pciutils")
EXTERNAL_DEPENDS+=("${ndctl_lib}")
if [[ "${DISTRO:-el8}" =~ el ]]; then
  EXTERNAL_DEPENDS+=("daxctl-devel")
fi
DEPENDS=( "daos-client = ${VERSION}-${RELEASE}" "daos-admin = ${VERSION}-${RELEASE}")
DEPENDS+=("daos-devel = ${VERSION}-${RELEASE}")
build_package "daos-client-tests"

TARGET_PATH="${includedir}"
list_files files "${SL_PREFIX}/include/*"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libdaos.so" \
                 "${SL_PREFIX}/lib64/libgurt.so" \
                 "${SL_PREFIX}/lib64/libcart.so" \
                 "${SL_PREFIX}/lib64/*.a"
append_install_list "${files[@]}"

TARGET_PATH="${daoshome}/python"
list_files files "${SL_PREFIX}/lib/daos/python/*"
append_install_list "${files[@]}"

EXTERNAL_DEPENDS=("${uuid_lib}")
DEPENDS=("daos-client = ${VERSION}-${RELEASE}")
build_package "${daos_dev}"

if [ "${OUTPUT_TYPE:-rpm}" = "rpm" ]; then
  EXTERNAL_DEPENDS=("${hdf5_lib}")
  TARGET_PATH="${libdir}"
  list_files files "${SL_PREFIX}/lib64/libdaos_serialize.so"
  append_install_list "${files[@]}"
  build_package "daos-serialize"
fi

if [ -f "${SL_PREFIX}/bin/daos_firmware_helper" ]; then
  TARGET_PATH="${bindir}/daos_firmware_helper"
  list_files files "${SL_PREFIX}/bin/daos_firmware_helper"
  append_install_list "${files[@]}"

  DEPENDS=("daos-server = ${VERSION}-${RELEASE}")
  build_package "daos-firmware"
fi

TARGET_PATH="${libdir}"
list_files files "${SL_PREFIX}/lib64/libdpar_mpi.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"
build_package "daos-client-tests-openmpi"

#shim packages
PACKAGE_TYPE="empty"
ARCH="noarch"
EXTERNAL_DEPENDS=("libmpi.so.40()(64bit)")
DEPENDS=()
# no files in shim
build_package "daos-mofed-shim"

DEPENDS=("daos-client-tests = ${VERSION}-${RELEASE}")
build_package "daos-tests"

build_package "daos-client-tests-mpich"

DEPENDS=("daos-tests = ${VERSION}-${RELEASE}")
DEPENDS=("daos-tests-openmpi = ${VERSION}-${RELEASE}")
DEPENDS=("daos-tests-mpich = ${VERSION}-${RELEASE}")
DEPENDS=("daos-serialize = ${VERSION}-${RELEASE}")
build_package "daos-tests-internal"
