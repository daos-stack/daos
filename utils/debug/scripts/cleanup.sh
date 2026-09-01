#!/bin/bash

# set -x
set -uo pipefail

CWD="$(realpath "$(dirname "$0")")"

# ── Helpers ────────────────────────────────────────────────────────────────────────

section() { echo; echo ">>> $*"; }

# ── Init ───────────────────────────────────────────────────────────────────────────

source "$CWD/env.sh"

# ── Processes ──────────────────────────────────────────────────────────────────────

section "Cleaning up clients"
{
	cat <<-EOF
	# set -x
	set -euo pipefail

	for item in daos daos-serialize daos-deserialize mpirun dfuse ior mdtest fio test_group_np_srv test_group_np_cli orted ; do
		echo "Killing \$item processes"
		proc_name=\${item:0:15}
		pkill --echo --exact \$proc_name || true
		sleep 1
		if pgrep --exact \$proc_name ; then
			pkill --echo --exact --signal 9 \$proc_name || true
			sleep 3
		fi
	done
	for target in \$(findmnt -t fuse.daos --json | jq -r '.filesystems[] | .target') ; do
		echo "Unmounting dfuse target \$target"
		fusermount -z -u "\$target"
		rmdir "\$target"
	done
	EOF
} | clush -BLS -l root -w "$ALL_NODES" bash -s

# ── Stop Services ─────────────────────────────────────────────────────────────────

section "Stopping DAOS file system"
ssh "root@$ADMIN_NODE" dmg system stop --force || true

section "Stopping DAOS services"
clush -BLS -w "$SERVER_NODES" -l root systemctl stop daos_server
clush -BLS -w "$CLIENT_NODES" -l root systemctl stop daos_agent

# ── Unmount ────────────────────────────────────────────────────────────────────────

section "Unmounting DAOS mount points"
{
	cat <<-EOF
	# set -x
	set -euo pipefail

	for target in \$(findmnt -t tmpfs --list --json | jq -r '.filesystems[] | select(.target | test("^/[^/]+/daos[^/]*$")) | .target') ; do
		echo "Unmounting tmpfs \$target"
		umount "\$target"
	done
	for target in \$(findmnt -t ext4 --list --json | jq -r '.filesystems[] | select(.source | test("^/dev/pmem[0-9]+$")) | .target') ; do
		echo "Unmounting pmem \$target"
		umount "\$target"
	done
	for dev in \$(ndctl list | jq -r '.[] | .blockdev') ; do
		echo "Wiping file system of \$dev"
		wipefs -a "/dev/\$dev"
	done
	EOF
} | clush -BLS -l root -w "$SERVER_NODES" bash -s

# ── Shared Memory and Huge Pages ─────────────────────────────────────────────

section "Cleaning DAOS shared memories"
clush -BLS -w "$SERVER_NODES" sudo ipcrm --all=shm

section "Cleaning DAOS huge pages"
clush -BLS -w "$SERVER_NODES" bash -s <<<"sudo find /dev/hugepages -maxdepth 1 -type f -name 'spdk_*' -delete -printf '.' | wc -c"

# ── Metadata and Locks ───────────────────────────────────────────────────────

section "Cleaning DAOS control plane metadata"
clush -BLS -w "$SERVER_NODES" bash -s <<<"sudo rm -frv /var/daos/control_meta | wc -l"

section "Cleaning SPDK lock files"
clush -BLS -w "$SERVER_NODES" bash -s <<<"sudo find /var/tmp -maxdepth 1 -type f -name 'spdk_pci_lock_*' -delete -printf '.' | wc -c"

section "Cleaning DPDK runtime directory"
{
	cat <<-'EOFSH'
	rm -rf /tmp/dpdk
	mkdir -p /tmp/dpdk
	chmod 777 /tmp/dpdk
	EOFSH
} | clush -BLS -l root -w "$SERVER_NODES" bash -s

section "Cleaning DAOS ftest workspace"
clush -BLS -w "$SERVER_NODES" bash -s <<<"sudo rm -frv /var/tmp/daos_testing | wc -l"

# ── Monitor and Profiling ─────────────────────────────────────────────────────

section "Cleaning DAOS monitor"
{
	cat <<-EOF
	# set -x

	if [[ -d /var/daos/daos-monitor ]] ; then
		bash /var/daos/daos-monitor/bin/daos-monitor-stop.sh
	fi
	pkill -f --signal 9 daos-monitor- || true
	rm -frv /var/daos/daos-monitor | wc -l
	EOF
} | clush -BLS -l root -w "$ALL_NODES" bash -s

section "Cleaning heap profiling"
{
	cat <<-EOF
	# set -x

	rm -frv /var/daos/hprof | wc -l
	EOF
} | clush -BLS -l root -w "$ALL_NODES" bash -s

# ── TLS Certificates ──────────────────────────────────────────────────────────

section "Removing TLS certificates from nodes"
{
	cat <<-'EOFSH'
	rm -fv /etc/daos/certs/server.crt /etc/daos/certs/server.key
	rm -fv /etc/daos/certs/daosCA.crt
	rm -fv /etc/daos/certs/clients/agent.crt /etc/daos/certs/clients/admin.crt
	EOFSH
} | clush -BLS -l root -w "$SERVER_NODES" bash -s

{
	cat <<-'EOFSH'
	rm -fv /etc/daos/certs/agent.crt /etc/daos/certs/agent.key
	rm -fv /etc/daos/certs/admin.crt /etc/daos/certs/admin.key
	rm -fv /etc/daos/certs/daosCA.crt
	EOFSH
} | clush -BLS -l root -w "$CLIENT_NODES" bash -s

# ── System Library Cache ──────────────────────────────────────────────────────

section "Removing DAOS ASAN ldconfig configuration"
{
	cat <<-'EOFSH'
	rm -f /etc/ld.so.conf.d/daos-asan.conf
	ldconfig
	EOFSH
} | clush -BLS -l root -w "$SERVER_NODES" bash -s

# ── Logs ──────────────────────────────────────────────────────────────────────────

section "Cleaning DAOS logs"
{
	cat <<-EOF
	{
		sudo find /var/daos -maxdepth 1 -type f -name "daos_*.log*" -print -delete
		sudo find /var/daos -maxdepth 1 -type f -name "daos_*.asan.*" -print -delete
		find /var/daos -maxdepth 1 -type f '(' -name "dnt_*" -o -name "vgdb-pipe-*" ')' -print -delete
		find /var/daos -maxdepth 1 -type p -name "vgdb-pipe*" -print -delete
		find /var/daos -maxdepth 1 -type d -name "dnt_*" -print -exec rm -fr {} ';'
		[ -d "$DAOS_SRC" ] && find "$DAOS_SRC" -maxdepth 1 -type f \( -name "nlt-*" -o -name "dnt.*.xml" \) -print -delete || true
	} | wc -l
	EOF
} | clush -BLS -w "$ALL_NODES" bash -s
