#!/bin/bash
# setup-docker_storage.sh
#
# Configures a dedicated NVMe device for Docker and containerd storage.
#   - Formats and mounts the NVMe device (no 'discard' mount option)
#   - Enables fstrim.timer for weekly periodic TRIM (better than online discard)
#   - Installs a udev rule to persist the 'none' I/O scheduler for NVMe
#   - Updates /etc/fstab for persistent mounting
#   - Updates /etc/docker/daemon.json  (Docker data-root)
#   - Updates /etc/containerd/config.toml (containerd root)
#   - Migrates existing containerd data to the new location
#
# Usage:
#   sudo ./setup-docker_storage.sh [OPTIONS]
#
# Options:
#   -d <device>     NVMe block device to use          (default: /dev/nvme0n1)
#   -m <mountpoint> Filesystem mount point             (default: /opt)
#   -c <path>       Containerd root directory          (default: /opt/containerd)
#   -k <path>       Docker data-root directory         (default: /opt/docker)
#   -f <fstype>     Filesystem type                    (default: ext4)
#   -o <options>    Mount options for fstab            (default: defaults,noatime,nodiratime,nobarrier,errors=remount-ro)
#   -F              Format the device  WARNING: destroys all data
#   -n              Dry-run: show what would be done without making changes
#   -h              Show this help message
#
# Notes:
#   The 'discard' mount option is intentionally NOT used. Periodic TRIM via
#   fstrim.timer is preferred for NVMe devices as it avoids the latency penalty
#   of synchronous per-delete TRIM operations.
#   The 'none' I/O scheduler is set via udev for NVMe devices because the
#   drive's internal queue already provides optimal scheduling.

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
DEFAULT_DEVICE="/dev/nvme0n1"
DEFAULT_MOUNTPOINT="/opt"
DEFAULT_CONTAINERD_ROOT="/opt/containerd"
DEFAULT_DOCKER_ROOT="/opt/docker"
DEFAULT_FSTYPE="ext4"
DEFAULT_MOUNT_OPTS="defaults,noatime,nodiratime,nobarrier,errors=remount-ro"

DEVICE="$DEFAULT_DEVICE"
MOUNTPOINT="$DEFAULT_MOUNTPOINT"
CONTAINERD_ROOT="$DEFAULT_CONTAINERD_ROOT"
DOCKER_ROOT="$DEFAULT_DOCKER_ROOT"
FSTYPE="$DEFAULT_FSTYPE"
MOUNT_OPTS="$DEFAULT_MOUNT_OPTS"
FORMAT=false
DRY_RUN=false

CONTAINERD_CONFIG="/etc/containerd/config.toml"
DOCKER_CONFIG="/etc/docker/daemon.json"
FSTAB="/etc/fstab"
UDEV_RULES_FILE="/etc/udev/rules.d/60-nvme-scheduler.rules"

# ── Helpers ───────────────────────────────────────────────────────────────────
info()  { echo -e "\e[32m[INFO]\e[0m  $*"; }
warn()  { echo -e "\e[33m[WARN]\e[0m  $*"; }
error() { echo -e "\e[31m[ERROR]\e[0m $*" >&2; exit 1; }
dry()   { echo -e "\e[36m[DRY-RUN]\e[0m $*"; }

run() {
    if $DRY_RUN; then dry "$*"; else eval "$*"; fi
}

usage() {
    grep '^#' "$0" | grep -v '#!/' | sed 's/^# \?//'
    exit 0
}

# ── Argument parsing ──────────────────────────────────────────────────────────
while getopts "d:m:c:k:f:o:Fnh" opt; do
    case $opt in
        d) DEVICE="$OPTARG" ;;
        m) MOUNTPOINT="$OPTARG" ;;
        c) CONTAINERD_ROOT="$OPTARG" ;;
        k) DOCKER_ROOT="$OPTARG" ;;
        f) FSTYPE="$OPTARG" ;;
        o) MOUNT_OPTS="$OPTARG" ;;
        F) FORMAT=true ;;
        n) DRY_RUN=true ;;
        h) usage ;;
        *) error "Unknown option. Use -h for help." ;;
    esac
done

# ── Pre-flight checks ─────────────────────────────────────────────────────────
[[ $EUID -ne 0 ]] && ! $DRY_RUN && error "This script must be run as root (use sudo)."
[[ ! -b "$DEVICE" ]] && ! $DRY_RUN && error "Block device not found: $DEVICE"
command -v rsync  &>/dev/null || error "'rsync' is required. Run: apt install rsync"
command -v docker &>/dev/null || error "'docker' is not installed or not in PATH."

# Derive the kernel device name (e.g. nvme0n1) from the device path
DEVNAME=$(basename "$DEVICE")

# ── Summary ───────────────────────────────────────────────────────────────────
echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Docker / Containerd Storage Setup"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  %-26s %s\n" "NVMe device:"         "$DEVICE"
printf "  %-26s %s\n" "Mount point:"         "$MOUNTPOINT"
printf "  %-26s %s\n" "Filesystem:"          "$FSTYPE"
printf "  %-26s %s\n" "Mount options:"       "$MOUNT_OPTS"
printf "  %-26s %s\n" "Containerd root:"     "$CONTAINERD_ROOT"
printf "  %-26s %s\n" "Docker data-root:"    "$DOCKER_ROOT"
printf "  %-26s %s\n" "Format device:"       "$FORMAT"
printf "  %-26s %s\n" "Dry-run:"             "$DRY_RUN"
printf "  %-26s %s\n" "TRIM strategy:"       "fstrim.timer (weekly, no online discard)"
printf "  %-26s %s\n" "I/O scheduler:"       "none (via udev: $UDEV_RULES_FILE)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if ! $DRY_RUN; then
    read -rp "Proceed? [y/N] " CONFIRM
    [[ "${CONFIRM,,}" != "y" ]] && { info "Aborted."; exit 0; }
fi

# ── Step 1: Format device (optional) ─────────────────────────────────────────
if $FORMAT; then
    warn "Formatting $DEVICE as $FSTYPE — ALL DATA WILL BE LOST!"
    if ! $DRY_RUN; then
        read -rp "Are you sure? Type 'yes' to confirm: " FCONFIRM
        [[ "$FCONFIRM" != "yes" ]] && { info "Aborted."; exit 0; }
    fi
    run "mkfs.$FSTYPE -F $DEVICE"
    info "Device formatted."
fi

# ── Step 2: Mount device ──────────────────────────────────────────────────────
if mountpoint -q "$MOUNTPOINT" 2>/dev/null; then
    info "$MOUNTPOINT is already mounted — skipping mount."
else
    info "Mounting $DEVICE on $MOUNTPOINT..."
    run "mkdir -p $MOUNTPOINT"
    run "mount -t $FSTYPE -o $MOUNT_OPTS $DEVICE $MOUNTPOINT"
    info "Device mounted."
fi

# ── Step 3: Update /etc/fstab ─────────────────────────────────────────────────
if grep -qE "^[^#]*[[:blank:]]$MOUNTPOINT[[:blank:]]" "$FSTAB" 2>/dev/null; then
    info "$MOUNTPOINT already present in $FSTAB — skipping."
else
    info "Adding $DEVICE entry to $FSTAB..."
    # Prefer UUID for robustness
    UUID=$(blkid -s UUID -o value "$DEVICE" 2>/dev/null || true)
    if [[ -n "$UUID" ]]; then
        FSTAB_ENTRY="UUID=$UUID  $MOUNTPOINT  $FSTYPE  $MOUNT_OPTS  0  2"
    else
        FSTAB_ENTRY="$DEVICE  $MOUNTPOINT  $FSTYPE  $MOUNT_OPTS  0  2"
    fi
    if $DRY_RUN; then
        dry "Append to $FSTAB: $FSTAB_ENTRY"
    else
        cp "$FSTAB" "${FSTAB}.bak"
        echo "$FSTAB_ENTRY" >> "$FSTAB"
        info "fstab updated (backup: ${FSTAB}.bak)."
    fi
fi

# ── Step 4: NVMe I/O scheduler — set to 'none' via udev ──────────────────────
# 'none' bypasses the kernel I/O scheduler; the NVMe drive handles its own
# internal queue far more efficiently than any software scheduler.
info "Configuring I/O scheduler for NVMe devices..."
UDEV_RULE='ACTION=="add|change", KERNEL=="nvme[0-9]*", ATTR{queue/scheduler}="none"'
if [[ -f "$UDEV_RULES_FILE" ]] && grep -qF 'nvme' "$UDEV_RULES_FILE" 2>/dev/null; then
    info "udev NVMe scheduler rule already exists — skipping."
else
    if $DRY_RUN; then
        dry "Create $UDEV_RULES_FILE with: $UDEV_RULE"
    else
        echo "$UDEV_RULE" > "$UDEV_RULES_FILE"
        udevadm control --reload-rules
        udevadm trigger --attr-match=subsystem=block
        info "udev rule created: $UDEV_RULES_FILE"
    fi
fi

# Apply immediately to the current device (survives next boot via udev rule)
CURRENT_SCHED=$(cat "/sys/block/$DEVNAME/queue/scheduler" 2>/dev/null || echo "unknown")
if [[ "$CURRENT_SCHED" != *"[none]"* ]]; then
    info "Applying 'none' scheduler to $DEVICE immediately..."
    run "echo none > /sys/block/$DEVNAME/queue/scheduler"
else
    info "I/O scheduler already set to 'none' for $DEVICE."
fi

# ── Step 5: Enable fstrim.timer (periodic weekly TRIM) ────────────────────────
# Preferred over the 'discard' mount option which performs synchronous TRIM on
# every block deletion, adding latency to write operations.
info "Enabling fstrim.timer for periodic TRIM..."
if systemctl is-enabled --quiet fstrim.timer 2>/dev/null; then
    info "fstrim.timer is already enabled."
else
    run "systemctl enable --now fstrim.timer"
    info "fstrim.timer enabled and started."
fi

# ── Step 6: Stop Docker and containerd ───────────────────────────────────────
info "Stopping Docker and containerd..."
run "systemctl stop docker docker.socket 2>/dev/null || true"
run "systemctl stop containerd"
info "Services stopped."

# ── Step 7: Migrate containerd data ──────────────────────────────────────────
OLD_CONTAINERD_ROOT=$(grep -E '^root\s*=' "$CONTAINERD_CONFIG" 2>/dev/null \
    | sed 's/root\s*=\s*//;s/"//g;s/ //g' \
    || echo "/var/lib/containerd")

if [[ -d "$OLD_CONTAINERD_ROOT" && "$OLD_CONTAINERD_ROOT" != "$CONTAINERD_ROOT" ]]; then
    DATA_SIZE=$(du -sh "$OLD_CONTAINERD_ROOT" 2>/dev/null | cut -f1)
    info "Migrating containerd data: $OLD_CONTAINERD_ROOT -> $CONTAINERD_ROOT ($DATA_SIZE)"

    REQUIRED=$(du -sb "$OLD_CONTAINERD_ROOT" | awk '{print $1}')
    AVAILABLE=$(df --output=avail -B1 "$MOUNTPOINT" | tail -1)
    if ! $DRY_RUN && (( REQUIRED > AVAILABLE )); then
        error "Not enough space on $MOUNTPOINT. Required: $(numfmt --to=iec $REQUIRED), Available: $(numfmt --to=iec $AVAILABLE)"
    fi

    run "mkdir -p $CONTAINERD_ROOT"
    run "rsync -aP $OLD_CONTAINERD_ROOT/ $CONTAINERD_ROOT/"

    if ! $DRY_RUN; then
        SRC=$(du -sb "$OLD_CONTAINERD_ROOT" | awk '{print $1}')
        DST=$(du -sb "$CONTAINERD_ROOT"     | awk '{print $1}')
        (( DST < SRC * 95 / 100 )) && \
            error "rsync size mismatch. Source: $(numfmt --to=iec $SRC), Dest: $(numfmt --to=iec $DST). Aborting."
        info "Data migrated successfully ($(du -sh "$CONTAINERD_ROOT" | cut -f1))."
    fi
else
    info "Containerd root already at $CONTAINERD_ROOT or source not found — skipping migration."
    run "mkdir -p $CONTAINERD_ROOT"
fi

# ── Step 8: Update /etc/containerd/config.toml ───────────────────────────────
info "Updating $CONTAINERD_CONFIG..."
if $DRY_RUN; then
    dry "Set root = \"$CONTAINERD_ROOT\" in $CONTAINERD_CONFIG"
else
    cp "$CONTAINERD_CONFIG" "${CONTAINERD_CONFIG}.bak"
    if grep -qE '^#?root\s*=' "$CONTAINERD_CONFIG"; then
        sed -i "s|^#\?root\s*=.*|root = \"$CONTAINERD_ROOT\"|" "$CONTAINERD_CONFIG"
    else
        echo "root = \"$CONTAINERD_ROOT\"" >> "$CONTAINERD_CONFIG"
    fi
    info "containerd config updated (backup: ${CONTAINERD_CONFIG}.bak)."
fi

# ── Step 9: Update /etc/docker/daemon.json ───────────────────────────────────
info "Updating $DOCKER_CONFIG..."
if $DRY_RUN; then
    dry "Set data-root = \"$DOCKER_ROOT\" in $DOCKER_CONFIG"
else
    mkdir -p "$(dirname "$DOCKER_CONFIG")"
    if [[ -f "$DOCKER_CONFIG" ]]; then
        cp "$DOCKER_CONFIG" "${DOCKER_CONFIG}.bak"
    fi
    python3 - <<EOF
import json, os
path = "$DOCKER_CONFIG"
cfg = {}
if os.path.exists(path):
    with open(path) as f:
        cfg = json.load(f)
cfg["data-root"] = "$DOCKER_ROOT"
with open(path, "w") as f:
    json.dump(cfg, f, indent="\t")
    f.write("\n")
EOF
    info "Docker config updated (backup: ${DOCKER_CONFIG}.bak)."
fi

# ── Step 10: Create Docker root and restart services ─────────────────────────
run "mkdir -p $DOCKER_ROOT"

info "Restarting containerd and Docker..."
run "systemctl start containerd"
run "systemctl start docker"

if ! $DRY_RUN; then
    sleep 3
    systemctl is-active --quiet containerd || error "containerd failed to start!"
    systemctl is-active --quiet docker     || error "Docker failed to start!"
    info "Services running."

    IMAGE_COUNT=$(docker images -q | wc -l)
    info "Docker is healthy. Images accessible: $IMAGE_COUNT"
    docker info | grep -E "Docker Root Dir|Storage Driver" | sed 's/^/  /'
fi

# ── Step 11: Remove old containerd data (optional) ────────────────────────────
if [[ -d "${OLD_CONTAINERD_ROOT:-}" && "${OLD_CONTAINERD_ROOT:-}" != "$CONTAINERD_ROOT" ]] && ! $DRY_RUN; then
    echo
    read -rp "Remove old containerd data at $OLD_CONTAINERD_ROOT? [y/N] " CLEANUP
    if [[ "${CLEANUP,,}" == "y" ]]; then
        rm -rf "$OLD_CONTAINERD_ROOT"
        info "Old data removed."
    else
        warn "Old data kept at $OLD_CONTAINERD_ROOT. Remove manually when satisfied."
    fi
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo
info "━━━ Setup complete ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
info "  NVMe device      : $DEVICE  ->  $MOUNTPOINT"
info "  I/O scheduler    : none  (udev: $UDEV_RULES_FILE)"
info "  TRIM             : fstrim.timer (weekly periodic)"
info "  Containerd root  : $CONTAINERD_ROOT"
info "  Docker data-root : $DOCKER_ROOT"
if ! $DRY_RUN; then
    info "  Root disk usage  : $(df -h / | awk 'NR==2 {print $3" used, "$4" free ("$5")"}')"
fi
info "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
