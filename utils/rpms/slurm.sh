#!/bin/bash
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
# shellcheck source=utils/rpms/fpm_common.sh
. "${root}/fpm_common.sh"

if [[ -z "${SL_SLURM_PREFIX:-}" || "${SL_SLURM_PREFIX}" = /usr ]]; then
  echo "Slurm must be built from source before packaging"
  exit 0
fi

if [[ "${OUTPUT_TYPE:-rpm}" != rpm ]]; then
  echo "Custom Slurm packaging currently supports RPM only" >&2
  exit 1
fi

VERSION="${slurm_version}"
RELEASE="${slurm_release}"
LICENSE="GPLv2+"
ARCH="${isa}"
DESCRIPTION="Slurm workload manager built for DAOS testing"
URL="https://github.com/schedmd/slurm"
RPM_CHANGELOG="slurm.changelog"
shopt -s nullglob

stage_files() {
  TARGET_PATH="$1"; shift
  if [[ $# -eq 0 ]]; then
    echo "No Slurm files found for ${TARGET_PATH}" >&2
    exit 1
  fi
  local source_file
  for source_file in "$@"; do
    if [[ ! -e "${source_file}" ]]; then
      echo "Missing Slurm file: ${source_file}" >&2
      exit 1
    fi
  done
  files=()
  list_files files "$@"
  BASE_PATH="${tmp}${TARGET_PATH}"
  append_install_list "${files[@]}"
}

stage_files "${bindir}" "${SL_SLURM_PREFIX}"/bin/*
clean_bin "${files[@]}"
stage_files "${libdir}" "${SL_SLURM_PREFIX}"/lib64/*.so*
clean_bin "${files[@]}"
FILTER_LIST=('\.la$' '\.a$' '/auth_none\.so$' '/cred_none\.so$'
             '/accounting_storage_mysql\.so$')
stage_files "${libdir}" "${SL_SLURM_PREFIX}/lib64/slurm"
clean_bin "${files[@]}"
for section in 1 5 8; do
  stage_files "${mandir}/man${section}" "${SL_SLURM_PREFIX}"/share/man/man"${section}"/*
done
if [[ -d "${SL_SLURM_PREFIX}/share/doc" ]]; then
  stage_files "${datadir}/doc" "${SL_SLURM_PREFIX}"/share/doc/*
fi
if [[ -d "${SL_SLURM_PREFIX}/share/bash-completion" ]]; then
  stage_files "${datadir}" "${SL_SLURM_PREFIX}/share/bash-completion"
fi
printf '#!/bin/sh\n/sbin/ldconfig\n' > "${tmp}/slurm_ldconfig"
EXTRA_OPTS+=(--after-install "${tmp}/slurm_ldconfig" --after-remove "${tmp}/slurm_ldconfig")
EXTERNAL_DEPENDS=(munge)
build_package "${slurm_lib}"

for daemon in slurmctld slurmd slurmdbd; do
  DESCRIPTION="Slurm ${daemon} daemon built for DAOS testing"
  daemon_files=("${SL_SLURM_PREFIX}/sbin/${daemon}")
  if [[ "${daemon}" = slurmd ]]; then
    daemon_files+=("${SL_SLURM_PREFIX}/sbin/slurmstepd")
    PROVIDES=(slurm-slurmmd)
  elif [[ "${daemon}" = slurmctld ]]; then
    PROVIDES=(slurmctld)
  fi
  stage_files "${SBINDIR:-/usr/sbin}" "${daemon_files[@]}"
  clean_bin "${files[@]}"
  stage_files "${unitdir}" "${SL_SLURM_PREFIX}/lib/systemd/system/${daemon}.service"
  if [[ "${daemon}" = slurmdbd ]]; then
    stage_files "${libdir}/slurm" "${SL_SLURM_PREFIX}/lib64/slurm/accounting_storage_mysql.so"
    clean_bin "${files[@]}"
  fi
  printf '#!/bin/sh\nif [ -d /run/systemd/system ]; then\n  systemctl daemon-reload || :\nfi\n' \
    > "${tmp}/${daemon}_reload"
  printf "#!/bin/sh\nif [ \"\$1\" = 0 ] && [ -d /run/systemd/system ]; then\n  systemctl --no-reload disable --now %s.service || :\nfi\n" \
    "${daemon}" > "${tmp}/${daemon}_remove"
  EXTRA_OPTS+=(--after-install "${tmp}/${daemon}_reload"
              --before-remove "${tmp}/${daemon}_remove"
              --after-remove "${tmp}/${daemon}_reload")
  DEPENDS=("${slurm_lib} = ${slurm_full}")
  EXTERNAL_DEPENDS=(systemd munge)
  build_package "slurm-${daemon}"
done

DESCRIPTION="Slurm API development files"
stage_files "${includedir}" "${SL_SLURM_PREFIX}/include/slurm"
stage_files "${libdir}/pkgconfig" "${SL_SLURM_PREFIX}/lib64/pkgconfig/slurm.pc"
DEPENDS=("${slurm_lib} = ${slurm_full}")
build_package "${slurm_dev}"

DESCRIPTION="Slurm example configuration files"
stage_files "${sysconfdir}/slurm" "${SL_SLURM_PREFIX}"/etc/slurm/*.example
for config in "${files[@]}"; do
  CONFIG_FILES+=("${config#"${tmp}"}")
done
ARCH=noarch
build_package slurm-example-configs