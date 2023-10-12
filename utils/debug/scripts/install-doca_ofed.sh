#!/bin/bash
# =============================================================================
# Script    : install_doca_ofed.sh
# Purpose   : Install NVIDIA DOCA-OFED 3.3.0 on Ubuntu 24.04 (online method)
# Usage     : sudo ./install_doca_ofed.sh
# =============================================================================

set -euo pipefail

# -----------------------------------------------------------------------------
# CONFIGURATION
# -----------------------------------------------------------------------------
DOCA_VERSION="3.3.0"
UBUNTU_VERSION="ubuntu24.04"
ARCH="x86_64"
DOCA_REPO_URL="https://linux.mellanox.com/public/repo/doca/${DOCA_VERSION}/${UBUNTU_VERSION}/${ARCH}/"
DOCA_GPG_URL="https://linux.mellanox.com/public/repo/doca/GPG-KEY-Mellanox.pub"
DOCA_GPG_DEST="/etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.gpg"
DOCA_LIST="/etc/apt/sources.list.d/doca.list"
VERIFY_SCRIPT="verify_doca_ofed.sh"

# Temporary file to capture apt install output for post-install analysis
INSTALL_LOG=$(mktemp /tmp/doca_ofed_install_XXXXXX.log)
trap 'rm -f "${INSTALL_LOG}"' EXIT

# -----------------------------------------------------------------------------
# COLORS
# -----------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
log_info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $*"; }
log_notice()  { echo -e "${CYAN}[NOTE]${NC}  $*"; }
log_section() { echo -e "\n${BLUE}=== $* ===${NC}"; }

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)."
        exit 1
    fi
}

check_os() {
    log_section "Checking Operating System"
    if [[ ! -f /etc/os-release ]]; then
        log_error "Cannot determine OS. /etc/os-release not found."
        exit 1
    fi
    source /etc/os-release
    if [[ "$ID" != "ubuntu" || "$VERSION_ID" != "24.04" ]]; then
        log_error "This script requires Ubuntu 24.04. Detected: ${PRETTY_NAME}"
        exit 1
    fi
    log_info "OS check passed: ${PRETTY_NAME}"
}

check_kernel() {
    log_section "Checking Kernel Version"
    KERNEL=$(uname -r)
    KERNEL_MAJOR=$(echo "$KERNEL" | cut -d. -f1)
    KERNEL_MINOR=$(echo "$KERNEL" | cut -d. -f2)
    log_info "Running kernel: ${KERNEL}"
    if [[ "$KERNEL_MAJOR" -lt 6 ]] || \
       { [[ "$KERNEL_MAJOR" -eq 6 ]] && [[ "$KERNEL_MINOR" -lt 8 ]]; }; then
        log_error "Kernel ${KERNEL} is below the minimum required 6.8.x for DOCA-OFED ${DOCA_VERSION}."
        exit 1
    fi
    log_info "Kernel check passed: ${KERNEL}"
}

check_internet() {
    log_section "Checking Internet Connectivity"
    if ! curl -sf --max-time 10 "https://linux.mellanox.com" > /dev/null 2>&1; then
        log_error "Cannot reach linux.mellanox.com. Please check your internet connection."
        exit 1
    fi
    log_info "Internet connectivity: OK"
}

# -----------------------------------------------------------------------------
# Fix: capture lspci output into a variable with || true to avoid
# set -euo pipefail triggering on grep returning exit code 1 (no match).
# This also avoids running lspci twice.
# -----------------------------------------------------------------------------
check_mellanox_hardware() {
    log_section "Checking Mellanox Hardware Presence"

    MELLANOX_DEVICES=$(lspci 2>/dev/null | grep -i mellanox || true)

    if [[ -z "$MELLANOX_DEVICES" ]]; then
        log_error "No Mellanox/NVIDIA NIC detected via lspci. Aborting installation."
        exit 1
    fi

    NIC_COUNT=$(echo "$MELLANOX_DEVICES" | wc -l)
    log_info "Detected ${NIC_COUNT} Mellanox device(s):"
    echo "$MELLANOX_DEVICES" | while read -r line; do
        log_info "  ${line}"
    done
}

# -----------------------------------------------------------------------------
# STEP 1 — Prerequisites
# -----------------------------------------------------------------------------
install_prerequisites() {
    log_section "Step 1 — Installing Prerequisites"
    apt-get update
    apt-get install -y \
        curl \
        gnupg \
        linux-headers-"$(uname -r)" \
        build-essential \
        dkms \
        dracut
    log_info "Prerequisites installed."
}

# -----------------------------------------------------------------------------
# STEP 2 — Remove previous OFED/DOCA installation
# -----------------------------------------------------------------------------
remove_previous_installation() {
    log_section "Step 2 — Removing Previous OFED/DOCA Installation (if any)"

    DOCA_PKGS=$(dpkg --list 2>/dev/null \
        | grep -E 'doca|flexio|dpa-gdbserver|dpa-stats|dpaeumgmt|dpdk-community' \
        | awk '{print $2}' || true)

    if [[ -n "$DOCA_PKGS" ]]; then
        log_warn "Found existing DOCA packages — removing them:"
        echo "$DOCA_PKGS" | while read -r pkg; do
            log_warn "  Removing: ${pkg}"
        done
        echo "$DOCA_PKGS" | xargs apt-get remove --purge -y
        apt-get autoremove -y
    else
        log_info "No existing DOCA packages found."
    fi

    if [[ -f /usr/sbin/ofed_uninstall.sh ]]; then
        log_warn "Found previous OFED installation — running ofed_uninstall.sh"
        /usr/sbin/ofed_uninstall.sh --force || true
    else
        log_info "No previous OFED installation found."
    fi
}

# -----------------------------------------------------------------------------
# STEP 3 — Add NVIDIA DOCA Repository
# -----------------------------------------------------------------------------
add_doca_repository() {
    log_section "Step 3 — Adding NVIDIA DOCA ${DOCA_VERSION} Repository"

    log_info "Importing NVIDIA GPG key..."
    curl -fsSL "${DOCA_GPG_URL}" | gpg --dearmor -o "${DOCA_GPG_DEST}"
    chmod 644 "${DOCA_GPG_DEST}"
    log_info "GPG key saved to ${DOCA_GPG_DEST}"

    log_info "Adding DOCA repository to apt sources..."
    echo "deb [signed-by=${DOCA_GPG_DEST}] ${DOCA_REPO_URL} ./" > "${DOCA_LIST}"
    log_info "Repository file created: ${DOCA_LIST}"

    log_info "Updating apt package index..."
    apt-get update
    log_info "Repository added successfully."
}

# -----------------------------------------------------------------------------
# STEP 4 — Install DOCA-OFED
# Capture full output to a temp log file so we can analyze it afterwards.
# -----------------------------------------------------------------------------
install_doca_ofed() {
    log_section "Step 4 — Installing DOCA-OFED ${DOCA_VERSION}"

    log_info "Running apt-get install — output is captured for post-install analysis..."
    if ! apt-get install -y doca-ofed 2>&1 | tee "${INSTALL_LOG}"; then
        log_error "apt-get install doca-ofed failed. Check the output above."
        exit 1
    fi
    log_info "DOCA-OFED ${DOCA_VERSION} package installation completed."

    log_info "Installing mlnx-fw-updater (firmware update utility)..."
    apt-get install -y mlnx-fw-updater 2>&1 | tee -a "${INSTALL_LOG}" \
        || log_warn "mlnx-fw-updater not available — skipping."
}

# -----------------------------------------------------------------------------
# STEP 5 — Analyze install log for known non-critical warnings
#
# Fix: grep output captured into variables with || true before testing,
# to avoid set -euo pipefail triggering on grep exit code 1 (no match).
# The "grep | while read" pattern is also replaced by variable capture
# to avoid pipefail on the grep stage.
# -----------------------------------------------------------------------------
analyze_install_log() {
    log_section "Step 5 — Post-Install Log Analysis"

    local issues_found=0

    # --- dracut nvmf warning detection ---
    NVMF_LINES=$(grep "Module 'nvmf' depends on 'network'" "${INSTALL_LOG}" \
                 2>/dev/null || true)

    if [[ -n "$NVMF_LINES" ]]; then
        echo ""
        log_warn "The following dracut message was detected in the install output:"
        echo ""
        echo "$NVMF_LINES" | while read -r line; do
            echo -e "    ${YELLOW}${line}${NC}"
        done
        echo ""
        log_notice "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        log_notice "This dracut warning is EXPECTED and NON-CRITICAL."
        log_notice ""
        log_notice "Root cause:"
        log_notice "  DOCA-OFED installs a dracut module for NVMe over Fabrics"
        log_notice "  (nvmf), which depends on dracut's 'network' module."
        log_notice "  Ubuntu 24.04 does not include this dracut network module"
        log_notice "  (it uses systemd-networkd/Netplan instead), so dracut"
        log_notice "  reports the unresolved dependency as an error."
        log_notice ""
        log_notice "Impact on your system:"
        log_notice "  - RDMA / InfiniBand drivers  : NOT affected"
        log_notice "  - ConnectX-6 NIC operation   : NOT affected"
        log_notice "  - UCX network provider       : NOT affected"
        log_notice "  - DAOS storage stack         : NOT affected"
        log_notice "  - initramfs for local boot   : NOT affected"
        log_notice ""
        log_notice "This warning only matters if your nodes boot from a remote"
        log_notice "NVMe-oF target over the network, which is not your case."
        log_notice "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        ((issues_found++))
    fi

    # --- Generic dracut fatal check (actual failure, not the nvmf warning) ---
    DRACUT_FATAL=$(grep -i "dracut.*FATAL" "${INSTALL_LOG}" 2>/dev/null || true)
    if [[ -n "$DRACUT_FATAL" ]]; then
        log_error "A FATAL dracut error (unrelated to nvmf) was detected:"
        echo "$DRACUT_FATAL" | while read -r line; do
            echo -e "    ${RED}${line}${NC}"
        done
        log_error "This may indicate a real initramfs build failure. Please investigate."
        ((issues_found++))
    fi

    # --- Check for any other unexpected apt errors in the install log ---
    UNEXPECTED=$(grep -iE "^E:|^Err:" "${INSTALL_LOG}" 2>/dev/null || true)
    if [[ -n "$UNEXPECTED" ]]; then
        log_error "Unexpected apt errors were detected:"
        echo "$UNEXPECTED" | while read -r line; do
            echo -e "    ${RED}${line}${NC}"
        done
        ((issues_found++))
    fi

    if [[ "$issues_found" -eq 0 ]]; then
        log_info "No warnings or errors detected in the install log."
    fi

    log_info "Post-install log analysis complete."
}

# -----------------------------------------------------------------------------
# STEP 6 — Enable openibd service at boot
#
# Fix: use "systemctl cat openibd" to probe service existence reliably,
# replacing "systemctl list-unit-files | grep -q" which suffers from the
# same set -euo pipefail / grep exit code interaction.
# -----------------------------------------------------------------------------
enable_services() {
    log_section "Step 6 — Enabling openibd Service at Boot"

    if systemctl cat openibd &>/dev/null; then
        systemctl enable openibd
        log_info "openibd service enabled at boot."
    else
        log_warn "openibd.service not found — skipping."
    fi
}

# -----------------------------------------------------------------------------
# MAIN
# -----------------------------------------------------------------------------
main() {
    echo -e "${BLUE}"
    echo "============================================================"
    echo "  NVIDIA DOCA-OFED ${DOCA_VERSION} — Installation Script"
    echo "  Target OS  : Ubuntu 24.04"
    echo "  Method     : Online (deb_network)"
    echo "  Date       : $(date '+%Y-%m-%d %H:%M:%S')"
    echo "============================================================"
    echo -e "${NC}"

    check_root
    check_os
    check_kernel
    check_internet
    check_mellanox_hardware

    install_prerequisites
    remove_previous_installation
    add_doca_repository
    install_doca_ofed
    analyze_install_log
    enable_services

    echo ""
    echo -e "${GREEN}"
    echo "============================================================"
    echo "  DOCA-OFED ${DOCA_VERSION} installation completed successfully."
    echo "============================================================"
    echo -e "${NC}"
    echo -e "${YELLOW}[NEXT STEPS]${NC}"
    echo ""
    echo -e "  1. ${YELLOW}A system reboot is required${NC} to load the new OFED kernel"
    echo -e "     modules and replace the Ubuntu inbox drivers."
    echo ""
    echo -e "     Run:  ${GREEN}sudo reboot${NC}"
    echo ""
    echo -e "  2. After reboot, run the verification script to confirm"
    echo -e "     that the installation is complete and your NICs are"
    echo -e "     properly detected:"
    echo ""
    echo -e "     Run:  ${GREEN}sudo ./${VERIFY_SCRIPT} --nic-count 2 --nic-devices mlx5_0,mlx5_1${NC}"
    echo ""
}

main "$@"
