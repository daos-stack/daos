#!/bin/bash
# =============================================================================
# Script    : verify_doca_ofed.sh
# Purpose   : Verify NVIDIA DOCA-OFED installation and NIC/UCX readiness
# Usage     : sudo ./verify_doca_ofed.sh [OPTIONS]
#
# Options:
#   --nic-count    <N>           Expected number of NICs (default: 2)
#   --nic-devices  <d1,d2,...>   Comma-separated UCX device names (default: mlx5_0,mlx5_1)
#   --ofed-version <version>     Expected OFED version string (default: any)
#   --skip-ucx                   Skip UCX checks
#   --skip-mst                   Skip MST checks
#   -h, --help                   Show this help
#
# Examples:
#   sudo ./verify_doca_ofed.sh
#   sudo ./verify_doca_ofed.sh --nic-count 2 --nic-devices mlx5_0,mlx5_1
#   sudo ./verify_doca_ofed.sh --nic-count 4 --nic-devices mlx5_0,mlx5_1,mlx5_2,mlx5_3
#   sudo ./verify_doca_ofed.sh --nic-count 1 --nic-devices mlx5_0 --skip-mst
# =============================================================================

set -uo pipefail

# -----------------------------------------------------------------------------
# DEFAULTS
# -----------------------------------------------------------------------------
EXPECTED_NIC_COUNT=2
NIC_DEVICES="mlx5_0,mlx5_1"
EXPECTED_OFED_VERSION=""
SKIP_UCX=false
SKIP_MST=false

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
# COUNTERS
# -----------------------------------------------------------------------------
PASS=0
FAIL=0
WARN=0

# Track specific failures for cross-check dependency reporting
MODULES_FAILED=()
OPENIBD_RUNNING=false
UCX_TRANSPORT_FAILED=false

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
log_section() { echo -e "\n${BLUE}=== $* ===${NC}"; }
log_pass()    { echo -e "  ${GREEN}[PASS]${NC} $*"; ((PASS++)); }
log_fail()    { echo -e "  ${RED}[FAIL]${NC} $*"; ((FAIL++)); }
log_warn()    { echo -e "  ${YELLOW}[WARN]${NC} $*"; ((WARN++)); }
log_info()    { echo -e "  ${CYAN}[INFO]${NC} $*"; }
log_hint()    { echo -e "  ${YELLOW}[HINT]${NC} $*"; }

usage() {
    sed -n '/^# Usage/,/^# ====/p' "$0" | grep -E "^#" | sed 's/^# \?//'
    exit 0
}

# -----------------------------------------------------------------------------
# Helper: check if a kernel module is loaded
# Uses awk to extract the exact first column, avoiding grep -q pipefail issues
# and ^ anchor fragility with set -uo pipefail.
# -----------------------------------------------------------------------------
is_module_loaded() {
    local mod="$1"
    local result
    result=$(lsmod 2>/dev/null | awk '{print $1}' | grep -x "${mod}" || true)
    [[ -n "$result" ]]
}

# -----------------------------------------------------------------------------
# ARGUMENT PARSING
# -----------------------------------------------------------------------------
parse_args() {
    local nic_devices_explicit=false

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --nic-count)    EXPECTED_NIC_COUNT="$2"; shift 2 ;;
            --nic-devices)  NIC_DEVICES="$2"; nic_devices_explicit=true; shift 2 ;;
            --ofed-version) EXPECTED_OFED_VERSION="$2"; shift 2 ;;
            --skip-ucx)     SKIP_UCX=true;  shift ;;
            --skip-mst)     SKIP_MST=true;  shift ;;
            -h|--help)      usage ;;
            *) echo -e "${RED}Unknown option: $1${NC}"; usage ;;
        esac
    done

    # If --nic-devices was NOT explicitly provided, auto-generate the device
    # list from --nic-count using the standard mlx5_N naming convention.
    if [[ "$nic_devices_explicit" == false ]]; then
        NIC_DEVICES=""
        for ((i = 0; i < EXPECTED_NIC_COUNT; i++)); do
            if [[ -n "$NIC_DEVICES" ]]; then
                NIC_DEVICES="${NIC_DEVICES},"
            fi
            NIC_DEVICES="${NIC_DEVICES}mlx5_${i}"
        done
    fi

    # Validate that the number of devices matches --nic-count
    IFS=',' read -r -a NIC_DEVICE_LIST <<< "$NIC_DEVICES"
    local actual_count=${#NIC_DEVICE_LIST[@]}
    if [[ "$actual_count" -ne "$EXPECTED_NIC_COUNT" ]]; then
        echo -e "${RED}[ERROR]${NC} --nic-count ${EXPECTED_NIC_COUNT} does not match" \
                "the number of devices in --nic-devices (${actual_count}: ${NIC_DEVICES})."
        echo -e "        Either omit --nic-devices to auto-generate, or provide" \
                "exactly ${EXPECTED_NIC_COUNT} device(s)."
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# CHECK 1 — OFED Version
# -----------------------------------------------------------------------------
check_ofed_version() {
    log_section "Check 1 — OFED Version"

    if ! command -v ofed_info &>/dev/null; then
        log_fail "ofed_info command not found. DOCA-OFED may not be installed."
        return
    fi

    OFED_VERSION=$(ofed_info -s 2>/dev/null | head -1 || true)
    if [[ -z "$OFED_VERSION" ]]; then
        log_fail "ofed_info returned empty output."
        return
    fi

    log_info "Installed OFED: ${OFED_VERSION}"

    if [[ -n "$EXPECTED_OFED_VERSION" ]]; then
        if echo "$OFED_VERSION" | grep -q "$EXPECTED_OFED_VERSION"; then
            log_pass "OFED version matches expected: ${EXPECTED_OFED_VERSION}"
        else
            log_fail "OFED version mismatch. Expected: ${EXPECTED_OFED_VERSION}, Got: ${OFED_VERSION}"
        fi
    else
        log_pass "OFED is installed: ${OFED_VERSION}"
    fi
}

# -----------------------------------------------------------------------------
# CHECK 2 — Mellanox NIC PCI Detection
# -----------------------------------------------------------------------------
check_pci_detection() {
    log_section "Check 2 — Mellanox NIC PCI Detection"

    if ! command -v lspci &>/dev/null; then
        log_warn "lspci not found. Install pciutils."
        return
    fi

    DETECTED_COUNT=$(lspci | grep -ci mellanox || true)
    log_info "Detected Mellanox PCI devices: ${DETECTED_COUNT} (expected: ${EXPECTED_NIC_COUNT})"

    lspci | grep -i mellanox | while read -r line; do
        log_info "  ${line}"
    done

    if [[ "$DETECTED_COUNT" -eq "$EXPECTED_NIC_COUNT" ]]; then
        log_pass "PCI detection: ${DETECTED_COUNT}/${EXPECTED_NIC_COUNT} NICs detected as expected."
    elif [[ "$DETECTED_COUNT" -gt 0 ]]; then
        log_warn "PCI detection: ${DETECTED_COUNT} NIC(s) found, but expected ${EXPECTED_NIC_COUNT}."
    else
        log_fail "PCI detection: No Mellanox NICs found."
    fi
}

# -----------------------------------------------------------------------------
# CHECK 3 — Kernel Modules
# Probes openibd service first to explain why modules may be missing.
# Uses is_module_loaded() helper to avoid grep -q / pipefail interaction.
# -----------------------------------------------------------------------------
check_kernel_modules() {
    log_section "Check 3 — Kernel Modules"

    # --- Pre-check: openibd service state ---
    ACTIVE_STATE=$(systemctl show openibd --property=ActiveState \
                   2>/dev/null | cut -d= -f2)
    SUB_STATE=$(systemctl show openibd --property=SubState \
                2>/dev/null | cut -d= -f2)
    EXIT_CODE=$(systemctl show openibd --property=ExecMainStatus \
                2>/dev/null | cut -d= -f2)

    log_info "openibd state: ${ACTIVE_STATE}/${SUB_STATE} (exit code: ${EXIT_CODE})"

    # openibd is a oneshot service: active/exited/0 is the correct running state
    if [[ "$ACTIVE_STATE" == "active" && "$SUB_STATE" == "exited" && "$EXIT_CODE" == "0" ]] || \
       [[ "$ACTIVE_STATE" == "active" && "$SUB_STATE" == "running" ]]; then
        OPENIBD_RUNNING=true
        log_pass "openibd completed successfully — OFED modules should be loaded."
    else
        OPENIBD_RUNNING=false
        echo ""
        log_warn "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        log_warn "openibd is NOT in a healthy state (${ACTIVE_STATE}/${SUB_STATE})."
        log_warn "This is the likely root cause of any kernel module failures"
        log_warn "reported below."
        log_warn ""
        log_warn "openibd is the OFED service responsible for loading all"
        log_warn "InfiniBand/RDMA kernel modules (mlx5_ib, ib_uverbs, etc.)"
        log_warn "at boot. Without it, UCX RDMA transports will also be"
        log_warn "unavailable (see Check 10)."
        log_warn ""
        log_hint "To fix, run:"
        log_hint "  sudo systemctl start openibd"
        OPENIBD_ENABLED=$(systemctl is-enabled openibd 2>/dev/null || echo "disabled")
        if [[ "$OPENIBD_ENABLED" != "enabled" ]]; then
            log_hint "  sudo systemctl enable openibd   # to persist across reboots"
        fi
        log_hint ""
        log_hint "Then re-run this verification script."
        log_warn "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
    fi

    # --- Module checks ---
    # Uses is_module_loaded() to avoid set -uo pipefail / grep -q interaction
    REQUIRED_MODULES=("mlx5_core" "mlx5_ib" "ib_core" "ib_uverbs" "rdma_cm")

    for mod in "${REQUIRED_MODULES[@]}"; do
        if is_module_loaded "$mod"; then
            log_pass "Module loaded: ${mod}"
        else
            log_fail "Module NOT loaded: ${mod}"
            MODULES_FAILED+=("$mod")

            if [[ "$OPENIBD_RUNNING" == false ]]; then
                log_hint "  → Try: sudo modprobe ${mod}  (or start openibd first)"
            else
                # openibd is healthy but module still missing — try modprobe
                MODPROBE_OUT=$(modprobe "$mod" 2>&1 || true)
                if is_module_loaded "$mod"; then
                    log_warn "  → Loaded ${mod} via modprobe (was not auto-loaded by openibd)"
                else
                    log_fail "  → modprobe ${mod} failed: ${MODPROBE_OUT}"
                    log_hint "  → Check: dmesg | grep ${mod}"
                    log_hint "  → Check: dkms status"
                    log_hint "  → Secure Boot may be blocking unsigned modules:"
                    log_hint "    mokutil --sb-state"
                fi
            fi
        fi
    done

    # --- Summary for this check ---
    if [[ ${#MODULES_FAILED[@]} -gt 0 && "$OPENIBD_RUNNING" == false ]]; then
        echo ""
        log_warn "All module failures above are consistent with openibd not"
        log_warn "being healthy. Start openibd and re-run this script."
    fi
}

# -----------------------------------------------------------------------------
# CHECK 4 — IB Device Status (ibstat)
# -----------------------------------------------------------------------------
check_ibstat() {
    log_section "Check 4 — InfiniBand Device Status (ibstat)"

    if ! command -v ibstat &>/dev/null; then
        log_fail "ibstat not found. DOCA-OFED may not be properly installed."
        return
    fi

    for dev in "${NIC_DEVICE_LIST[@]}"; do
        log_info "Checking device: ${dev}"
        if ! ibstat "$dev" &>/dev/null; then
            log_fail "  Device ${dev} not found in ibstat output."
            if [[ "$OPENIBD_RUNNING" == false ]]; then
                log_hint "  → openibd is not healthy. Start it to expose IB devices."
            fi
            continue
        fi

        STATE=$(ibstat "$dev" 2>/dev/null | grep -E "^\s+State:" | awk '{print $2}')
        PHYS_STATE=$(ibstat "$dev" 2>/dev/null | grep -E "^\s+Physical state:" | awk '{print $3}')
        RATE=$(ibstat "$dev" 2>/dev/null | grep -E "^\s+Rate:" | awk '{print $2}')
        LINK_LAYER=$(ibstat "$dev" 2>/dev/null | grep -E "^\s+Link layer:" | awk '{print $3}')
        FW=$(ibstat "$dev" 2>/dev/null | grep -E "^\s+Firmware version:" | awk '{print $3}')

        log_info "  State        : ${STATE}"
        log_info "  Physical     : ${PHYS_STATE}"
        log_info "  Rate (Gb/s)  : ${RATE}"
        log_info "  Link layer   : ${LINK_LAYER}"
        log_info "  Firmware     : ${FW}"

        if [[ "$STATE" == "Active" ]]; then
            log_pass "  ${dev}: State is Active"
        else
            log_fail "  ${dev}: State is ${STATE} (expected Active)"
        fi

        if [[ "$PHYS_STATE" == "LinkUp" ]]; then
            log_pass "  ${dev}: Physical state is LinkUp"
        else
            log_fail "  ${dev}: Physical state is ${PHYS_STATE} (expected LinkUp)"
        fi
    done
}

# -----------------------------------------------------------------------------
# CHECK 5 — RDMA Link Status
# -----------------------------------------------------------------------------
check_rdma_links() {
    log_section "Check 5 — RDMA Link Status (rdma link)"

    if ! command -v rdma &>/dev/null; then
        log_warn "rdma command not found. Skipping RDMA link check."
        return
    fi

    for dev in "${NIC_DEVICE_LIST[@]}"; do
        RDMA_LINE=$(rdma link 2>/dev/null | grep "^link ${dev}" || true)
        if [[ -z "$RDMA_LINE" ]]; then
            log_fail "  ${dev}: Not found in rdma link output."
            if [[ "$OPENIBD_RUNNING" == false ]]; then
                log_hint "  → openibd is not healthy. Start it to expose RDMA links."
            fi
            continue
        fi

        log_info "  ${RDMA_LINE}"

        if echo "$RDMA_LINE" | grep -q "state ACTIVE"; then
            log_pass "  ${dev}: RDMA state is ACTIVE"
        else
            log_fail "  ${dev}: RDMA state is not ACTIVE"
        fi

        if echo "$RDMA_LINE" | grep -q "physical_state LINK_UP"; then
            log_pass "  ${dev}: RDMA physical state is LINK_UP"
        else
            log_fail "  ${dev}: RDMA physical state is not LINK_UP"
        fi
    done
}

# -----------------------------------------------------------------------------
# CHECK 6 — Firmware Info (mlxfwmanager)
# -----------------------------------------------------------------------------
check_firmware() {
    log_section "Check 6 — NIC Firmware Info (mlxfwmanager)"

    if ! command -v mlxfwmanager &>/dev/null; then
        log_warn "mlxfwmanager not found. Skipping firmware check."
        return
    fi

    FW_OUTPUT=$(mlxfwmanager 2>/dev/null || true)
    if [[ -z "$FW_OUTPUT" ]]; then
        log_warn "mlxfwmanager returned no output."
        return
    fi

    DETECTED=$(echo "$FW_OUTPUT" | grep -c "Device Type" || true)
    log_info "mlxfwmanager detected ${DETECTED} device(s):"
    echo "$FW_OUTPUT" | grep -E "Device Type|FW|Part Number|PCI Device" | \
        while read -r line; do log_info "  ${line}"; done

    if [[ "$DETECTED" -eq "$EXPECTED_NIC_COUNT" ]]; then
        log_pass "Firmware check: ${DETECTED}/${EXPECTED_NIC_COUNT} devices reported."
    else
        log_warn "Firmware check: ${DETECTED} device(s) found, expected ${EXPECTED_NIC_COUNT}."
    fi
}

# -----------------------------------------------------------------------------
# CHECK 7 — MST Device Access
# -----------------------------------------------------------------------------
check_mst() {
    log_section "Check 7 — MST Device Access"

    if [[ "$SKIP_MST" == true ]]; then
        log_info "MST check skipped (--skip-mst)."
        return
    fi

    if ! command -v mst &>/dev/null; then
        log_warn "mst command not found. Skipping MST check."
        return
    fi

    mst start &>/dev/null || true
    MST_DEVICES=$(mst status 2>/dev/null | grep "^/dev/mst" || true)

    if [[ -z "$MST_DEVICES" ]]; then
        log_warn "No MST devices found. Run 'mst start' manually."
        return
    fi

    MST_COUNT=$(echo "$MST_DEVICES" | wc -l)
    log_info "MST devices found: ${MST_COUNT}"
    echo "$MST_DEVICES" | while read -r line; do log_info "  ${line}"; done

    if [[ "$MST_COUNT" -ge "$EXPECTED_NIC_COUNT" ]]; then
        log_pass "MST: ${MST_COUNT}/${EXPECTED_NIC_COUNT} devices accessible."
    else
        log_warn "MST: ${MST_COUNT} device(s) found, expected ${EXPECTED_NIC_COUNT}."
    fi
}

# -----------------------------------------------------------------------------
# CHECK 8 — UCX Version and MLX5 Support
# -----------------------------------------------------------------------------
check_ucx_version() {
    log_section "Check 8 — UCX Version and MLX5 Build Support"

    if [[ "$SKIP_UCX" == true ]]; then
        log_info "UCX checks skipped (--skip-ucx)."
        return
    fi

    if ! command -v ucx_info &>/dev/null; then
        log_fail "ucx_info not found. UCX may not be installed."
        return
    fi

    UCX_VERSION=$(ucx_info -v 2>/dev/null | grep "Library version" | awk '{print $NF}')
    log_info "UCX version: ${UCX_VERSION}"
    log_pass "UCX is installed (version ${UCX_VERSION})"

    if ucx_info -v 2>/dev/null | grep -q "\-\-with-mlx5"; then
        log_pass "UCX compiled with MLX5 support (--with-mlx5)"
    else
        log_fail "UCX is NOT compiled with MLX5 support. RDMA transports unavailable."
    fi

    if ucx_info -v 2>/dev/null | grep -q "\-\-with-verbs"; then
        log_pass "UCX compiled with RDMA Verbs support (--with-verbs)"
    else
        log_warn "UCX may not have RDMA verbs support."
    fi
}

# -----------------------------------------------------------------------------
# CHECK 9 — UCX NIC Device Detection
# -----------------------------------------------------------------------------
check_ucx_devices() {
    log_section "Check 9 — UCX NIC Device Detection"

    if [[ "$SKIP_UCX" == true ]]; then
        log_info "UCX checks skipped (--skip-ucx)."
        return
    fi

    if ! command -v ucx_info &>/dev/null; then
        log_warn "ucx_info not found. Skipping UCX device check."
        return
    fi

    UCX_DEVICES=$(ucx_info -d 2>/dev/null | grep "Device:" | awk '{print $NF}' | sort -u)
    log_info "UCX detected devices:"
    echo "$UCX_DEVICES" | while read -r d; do log_info "  ${d}"; done

    for dev in "${NIC_DEVICE_LIST[@]}"; do
        if echo "$UCX_DEVICES" | grep -q "^${dev}"; then
            log_pass "UCX detects device: ${dev}"
        else
            log_fail "UCX does NOT detect device: ${dev}"
            if [[ "$OPENIBD_RUNNING" == false ]]; then
                log_hint "  → openibd is not healthy. UCX cannot access RDMA devices"
                log_hint "    until mlx5_ib is loaded. Run: sudo systemctl start openibd"
            fi
        fi
    done
}

# -----------------------------------------------------------------------------
# CHECK 10 — UCX RDMA Transports
#
# Key notes:
# - ucx_info -d output lines are prefixed with '#' (e.g. "# Transport: rc_mlx5")
# - The grep pattern matches within the line (no ^ anchor) to handle this prefix
# - Uses is_module_loaded() helper to avoid set -uo pipefail / grep -q issues
# - If mlx5_ib is not loaded (openibd not healthy), NO mlx5 transports will
#   appear regardless of UCX build flags. This check detects and reports that
#   dependency explicitly.
# -----------------------------------------------------------------------------
check_ucx_transports() {
    log_section "Check 10 — UCX RDMA Transports per NIC"

    if [[ "$SKIP_UCX" == true ]]; then
        log_info "UCX checks skipped (--skip-ucx)."
        return
    fi

    if ! command -v ucx_info &>/dev/null; then
        log_warn "ucx_info not found. Skipping UCX transport check."
        return
    fi

    # --- Pre-check: detect if mlx5_ib is loaded using the robust helper ---
    MLX5_IB_LOADED=true
    if ! is_module_loaded "mlx5_ib"; then
        MLX5_IB_LOADED=false
        echo ""
        log_warn "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        log_warn "Kernel module mlx5_ib is NOT loaded."
        log_warn ""
        log_warn "UCX RDMA transports (rc_mlx5, ud_mlx5, dc_mlx5) are"
        log_warn "provided by the mlx5_ib kernel module. Without it, UCX"
        log_warn "cannot expose any RDMA/InfiniBand transport — all transport"
        log_warn "checks below will fail as a direct consequence."
        log_warn ""
        log_warn "This is NOT a UCX installation issue."
        log_warn "This is a consequence of openibd not being healthy"
        log_warn "(see Check 3 above for the fix)."
        log_warn ""
        log_hint "Fix:  sudo systemctl start openibd"
        log_hint "Then: re-run this verification script."
        log_warn "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
    fi

    REQUIRED_TRANSPORTS=("rc_mlx5" "ud_mlx5" "dc_mlx5")

    for dev in "${NIC_DEVICE_LIST[@]}"; do
        log_info "Checking UCX transports for device: ${dev}:1"

        # Capture full transport output for this device once
        UCX_OUTPUT=$(UCX_NET_DEVICES="${dev}:1" ucx_info -d 2>/dev/null || true)

        for transport in "${REQUIRED_TRANSPORTS[@]}"; do
            # Match "Transport: rc_mlx5" with optional leading '#' and spaces
            if echo "$UCX_OUTPUT" | grep -qE "[#[:space:]]*Transport:[[:space:]]+${transport}"; then
                log_pass "  ${dev}: Transport ${transport} available"
            else
                log_fail "  ${dev}: Transport ${transport} NOT available"
                UCX_TRANSPORT_FAILED=true
                if [[ "$MLX5_IB_LOADED" == false ]]; then
                    log_hint "    → Root cause: mlx5_ib not loaded (openibd not healthy)"
                else
                    log_hint "    → Check: UCX_NET_DEVICES=${dev}:1 ucx_info -d | grep Transport"
                    log_hint "    → Check: dmesg | grep mlx5"
                fi
            fi
        done
    done
}

# -----------------------------------------------------------------------------
# CHECK 11 — openibd Service Status
#
# Note: openibd is a oneshot service. Its correct state after a successful
# boot is "active (exited)" with exit code 0. It loads all OFED kernel
# modules at boot then exits cleanly. It does NOT remain as a running daemon.
#
# Service existence is checked via "systemctl cat" which works reliably
# regardless of permissions, unlike "systemctl list-unit-files | grep".
# -----------------------------------------------------------------------------
check_openibd_service() {
    log_section "Check 11 — openibd Service"

    # Use systemctl cat to reliably probe unit existence without permission issues
    if ! systemctl cat openibd &>/dev/null; then
        log_warn "openibd.service not found in systemd."
        log_hint "  → OFED module auto-loading at boot may not be configured."
        return
    fi

    ENABLED=$(systemctl is-enabled openibd 2>/dev/null || echo "disabled")

    # Retrieve the full ActiveState, SubState and exit code for precise detection
    ACTIVE_STATE=$(systemctl show openibd --property=ActiveState \
                   2>/dev/null | cut -d= -f2)
    SUB_STATE=$(systemctl show openibd --property=SubState \
                2>/dev/null | cut -d= -f2)
    EXIT_CODE=$(systemctl show openibd --property=ExecMainStatus \
                2>/dev/null | cut -d= -f2)

    log_info "openibd enabled     : ${ENABLED}"
    log_info "openibd ActiveState : ${ACTIVE_STATE}"
    log_info "openibd SubState    : ${SUB_STATE}"
    log_info "openibd exit code   : ${EXIT_CODE}"

    # --- Enabled at boot check ---
    if [[ "$ENABLED" == "enabled" ]]; then
        log_pass "openibd is enabled at boot."
    else
        log_fail "openibd is NOT enabled at boot."
        log_hint "  → Run: sudo systemctl enable openibd"
        log_hint "     OFED modules will not be loaded automatically on next reboot."
    fi

    # --- Active state check ---
    # Valid expected states for a oneshot service:
    #   ActiveState=active + SubState=exited + ExitCode=0  → correct (NORMAL)
    #   ActiveState=active + SubState=running              → still running (unexpected)
    #   ActiveState=failed                                 → failed during execution
    #   ActiveState=inactive + SubState=dead               → never ran or was stopped

    if [[ "$ACTIVE_STATE" == "active" && "$SUB_STATE" == "exited" && "$EXIT_CODE" == "0" ]]; then
        log_pass "openibd completed successfully (active/exited, exit code 0)."
        log_info "  ✓ This is the correct and expected state for openibd."
        log_info "    openibd is a oneshot service: it loads all OFED kernel"
        log_info "    modules at boot then exits cleanly. It does not remain"
        log_info "    as a running daemon."

    elif [[ "$ACTIVE_STATE" == "active" && "$SUB_STATE" == "running" ]]; then
        log_pass "openibd is currently active and running."

    elif [[ "$ACTIVE_STATE" == "failed" ]]; then
        log_fail "openibd has FAILED (exit code: ${EXIT_CODE})."
        log_hint "  → Check logs: sudo journalctl -u openibd --no-pager"
        log_hint "  → Retry:      sudo systemctl start openibd"

    elif [[ "$ACTIVE_STATE" == "inactive" && "$SUB_STATE" == "dead" ]]; then
        log_fail "openibd is inactive — it has not run since last boot."
        log_hint "  → Start now:  sudo systemctl start openibd"
        log_hint "  → Then re-run this verification script."

    else
        log_warn "openibd is in an unexpected state: ${ACTIVE_STATE}/${SUB_STATE}"
        log_hint "  → Check: sudo systemctl status openibd"
    fi
}

# -----------------------------------------------------------------------------
# FINAL REPORT
# Includes a dependency summary section when related failures are detected.
# -----------------------------------------------------------------------------
print_report() {
    TOTAL=$((PASS + FAIL + WARN))
    echo ""
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${BLUE}  DOCA-OFED Verification Report${NC}"
    echo -e "${BLUE}  Host       : $(hostname)${NC}"
    echo -e "${BLUE}  Kernel     : $(uname -r)${NC}"
    echo -e "${BLUE}  Date       : $(date '+%Y-%m-%d %H:%M:%S')${NC}"
    echo -e "${BLUE}  NICs tested: ${NIC_DEVICES} (expected count: ${EXPECTED_NIC_COUNT})${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo -e "  ${GREEN}PASSED : ${PASS}${NC}"
    echo -e "  ${RED}FAILED : ${FAIL}${NC}"
    echo -e "  ${YELLOW}WARNED : ${WARN}${NC}"
    echo -e "  Total  : ${TOTAL}"
    echo -e "${BLUE}============================================================${NC}"

    # --- Dependency summary: explain cascading failures from one root cause ---
    if [[ ${#MODULES_FAILED[@]} -gt 0 && "$OPENIBD_RUNNING" == false ]]; then
        echo ""
        echo -e "${YELLOW}  Root Cause Summary — Cascading Failures Detected${NC}"
        echo -e "${YELLOW}  ─────────────────────────────────────────────────${NC}"
        echo -e "${YELLOW}  The following failures all share the same root cause:${NC}"
        echo -e "${YELLOW}  openibd service is not in a healthy state.${NC}"
        echo ""
        echo -e "  Failure chain:"
        echo -e "    ${RED}openibd not healthy${NC}"
        echo -e "      └── ${RED}Modules not loaded:${NC} ${MODULES_FAILED[*]}"
        if [[ "$UCX_TRANSPORT_FAILED" == true ]]; then
            echo -e "              └── ${RED}UCX RDMA transports unavailable${NC} (rc_mlx5, ud_mlx5, dc_mlx5)"
        fi
        echo ""
        echo -e "  ${GREEN}Single fix to resolve all failures above:${NC}"
        echo -e "    ${GREEN}sudo systemctl start openibd${NC}   # immediate"
        echo -e "    ${GREEN}sudo systemctl enable openibd${NC}  # persist across reboots"
        echo -e "    Then re-run: ${GREEN}sudo ./$(basename "$0") --nic-count ${EXPECTED_NIC_COUNT} --nic-devices ${NIC_DEVICES}${NC}"
        echo -e "${YELLOW}  ─────────────────────────────────────────────────${NC}"
    fi

    echo ""
    if [[ "$FAIL" -eq 0 && "$WARN" -eq 0 ]]; then
        echo -e "${GREEN}  All checks PASSED. Your system is ready for DAOS with UCX.${NC}"
        echo ""
        exit 0
    elif [[ "$FAIL" -eq 0 ]]; then
        echo -e "${YELLOW}  All critical checks PASSED with some warnings. Review above.${NC}"
        echo ""
        exit 0
    else
        echo -e "${RED}  Some checks FAILED. Please review the output and hints above.${NC}"
        echo ""
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# MAIN
# -----------------------------------------------------------------------------
main() {
    parse_args "$@"

    echo -e "${BLUE}"
    echo "============================================================"
    echo "  NVIDIA DOCA-OFED — Post-Install Verification Script"
    echo "  NICs expected  : ${EXPECTED_NIC_COUNT}"
    echo "  UCX devices    : ${NIC_DEVICES}"
    echo "  Date           : $(date '+%Y-%m-%d %H:%M:%S')"
    echo "============================================================"
    echo -e "${NC}"

    check_ofed_version
    check_pci_detection
    check_kernel_modules
    check_ibstat
    check_rdma_links
    check_firmware
    check_mst
    check_ucx_version
    check_ucx_devices
    check_ucx_transports
    check_openibd_service

    print_report
}

main "$@"
