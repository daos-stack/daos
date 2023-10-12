#!/bin/bash

# set -x
set -eu -o pipefail

# ---------------------------------------------------------------------------
# Protocol registry  –  short name → (command, one-line description, detail)
# ---------------------------------------------------------------------------
declare -A PROTO_CMD=(
    [rc]="ibv_rc_pingpong"
    [uc]="ibv_uc_pingpong"
    [ud]="ibv_ud_pingpong"
    [srq]="ibv_srq_pingpong"
    [xsrq]="ibv_xsrq_pingpong"
)

declare -A PROTO_SHORT_DESC=(
    [rc]="Reliable Connected (RC)"
    [uc]="Unreliable Connected (UC)"
    [ud]="Unreliable Datagram (UD)"
    [srq]="Reliable Connected with Shared Receive Queue (SRQ)"
    [xsrq]="Reliable Connected with Extended Shared Receive Queue (XSRQ)"
)

declare -A PROTO_LONG_DESC=(
    [rc]="RC provides reliable, in-order, connection-oriented delivery between a \
dedicated pair of Queue Pairs (QPs). Lost or corrupted packets are retransmitted \
automatically by the HCA firmware, making RC the most commonly used transport for \
RDMA workloads (MPI, storage, etc.)."
    [uc]="UC is connection-oriented (one dedicated QP pair) but offers no delivery \
guarantee and no retransmission. It is lighter than RC and useful when the \
application handles its own error recovery, or when low latency matters more than \
reliability."
    [ud]="UD is connectionless: a single QP can exchange messages with any number of \
remote QPs. Packets may be dropped, reordered, or duplicated; there is no \
retransmission. UD is used for multicast, subnet management (MADs), and \
latency-sensitive small-message workloads."
    [srq]="SRQ allows multiple RC QPs to share a single Receive Queue, reducing memory \
consumption when many connections are open. This test exercises RC transport \
semantics (reliable, in-order) while validating the SRQ plumbing."
    [xsrq]="XSRQ (also called Tagged-Matching SRQ) extends SRQ with hardware tag-matching \
capabilities defined in the MPI standard. It is used by MPI libraries that \
offload message matching to the HCA, reducing CPU overhead on large multi-rail \
clusters."
)

# Ordered list for display purposes
PROTO_ORDER=( rc uc ud srq xsrq )

# ---------------------------------------------------------------------------
# Help / usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] <nodeset>

Test InfiniBand RDMA connectivity between nodes using an ibv pingpong command.
Nodes are specified as a ClusterShell nodeset expression (e.g. "node[1-4]").

OPTIONS:
  -C, --command PROTO   InfiniBand transport protocol to test (default: rc).
                        Can also be set via the IBV_PROTO environment variable.
  -o, --output FILE     Markdown report output file
                        (default: <command>-report.md, e.g. ibv_rc_pingpong-report.md)
                        Can also be set via the REPORT_FILE environment variable.
  -h, --help            Show this help message and exit.

SUPPORTED PROTOCOLS:
EOF
    for proto in "${PROTO_ORDER[@]}"; do
        printf "  %-6s  %-10s  %s\n" \
            "$proto" "(${PROTO_CMD[$proto]})" "${PROTO_SHORT_DESC[$proto]}"
    done
    cat <<EOF

PROTOCOL DETAILS:
EOF
    for proto in "${PROTO_ORDER[@]}"; do
        echo "  $proto — ${PROTO_SHORT_DESC[$proto]}"
        # word-wrap the long description at 72 chars with 6-space indent
        echo "${PROTO_LONG_DESC[$proto]}" \
            | fold -s -w 72 \
            | sed 's/^/        /'
        echo
    done
    cat <<EOF
EXAMPLES:
  $(basename "$0") node[1-4]
  $(basename "$0") --command rc node[1-4]
  $(basename "$0") --command ud --output report.md node[1-4]
  IBV_PROTO=srq REPORT_FILE=out.md $(basename "$0") node[1-4]

REQUIREMENTS:
  - nodeset      (ClusterShell)
  - ibdev2netdev and the chosen ibv pingpong command available on each remote host
  - Each IB interface must have an IP address assigned
  - Passwordless SSH access as root to all target nodes

OUTPUT:
  A Markdown file containing:
  - A summary (host list, protocol used, pass/fail counts)
  - A description of the protocol under test
  - A connection matrix (client × server) with per-cell status icons
  - A table of failed links with their failure reason

EXIT CODES:
  0   All tests passed (or completed without a fatal error)
  1   Fatal error (e.g. unable to kill a stale pingpong process)
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
IBV_PROTO="${IBV_PROTO:-rc}"
REPORT_FILE="${REPORT_FILE:-}"   # resolved after --command is known

while [[ $# -gt 0 ]]; do
    case "$1" in
        -C|--command)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            IBV_PROTO="$2"; shift 2 ;;
        -o|--output)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            REPORT_FILE="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        -*)
            echo "[ERROR] Unknown option: $1" >&2; echo >&2; usage >&2; exit 1 ;;
        *)
            break ;;   # remaining args are the nodeset
    esac
done

# Validate protocol
if [[ -z "${PROTO_CMD[$IBV_PROTO]+set}" ]]; then
    echo "[ERROR] Invalid protocol: '$IBV_PROTO'" >&2
    echo "[ERROR] Valid values: ${PROTO_ORDER[*]}" >&2
    echo >&2
    usage >&2
    exit 1
fi

IBV_COMMAND="${PROTO_CMD[$IBV_PROTO]}"

# Default report file name now that the command is known
[[ -z "$REPORT_FILE" ]] && REPORT_FILE="${IBV_COMMAND}-report.md"

if [[ $# -eq 0 ]]; then
    echo "[ERROR] No nodeset specified." >&2
    echo >&2
    usage >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log_info()  { echo "[INFO] $*" ; }
log_warn()  { echo "[WARN] $*" ; }
log_error() { echo "[ERROR] $*" ; }
log_fatal() { echo ; echo "[FATAL] $*" ; exit 1 ; }

function kill_pingpong() {
    local hostname="$1"

    if ! ssh -n root@"$hostname" pgrep "$IBV_COMMAND" &>/dev/null; then
        return
    fi

    log_info "Killing $IBV_COMMAND on host $hostname"
    ssh -n root@"$hostname" pkill "$IBV_COMMAND"
    sleep 1

    if ssh -n root@"$hostname" pgrep "$IBV_COMMAND" &>/dev/null; then
        log_warn "Hard killing $IBV_COMMAND on host $hostname"
        ssh -n root@"$hostname" pkill -9 "$IBV_COMMAND"
        sleep 3
    fi

    if ssh -n root@"$hostname" pgrep "$IBV_COMMAND" &>/dev/null; then
        log_fatal "Not able to kill $IBV_COMMAND on host $hostname"
    fi
}

# ---------------------------------------------------------------------------
# Discover host / interface information
# ---------------------------------------------------------------------------
log_info "Discovering host information..."
log_info "Protocol : $IBV_PROTO — ${PROTO_SHORT_DESC[$IBV_PROTO]}"
log_info "Command  : $IBV_COMMAND"

declare -A hosts   # hosts["hostname:if"] = "dev:port:ip"

for hostname in $( nodeset -e "$@" ); do
    log_info "    - Discovering IB information of host $hostname"
    while IFS= read -r line0; do
        grep -qE 'Up' <<<"$line0" || continue

        local_dev=$(  awk '{ print $1 }' <<<"$line0" )
        local_port=$( awk '{ print $3 }' <<<"$line0" )
        local_if=$(   awk '{ print $5 }' <<<"$line0" )
        local_ip=$(   ssh -n root@"$hostname" ip address show "$local_if" \
                      | sed -n -E '/inet/s#^[[:blank:]]+inet[[:space:]]+([0-9][0-9.]+)/.+$#\1#p' )

        if [[ -z "$local_ip" ]]; then
            log_warn "        No IP address found for interface $local_if on $hostname — skipping."
            continue
        fi

        hosts["$hostname:$local_if"]="$local_dev:$local_port:$local_ip"
        log_info "        Found: interface=$local_if, dev=$local_dev, port=$local_port, ip=$local_ip"
    done < <( ssh -n root@"$hostname" ibdev2netdev )
done

# Build a stable sorted list of keys for a deterministic table layout
mapfile -t sorted_keys < <( printf '%s\n' "${!hosts[@]}" | sort )

if [[ ${#sorted_keys[@]} -eq 0 ]]; then
    log_fatal "No active InfiniBand interfaces with IP addresses found on the specified hosts."
fi

# ---------------------------------------------------------------------------
# Run pingpong tests and collect results
# ---------------------------------------------------------------------------
# results["client_key->server_key"] = "ok" | "server_start_failed" | "test_failed" | "self"
declare -A results

for key0 in "${sorted_keys[@]}"; do
    server_hostname=$( cut -d: -f1 <<<"$key0" )
    server_if=$(       cut -d: -f2 <<<"$key0" )
    server_dev=$(      cut -d: -f1 <<<"${hosts[$key0]}" )
    server_port=$(     cut -d: -f2 <<<"${hosts[$key0]}" )
    server_ip=$(       cut -d: -f3 <<<"${hosts[$key0]}" )

    for key1 in "${sorted_keys[@]}"; do
        client_hostname=$( cut -d: -f1 <<<"$key1" )

        if [[ "$client_hostname" == "$server_hostname" ]]; then
            results["$key1->$key0"]="self"
            continue
        fi

        client_if=$(   cut -d: -f2 <<<"$key1" )
        client_dev=$(  cut -d: -f1 <<<"${hosts[$key1]}" )
        client_port=$( cut -d: -f2 <<<"${hosts[$key1]}" )

        echo
        kill_pingpong "$server_hostname"
        log_info "Starting $IBV_COMMAND server: host=$server_hostname, interface=$server_if"
        ssh -n root@"$server_hostname" \
            "$IBV_COMMAND -d $server_dev -i $server_port &>/tmp/${IBV_COMMAND}-server.log &"

        timeout=3
        while ! ssh -n root@"$server_hostname" pgrep "$IBV_COMMAND" &>/dev/null \
              && (( timeout > 0 )); do
            sleep 1
            (( timeout-- )) || true
        done

        if ! ssh -n root@"$server_hostname" pgrep "$IBV_COMMAND" &>/dev/null; then
            echo
            log_error "Could not start $IBV_COMMAND server: host=$server_hostname, interface=$server_if"
            log_error "Server command : $IBV_COMMAND -d $server_dev -i $server_port"
            log_error "Server log:"
            ssh -n root@"$server_hostname" cat "/tmp/${IBV_COMMAND}-server.log"
            results["$key1->$key0"]="server_start_failed"
            kill_pingpong "$server_hostname"
            continue
        fi

        server_pid=$( ssh -n root@"$server_hostname" pgrep "$IBV_COMMAND" )
        log_info "Server started with PID: $server_pid"

        kill_pingpong "$client_hostname"
        log_info "    - Pinging server from client: host=$client_hostname, interface=$client_if"

        if ! ssh -n root@"$client_hostname" \
                "$IBV_COMMAND -d $client_dev -i $client_port $server_ip \
                 &>/tmp/${IBV_COMMAND}-client.log" ; then
            echo
            log_error "    - $client_hostname:$client_if =X=> $server_hostname:$server_if"
            log_error "Server command : $IBV_COMMAND -d $server_dev -i $server_port"
            log_error "Client command : $IBV_COMMAND -d $client_dev -i $client_port $server_ip"
            log_error "Client log:"
            ssh -n root@"$client_hostname" cat "/tmp/${IBV_COMMAND}-client.log"
            results["$key1->$key0"]="test_failed"
        else
            results["$key1->$key0"]="ok"
        fi

        kill_pingpong "$client_hostname"
        kill_pingpong "$server_hostname"
    done
done

# ---------------------------------------------------------------------------
# Generate Markdown report
# ---------------------------------------------------------------------------
generate_report() {
    local report_file="$1"
    local -n _keys="$2"    # nameref to sorted_keys array
    local -n _results="$3" # nameref to results associative array

    local total=0 ok_count=0

    for key_client in "${_keys[@]}"; do
        for key_server in "${_keys[@]}"; do
            local h_c h_s
            h_c=$( cut -d: -f1 <<<"$key_client" )
            h_s=$( cut -d: -f1 <<<"$key_server" )
            [[ "$h_c" == "$h_s" ]] && continue
            (( total++ )) || true
            local cell="${_results[$key_client->$key_server]:-unknown}"
            [[ "$cell" == "ok" ]] && (( ok_count++ )) || true
        done
    done

    {
        echo "# InfiniBand Pingpong Test Report"
        echo
        # ── Test summary ──────────────────────────────────────────────────
        echo "## Test Summary"
        echo
        echo "| Parameter | Value |"
        echo "|-----------|-------|"
        echo "| Date | $( date -u '+%Y-%m-%d %H:%M:%S UTC' ) |"
        echo "| Protocol | \`$IBV_PROTO\` — ${PROTO_SHORT_DESC[$IBV_PROTO]} |"
        echo "| Command | \`$IBV_COMMAND\` |"
        echo "| Hosts | $( printf '%s\n' "${_keys[@]}" | cut -d: -f1 | sort -u | tr '\n' ',' | sed 's/,$//' | sed 's/,/, /g' ) |"
        echo "| Total links tested | ${total} |"
        echo "| Passed | ${ok_count} |"
        echo "| Failed | $(( total - ok_count )) |"
        echo
        # ── Protocol description ──────────────────────────────────────────
        echo "## Protocol Under Test"
        echo
        echo "### ${PROTO_SHORT_DESC[$IBV_PROTO]}"
        echo
        # Re-wrap the long description for Markdown (no indent needed)
        echo "${PROTO_LONG_DESC[$IBV_PROTO]}" | fold -s -w 80
        echo
        echo "**Server command:** \`$IBV_COMMAND -d <dev> -i <port>\`"
        echo
        echo "**Client command:** \`$IBV_COMMAND -d <dev> -i <port> <server-ip>\`"
        echo
        # ── What is tested ────────────────────────────────────────────────
        echo "## What Is Tested"
        echo
        cat <<'WHAT'
Each active InfiniBand interface on every node is exercised as both a **server**
and a **client**. For every (client, server) pair across different hosts:

1. A server process is started on the server node, listening on its IB device
   and port.
2. A client process is started on the client node, connecting to the server via
   its IP address.
3. The test is marked **passed** (✅) if the client exits with code 0, or
   **failed** (❌) otherwise.
4. Server and client processes are cleaned up after each pair, regardless of
   the outcome.

Same-host pairs are skipped (—) since loopback is not meaningful here.
WHAT
        echo
        # ── Legend ───────────────────────────────────────────────────────
        echo "## Legend"
        echo
        echo "| Symbol | Meaning |"
        echo "|--------|---------|"
        echo "| ✅ | Connection OK |"
        echo "| ❌ | Test failed |"
        echo "| 🚫 | Server failed to start |"
        echo "| — | Same host (not tested) |"
        echo
        # ── Connection matrix ─────────────────────────────────────────────
        echo "## Connection Matrix"
        echo
        echo "> Rows = **client** (source) · Columns = **server** (destination)"
        echo

        local header="| Client \\ Server |"
        local separator="|:---|"
        for key_server in "${_keys[@]}"; do
            header+=" \`$key_server\` |"
            separator+=":---:|"
        done
        echo "$header"
        echo "$separator"

        for key_client in "${_keys[@]}"; do
            local row="| \`$key_client\` |"
            for key_server in "${_keys[@]}"; do
                local h_c h_s cell symbol
                h_c=$( cut -d: -f1 <<<"$key_client" )
                h_s=$( cut -d: -f1 <<<"$key_server" )
                if [[ "$h_c" == "$h_s" ]]; then
                    symbol="—"
                else
                    cell="${_results[$key_client->$key_server]:-unknown}"
                    case "$cell" in
                        ok)                  symbol="✅" ;;
                        test_failed)         symbol="❌" ;;
                        server_start_failed) symbol="🚫" ;;
                        *)                   symbol="❓" ;;
                    esac
                fi
                row+=" $symbol |"
            done
            echo "$row"
        done

        # ── Failed links detail ───────────────────────────────────────────
        local any_failure=0
        for key_client in "${_keys[@]}"; do
            for key_server in "${_keys[@]}"; do
                local h_c h_s
                h_c=$( cut -d: -f1 <<<"$key_client" )
                h_s=$( cut -d: -f1 <<<"$key_server" )
                [[ "$h_c" == "$h_s" ]] && continue
                local cell="${_results[$key_client->$key_server]:-unknown}"
                [[ "$cell" == "ok" || "$cell" == "self" ]] && continue
                if (( any_failure == 0 )); then
                    echo
                    echo "## Failed Links"
                    echo
                    echo "| Client | Server | Status |"
                    echo "|--------|--------|--------|"
                    any_failure=1
                fi
                local label
                case "$cell" in
                    test_failed)         label="❌ Test failed" ;;
                    server_start_failed) label="🚫 Server did not start" ;;
                    *)                   label="❓ Unknown" ;;
                esac
                echo "| \`$key_client\` | \`$key_server\` | $label |"
            done
        done

        if (( ok_count == total )); then
            echo
            echo "## ✅ All links passed"
        fi

    } > "$report_file"

    log_info "Markdown report written to: $report_file"
}

generate_report "$REPORT_FILE" sorted_keys results

echo
log_info "Done."
