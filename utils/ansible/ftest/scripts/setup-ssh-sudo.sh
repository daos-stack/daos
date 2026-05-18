#!/bin/bash
# Pre-configure SSH key authentication and passwordless sudo for ansible target nodes.
#
# This script is a prerequisite to running the ftest.yml ansible playbook on a
# fresh cluster where only password-based access is available.
#
# Usage:
#   setup-ssh-sudo.sh -w NODESET [-u USERNAME] [-p USER_PASS] [-r ROOT_PASS] [-k KEY]
#   setup-ssh-sudo.sh -i INVENTORY [-u USERNAME] [-p USER_PASS] [-r ROOT_PASS] [-k KEY]
#
# Requirements: sshpass, ssh-copy-id
# Optional:     nodeset (clustershell) for nodeset notation in -w

set -euo pipefail

SSH_KEY="${HOME}/.ssh/id_ed25519"
NODESET=""
INVENTORY=""
USERNAME="${USER:-$(whoami)}"
USER_PASS=""
ROOT_PASS=""

# ── Helpers ────────────────────────────────────────────────────────────────────

usage() {
    cat <<-EOF
	Usage: $(basename "$0") [OPTIONS]

	Pre-configure SSH key authentication and passwordless sudo on target nodes
	before running the DAOS ftest ansible playbook.

	Options:
	  -w NODESET    Target nodes in ClusterShell notation (e.g. "host[1-4]")
	  -i INVENTORY  Ansible inventory file (hosts extracted automatically)
	  -u USERNAME   Ansible user account on target nodes (default: current user)
	  -p USER_PASS  User password (used by sshpass for SSH key copy)
	  -r ROOT_PASS  Root password (used to configure passwordless sudo via root SSH)
	  -k SSH_KEY    SSH private key path (default: ~/.ssh/id_ed25519)
	  -h            Show this help message

	Examples:
	  $(basename "$0") -w "brd-[216-219]" -u alice -p S3cr3t -r R00tPass
	  $(basename "$0") -i inventory.yml   -u alice -p S3cr3t -r R00tPass
	  $(basename "$0") -i inventory.yml   -u alice -k ~/.ssh/id_rsa   # key already copied
	EOF
}

info()  { echo "[INFO] $*"; }
warn()  { echo "[WARN] $*"; }
err()   { echo "[ERROR] $*" >&2; }
die()   { err "$1"; exit "${2:-1}"; }

# ── Argument parsing ──────────────────────────────────────────────────────────

while getopts "w:i:u:p:r:k:h" opt; do
    case "$opt" in
        w) NODESET="$OPTARG" ;;
        i) INVENTORY="$OPTARG" ;;
        u) USERNAME="$OPTARG" ;;
        p) USER_PASS="$OPTARG" ;;
        r) ROOT_PASS="$OPTARG" ;;
        k) SSH_KEY="$OPTARG" ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

[[ -z "$NODESET" && -z "$INVENTORY" ]] && die "Specify target hosts with -w NODESET or -i INVENTORY"
[[ -n "$NODESET" && -n "$INVENTORY" ]] && die "Specify either -w or -i, not both"

# ── Build host list ───────────────────────────────────────────────────────────

declare -a HOSTS=()

if [[ -n "$INVENTORY" ]]; then
    [[ -f "$INVENTORY" ]] || die "Inventory file not found: $INVENTORY"
    command -v ansible-inventory &>/dev/null \
        || die "ansible-inventory not found — install ansible or use -w NODESET"
    mapfile -t HOSTS < <(
        ansible-inventory -i "$INVENTORY" --list 2>/dev/null \
        | python3 -c "
import json, sys
data = json.load(sys.stdin)
hosts = set()
for key, val in data.items():
    if key == '_meta':
        hosts.update(val.get('hostvars', {}).keys())
    elif isinstance(val, dict) and 'hosts' in val:
        hosts.update(val['hosts'])
for h in sorted(hosts):
    print(h)
"
    )
elif [[ -n "$NODESET" ]]; then
    if command -v nodeset &>/dev/null; then
        read -r -a HOSTS <<< "$(nodeset -e "$NODESET")"
    else
        warn "nodeset (clustershell) not found — treating -w value as comma-separated list"
        IFS=',' read -ra HOSTS <<< "$NODESET"
    fi
fi

[[ ${#HOSTS[@]} -eq 0 ]] && die "No hosts found"
info "Target hosts: ${HOSTS[*]}"

# ── Generate SSH key ──────────────────────────────────────────────────────────

if [[ ! -f "$SSH_KEY" ]]; then
    info "Generating SSH key: $SSH_KEY"
    ssh-keygen -t ed25519 -f "$SSH_KEY" -N ""
fi
SSH_PUB="${SSH_KEY}.pub"
[[ -f "$SSH_PUB" ]] || die "SSH public key not found: $SSH_PUB"

# ── Step 1: SSH key copy ──────────────────────────────────────────────────────

echo
info "==> Step 1: SSH key authentication for user ${USERNAME}"
for host in "${HOSTS[@]}"; do
    printf "[INFO]   %-20s " "${host}:"
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "${USERNAME}@${host}" true 2>/dev/null; then
        echo "already configured (key auth works)"
    elif [[ -n "$USER_PASS" ]]; then
        if sshpass -p "$USER_PASS" \
                ssh-copy-id -o StrictHostKeyChecking=no -i "$SSH_PUB" \
                "${USERNAME}@${host}" 2>/dev/null; then
            echo "SSH key copied"
        else
            warn "ssh-copy-id failed for ${host}"
        fi
    else
        warn "key auth not configured and no -p USER_PASS provided — skipping ${host}"
    fi
done

# ── Step 2: Root SSH key authorization ───────────────────────────────────────

SSH_PUB_CONTENT="$(cat "$SSH_PUB")"
# Remote command to add the key: reads key from stdin, no embedded key content.
ROOT_ADD_CMD="mkdir -p /root/.ssh && chmod 700 /root/.ssh && \
    cat >> /root/.ssh/authorized_keys && chmod 600 /root/.ssh/authorized_keys"
# Check command: key has no single quotes (base64+hostname), so this quoting is safe.
ROOT_CHECK_CMD="grep -qxF '${SSH_PUB_CONTENT}' /root/.ssh/authorized_keys 2>/dev/null"

echo
info "==> Step 2: Adding SSH public key to root's authorized_keys"
for host in "${HOSTS[@]}"; do
    printf "[INFO]   %-20s " "${host}:"
    _root_done=false

    # Try 1: root key auth already works
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "root@${host}" true 2>/dev/null; then
        if ssh -o BatchMode=yes "root@${host}" "${ROOT_CHECK_CMD}" 2>/dev/null; then
            echo "root authorized_keys already present (root key auth)"
        elif printf '%s\n' "${SSH_PUB_CONTENT}" | \
                ssh -o BatchMode=yes "root@${host}" "${ROOT_ADD_CMD}" 2>/dev/null; then
            echo "root authorized_keys updated (root key auth)"
        else
            warn "failed to update root authorized_keys on ${host}"
        fi
        _root_done=true
    fi

    # Try 2: root password via sshpass
    if ! $_root_done && [[ -n "$ROOT_PASS" ]]; then
        if ! command -v sshpass &>/dev/null; then
            warn "sshpass not found — cannot use root password; falling back to sudo"
        elif sshpass -p "$ROOT_PASS" \
                ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 \
                "root@${host}" "${ROOT_CHECK_CMD}" 2>/dev/null; then
            echo "root authorized_keys already present (root password)"
            _root_done=true
        elif printf '%s\n' "${SSH_PUB_CONTENT}" | \
                sshpass -p "$ROOT_PASS" \
                ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 \
                "root@${host}" "${ROOT_ADD_CMD}" 2>/dev/null; then
            echo "root authorized_keys updated via root password"
            _root_done=true
        else
            warn "root password SSH failed for ${host} — falling back to sudo"
        fi
    fi

    # Try 3: user passwordless sudo — pipe key via stdin to avoid quoting issues
    if ! $_root_done; then
        if ssh -o BatchMode=yes -o ConnectTimeout=5 "${USERNAME}@${host}" \
                "sudo -n ${ROOT_CHECK_CMD}" 2>/dev/null; then
            echo "root authorized_keys already present (user sudo)"
            _root_done=true
        elif printf '%s\n' "${SSH_PUB_CONTENT}" | \
                ssh -o BatchMode=yes -o ConnectTimeout=5 "${USERNAME}@${host}" \
                "sudo -n bash -c '${ROOT_ADD_CMD}'" 2>/dev/null; then
            echo "root authorized_keys updated via user sudo (passwordless)"
            _root_done=true
        fi
    fi

    # Try 4: user sudo with password
    if ! $_root_done && [[ -n "$USER_PASS" ]]; then
        if ssh -o BatchMode=yes "${USERNAME}@${host}" \
                "echo '${USER_PASS}' | sudo -S ${ROOT_CHECK_CMD}" 2>/dev/null; then
            echo "root authorized_keys already present (user sudo+password)"
            _root_done=true
        elif printf '%s\n' "${SSH_PUB_CONTENT}" | \
                ssh -o BatchMode=yes "${USERNAME}@${host}" \
                "echo '${USER_PASS}' | sudo -S bash -c '${ROOT_ADD_CMD}'" 2>/dev/null; then
            echo "root authorized_keys updated via user sudo (password)"
            _root_done=true
        fi
    fi

    if ! $_root_done; then
        warn "cannot update root authorized_keys on ${host} — provide -r ROOT_PASS or -p USER_PASS"
    fi
done

# ── Step 3: Passwordless sudo ─────────────────────────────────────────────────

SUDOERS_LINE="${USERNAME} ALL=(ALL) NOPASSWD: ALL"
SUDOERS_FILE="/etc/sudoers.d/${USERNAME}"

echo
info "==> Step 3: Passwordless sudo for user ${USERNAME}"
for host in "${HOSTS[@]}"; do
    printf "[INFO]   %-20s " "${host}:"
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "${USERNAME}@${host}" \
            "sudo -n true" 2>/dev/null; then
        echo "already configured (NOPASSWD works)"
        continue
    fi

    if [[ -n "$ROOT_PASS" ]]; then
        if sshpass -p "$ROOT_PASS" \
                ssh -o StrictHostKeyChecking=no "root@${host}" \
                "echo '${SUDOERS_LINE}' > ${SUDOERS_FILE} && chmod 600 ${SUDOERS_FILE}" \
                2>/dev/null; then
            echo "sudoers configured via root"
        else
            warn "root SSH failed for ${host}"
        fi
    elif [[ -n "$USER_PASS" ]]; then
        if ssh -o BatchMode=yes "${USERNAME}@${host}" \
                "echo '${USER_PASS}' | sudo -S bash -c \
                'echo \"${SUDOERS_LINE}\" > ${SUDOERS_FILE} && chmod 600 ${SUDOERS_FILE}'" \
                2>/dev/null; then
            echo "sudoers configured via user sudo"
        else
            warn "sudo configuration failed for ${host}"
        fi
    else
        warn "cannot configure sudo — provide -r ROOT_PASS or -p USER_PASS"
    fi
done

# ── Step 4: Verify ────────────────────────────────────────────────────────────

echo
info "==> Step 4: Verifying SSH and sudo on all nodes"
ERRORS=0
for host in "${HOSTS[@]}"; do
    printf "[INFO]   %-20s " "${host}:"
    SSH_OK=false
    SUDO_OK=false
    ssh -o BatchMode=yes -o ConnectTimeout=5 \
        "${USERNAME}@${host}" true 2>/dev/null && SSH_OK=true
    ssh -o BatchMode=yes -o ConnectTimeout=5 \
        "${USERNAME}@${host}" "sudo -n true" 2>/dev/null && SUDO_OK=true

    if $SSH_OK && $SUDO_OK; then
        echo "OK (ssh=✓  sudo=✓)"
    else
        echo "FAILED (ssh=${SSH_OK}  sudo=${SUDO_OK})"
        ERRORS=$((ERRORS + 1))
    fi
done

echo
if [[ $ERRORS -eq 0 ]]; then
    info "All ${#HOSTS[@]} node(s) configured successfully."
    info "You can now run the ansible playbook:"
    info "  ansible-playbook -i INVENTORY ftest.yml"
else
    die "${ERRORS} node(s) failed verification — check warnings above" 1
fi
