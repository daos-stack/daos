#!/bin/bash

# set -x
set -eu -o pipefail

# ---------------------------------------------------------------------------
# Generic tool description (protocol-independent)
# ---------------------------------------------------------------------------
TOOL_SHORT_DESC="HPC RPC connectivity and performance test using the Mercury framework over UCX"
TOOL_LONG_DESC="Mercury is an HPC Remote Procedure Call (RPC) library designed for \
high-performance computing environments. It abstracts multiple network transports \
(UCX, OFI/libfabric, BMI) and provides both RPC and bulk data transfer \
primitives used by HPC middleware such as DAOS and Mochi. \
This test uses the Mercury built-in performance tools: \`hg_perf_server\` \
listens for incoming RPC connections and \`hg_rate\` connects to it, \
sends RPCs, and reports throughput and latency. This test validates \
not only the IB fabric and IP-over-IB addressing, but also the UCX and Mercury \
software stacks installed on the nodes."

# ---------------------------------------------------------------------------
# UCX protocol registry
# ---------------------------------------------------------------------------
declare -A UCX_PROTO_SHORT_DESC=(
    [dc_mlx5]="Dynamically Connected — scalable, low-QP-count transport (ConnectX)"
    [rc_mlx5]="Reliable Connected — one dedicated QP per connection (ConnectX)"
    [ud_mlx5]="Unreliable Datagram — connectionless, low-overhead transport (ConnectX)"
    [rc_verbs]="Reliable Connected via libibverbs (non-offloaded, portable)"
    [ud_verbs]="Unreliable Datagram via libibverbs (non-offloaded, portable)"
)

declare -A UCX_PROTO_LONG_DESC=(
    [dc_mlx5]="DC (Dynamically Connected) is a Mellanox/NVIDIA ConnectX-specific \
transport that multiplexes many logical connections over a small number of QPs \
using a shared Target (DCT) QP on the server side. It dramatically reduces QP \
memory consumption on large-scale clusters where every node would otherwise need \
one RC QP per peer. DC provides reliable, in-order delivery and is the \
recommended UCX transport for MPI and HPC storage workloads on modern ConnectX \
adapters."
    [rc_mlx5]="RC (Reliable Connected) provides reliable, in-order, \
connection-oriented delivery between a dedicated pair of QPs. Every (client, \
server) pair requires its own QP, which consumes more HCA memory than DC but \
offers maximum compatibility and predictable latency. RC is the classic RDMA \
transport used by MPI, NFS-RDMA, and storage protocols."
    [ud_mlx5]="UD (Unreliable Datagram) is a connectionless transport: a single \
QP can communicate with any number of remote QPs without prior connection \
setup. Packets may be dropped or reordered and there is no retransmission. UD \
consumes the fewest HCA resources and is used for multicast, subnet management \
(MADs), and latency-sensitive small-message workloads where the application \
handles reliability itself."
    [rc_verbs]="RC via the standard libibverbs interface (no Mellanox offload \
extensions). Functionally identical to rc_mlx5 but uses the generic verbs path, \
making it compatible with non-Mellanox HCAs. Performance may be lower than the \
offloaded mlx5 variant."
    [ud_verbs]="UD via the standard libibverbs interface (no Mellanox offload \
extensions). Functionally identical to ud_mlx5 but uses the generic verbs path, \
making it compatible with non-Mellanox HCAs."
)

# Ordered list for display purposes
UCX_PROTO_ORDER=( dc_mlx5 rc_mlx5 ud_mlx5 rc_verbs ud_verbs )

# ---------------------------------------------------------------------------
# Help / usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] <nodeset>

Test InfiniBand RDMA connectivity and Mercury RPC between nodes using
hg_perf_server and hg_rate (Mercury framework, UCX transport).
Nodes are specified as a ClusterShell nodeset expression (e.g. "node[1-4]").

TOOL DESCRIPTION:
  hg_perf_server / hg_rate — ${TOOL_SHORT_DESC}.

  ${TOOL_LONG_DESC}

  Server command : hg_perf_server -c ucx -p <protocol> -d <dev>:<port>
                     -H <server-ip> -P <server-port> -V
  Client command : hg_rate -f ./port.cfg -c ucx -p <protocol>
                     -d <dev>:<port> -H <client-ip> -V

OPTIONS:
  -o, --output FILE       Markdown report output file (default: mercury-report.md)
                          Can also be set via the REPORT_FILE environment variable.
  -P, --server-port PORT  TCP/IP port the server listens on (default: 30999)
                          Can also be set via the MERCURY_SERVER_PORT environment
                          variable.
  -p, --protocol PROTO    UCX protocol passed to -p (default: dc_mlx5)
                          Can also be set via the MERCURY_PROTO environment variable.
  -t, --timeout SECS      Client-side timeout in seconds (default: 30)
                          Can also be set via the MERCURY_TIMEOUT environment
                          variable.
  -h, --help              Show this help message and exit.

SUPPORTED UCX PROTOCOLS:
EOF
    for proto in "${UCX_PROTO_ORDER[@]}"; do
        printf "  %-10s  %s\n" "$proto" "${UCX_PROTO_SHORT_DESC[$proto]}"
    done
    cat <<EOF

PROTOCOL DETAILS:
EOF
    for proto in "${UCX_PROTO_ORDER[@]}"; do
        echo "  $proto — ${UCX_PROTO_SHORT_DESC[$proto]}"
        echo "${UCX_PROTO_LONG_DESC[$proto]}" \
            | fold -s -w 72 \
            | sed 's/^/        /'
        echo
    done
    cat <<EOF
EXAMPLES:
  $(basename "$0") node[1-4]
  $(basename "$0") --protocol dc_mlx5 --output report.md node[1-4]
  $(basename "$0") --protocol ud_mlx5 --server-port 31000 --timeout 60 node[1-4]
  MERCURY_PROTO=rc_mlx5 REPORT_FILE=out.md $(basename "$0") node[1-4]

REQUIREMENTS:
  - nodeset        (ClusterShell)
  - ibdev2netdev   available on each remote host
  - hg_perf_server and hg_rate  available on each remote host (Mercury package)
  - UCX installed with the requested protocol support on each remote host
  - Each IB interface must have an IP address assigned (IPoIB or RoCE)
  - Passwordless SSH access as root to all target nodes
  - scp access between nodes (port.cfg transfer from server to client)

OUTPUT:
  A Markdown file containing:
  - A test summary table (date, hosts, protocol, pass/fail counts)
  - A generic description of the Mercury tool
  - A description of the specific UCX protocol under test
  - A section explaining what is tested
  - A connection matrix (client × server) with per-cell status icons
  - A table of failed links with their failure reason

EXIT CODES:
  0   All tests passed (or completed without a fatal error)
  1   Fatal error (e.g. unable to kill a stale hg_perf_server process)
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
REPORT_FILE="${REPORT_FILE:-mercury-report.md}"
MERCURY_SERVER_PORT="${MERCURY_SERVER_PORT:-30999}"
MERCURY_PROTO="${MERCURY_PROTO:-dc_mlx5}"
MERCURY_TIMEOUT="${MERCURY_TIMEOUT:-30}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            REPORT_FILE="$2"; shift 2 ;;
        -P|--server-port)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            MERCURY_SERVER_PORT="$2"; shift 2 ;;
        -p|--protocol)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            MERCURY_PROTO="$2"; shift 2 ;;
        -t|--timeout)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            MERCURY_TIMEOUT="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        -*)
            echo "[ERROR] Unknown option: $1" >&2; echo >&2; usage >&2; exit 1 ;;
        *)
            break ;;
    esac
done

if [[ $# -eq 0 ]]; then
    echo "[ERROR] No nodeset specified." >&2
    echo >&2
    usage >&2
    exit 1
fi

# Resolve protocol description — fall back gracefully for unknown protocols
PROTO_SHORT="${UCX_PROTO_SHORT_DESC[$MERCURY_PROTO]:-"Custom/unknown protocol"}"
PROTO_LONG="${UCX_PROTO_LONG_DESC[$MERCURY_PROTO]:-"No detailed description is available for protocol '\`$MERCURY_PROTO\`'. \
Please refer to the UCX documentation for details on this transport."}"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log_info()  { echo "[INFO] $*" ; }
log_warn()  { echo "[WARN] $*" ; }
log_error() { echo "[ERROR] $*" ; }
log_fatal() { echo ; echo "[FATAL] $*" ; exit 1 ; }

function kill_cmd() {
    local hostname="$1"
    local cmd="$2"

    if ! ssh -n root@"$hostname" pgrep "$cmd" &>/dev/null; then
        return
    fi

    log_info "Killing $cmd on host $hostname"
    ssh -n root@"$hostname" pkill "$cmd"
    sleep 1

    if ssh -n root@"$hostname" pgrep "$cmd" &>/dev/null; then
        log_warn "Hard killing $cmd on host $hostname"
        ssh -n root@"$hostname" pkill -9 "$cmd"
        sleep 3
    fi

    if ssh -n root@"$hostname" pgrep "$cmd" &>/dev/null; then
        log_fatal "Not able to kill $cmd on host $hostname"
    fi
}

# ---------------------------------------------------------------------------
# Discover host / interface information
# ---------------------------------------------------------------------------
log_info "Discovering host information..."
log_info "UCX protocol  : $MERCURY_PROTO — $PROTO_SHORT"
log_info "Server port   : $MERCURY_SERVER_PORT"
log_info "Client timeout: ${MERCURY_TIMEOUT}s"

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

mapfile -t sorted_keys < <( printf '%s\n' "${!hosts[@]}" | sort )

if [[ ${#sorted_keys[@]} -eq 0 ]]; then
    log_fatal "No active InfiniBand interfaces with IP addresses found on the specified hosts."
fi

# ---------------------------------------------------------------------------
# Run Mercury tests and collect results
# ---------------------------------------------------------------------------
# results["client_key->server_key"] = "ok" | "server_start_failed" |
#                                      "scp_failed" | "test_failed" | "self"
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
        client_ip=$(   cut -d: -f3 <<<"${hosts[$key1]}" )

        echo
        kill_cmd "$server_hostname" hg_perf_server
        log_info "Starting hg_perf_server: host=$server_hostname, interface=$server_if"
        ssh -n root@"$server_hostname" \
            "hg_perf_server -c ucx -p $MERCURY_PROTO \
             -d $server_dev:$server_port -H $server_ip \
             -P $MERCURY_SERVER_PORT -V \
             &>/tmp/mercury-server.log &"

        timeout=3
        while ! ssh -n root@"$server_hostname" pgrep hg_perf_server &>/dev/null \
              && (( timeout > 0 )); do
            sleep 1
            (( timeout-- )) || true
        done

        if ! ssh -n root@"$server_hostname" pgrep hg_perf_server &>/dev/null; then
            echo
            log_error "Could not start hg_perf_server: host=$server_hostname, interface=$server_if"
            log_error "Server command : hg_perf_server -c ucx -p $MERCURY_PROTO -d $server_dev:$server_port -H $server_ip -P $MERCURY_SERVER_PORT -V"
            log_error "Server log:"
            ssh -n root@"$server_hostname" cat /tmp/mercury-server.log
            results["$key1->$key0"]="server_start_failed"
            continue
        fi

        server_pid=$( ssh -n root@"$server_hostname" pgrep hg_perf_server )
        log_info "Server started with PID: $server_pid"

        log_info "    - Copying port.cfg from server to client"
        if ! scp -q root@"$server_hostname":./port.cfg root@"$client_hostname":./port.cfg ; then
            echo
            log_error "    - Failed to copy port.cfg from $server_hostname to $client_hostname"
            results["$key1->$key0"]="scp_failed"
            kill_cmd "$server_hostname" hg_perf_server
            continue
        fi

        kill_cmd "$client_hostname" hg_rate
        log_info "    - Running hg_rate from client: host=$client_hostname, interface=$client_if"

        if ! ssh -n root@"$client_hostname" \
                "timeout --kill-after=3s ${MERCURY_TIMEOUT}s bash -c \
                 'hg_rate -f ./port.cfg -c ucx -p $MERCURY_PROTO \
                  -d $client_dev:$client_port -H $client_ip -V \
                  &>/tmp/mercury-client.log'" ; then
            echo
            log_error "    - $client_hostname:$client_if =X=> $server_hostname:$server_if"
            log_error "Server command : hg_perf_server -c ucx -p $MERCURY_PROTO -d $server_dev:$server_port -H $server_ip -P $MERCURY_SERVER_PORT -V"
            log_error "Client command : hg_rate -f ./port.cfg -c ucx -p $MERCURY_PROTO -d $client_dev:$client_port -H $client_ip -V"
            log_error "Client log:"
            ssh -n root@"$client_hostname" cat /tmp/mercury-client.log
            results["$key1->$key0"]="test_failed"
        else
            results["$key1->$key0"]="ok"
        fi

        kill_cmd "$server_hostname" hg_perf_server
    done
done

# ---------------------------------------------------------------------------
# Generate Markdown report
# ---------------------------------------------------------------------------
generate_report() {
    local report_file="$1"
    local -n _keys="$2"
    local -n _results="$3"

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
        echo "# Mercury / UCX InfiniBand Test Report"
        echo
        # ── Test summary ──────────────────────────────────────────────────
        echo "## Test Summary"
        echo
        echo "| Parameter | Value |"
        echo "|-----------|-------|"
        echo "| Date | $( date -u '+%Y-%m-%d %H:%M:%S UTC' ) |"
        echo "| Server tool | \`hg_perf_server\` |"
        echo "| Client tool | \`hg_rate\` |"
        echo "| UCX protocol | \`$MERCURY_PROTO\` — $PROTO_SHORT |"
        echo "| Server port | \`$MERCURY_SERVER_PORT\` |"
        echo "| Client timeout | ${MERCURY_TIMEOUT}s |"
        echo "| Hosts | $( printf '%s\n' "${_keys[@]}" | cut -d: -f1 | sort -u | tr '\n' ',' | sed 's/,$//' | sed 's/,/, /g' ) |"
        echo "| Total links tested | ${total} |"
        echo "| Passed | ${ok_count} |"
        echo "| Failed | $(( total - ok_count )) |"
        echo
        # ── Tool description (generic) ────────────────────────────────────
        echo "## Tool Description"
        echo
        echo "### hg_perf_server / hg_rate — ${TOOL_SHORT_DESC}"
        echo
        echo "${TOOL_LONG_DESC}" | fold -s -w 80
        echo
        echo "**Server command:**"
        echo "\`\`\`"
        echo "hg_perf_server -c ucx -p $MERCURY_PROTO -d <dev>:<port> -H <server-ip> -P $MERCURY_SERVER_PORT -V"
        echo "\`\`\`"
        echo
        echo "**Client command:**"
        echo "\`\`\`"
        echo "hg_rate -f ./port.cfg -c ucx -p $MERCURY_PROTO -d <dev>:<port> -H <client-ip> -V"
        echo "\`\`\`"
        echo
        # ── Protocol under test (dynamic) ────────────────────────────────
        echo "## Protocol Under Test"
        echo
        echo "### \`$MERCURY_PROTO\` — $PROTO_SHORT"
        echo
        echo "$PROTO_LONG" | fold -s -w 80
        echo
        # ── What is tested ────────────────────────────────────────────────
        echo "## What Is Tested"
        echo
        cat <<'WHAT'
Each active InfiniBand interface (with an assigned IP address) on every node is
exercised as both a **server** and a **client**. For every (client, server) pair
across different hosts:

1. A `hg_perf_server` process is started on the server node, binding to its IB
   interface IP address and listening for Mercury RPC connections on the
   configured port.
2. The server writes a `port.cfg` file in its home directory describing its
   listening endpoint. This file is copied from the server to the client via
   `scp` so that `hg_rate` can locate the server without hardcoding an address.
3. A `hg_rate` process is started on the client node. It reads `port.cfg`,
   connects to the server over UCX, sends a stream of RPCs, and reports
   throughput and latency before exiting.
4. The client runs under a `timeout` wrapper to prevent the test from hanging
   indefinitely if the connection cannot be established.
5. The test is marked **passed** (✅) if the client exits with code 0, or
   **failed** otherwise (❌ for client failure, 📋 for `port.cfg` copy failure,
   🚫 for server start failure).
6. Server and client processes are cleaned up after each pair, regardless of
   outcome.

Same-host pairs are skipped (—). IP addresses on IB interfaces are required
(IPoIB or RoCE).
WHAT
        echo
        # ── Legend ───────────────────────────────────────────────────────
        echo "## Legend"
        echo
        echo "| Symbol | Meaning |"
        echo "|--------|---------|"
        echo "| ✅ | Connection OK |"
        echo "| ❌ | Client test failed or timed out |"
        echo "| 📋 | port.cfg copy (scp) failed |"
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
                        scp_failed)          symbol="📋" ;;
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
                    test_failed)         label="❌ Client failed or timed out" ;;
                    scp_failed)          label="📋 port.cfg copy failed" ;;
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
