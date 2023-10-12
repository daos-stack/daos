#!/bin/bash

# set -x
set -eu -o pipefail

# ---------------------------------------------------------------------------
# Tool description
# ---------------------------------------------------------------------------
TOOL_SHORT_DESC="RDMA CM ping-pong test over a Reliable Connected (RC) QP"
TOOL_LONG_DESC="rping tests end-to-end RDMA connectivity by establishing a \
Reliable Connected (RC) Queue Pair via the RDMA Connection Manager (RDMA CM) \
and performing RDMA Write ping-pong exchanges between two nodes. It exercises \
the full RDMA stack — IP/RDMA CM address resolution, RC QP creation, memory \
registration, and RDMA Write operations — making it a thorough validation of \
both the fabric and the host RDMA software stack. rping is part of the \
librdmacm test utilities included in the rdma-core package."

# ---------------------------------------------------------------------------
# Help / usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] <nodeset>

Test InfiniBand RDMA connectivity between nodes using rping.
Nodes are specified as a ClusterShell nodeset expression (e.g. "node[1-4]").

TOOL DESCRIPTION:
  rping — ${TOOL_SHORT_DESC}.

  ${TOOL_LONG_DESC}

  Server command : rping -v -s -P -a <server-ip>
  Client command : rping -v -C <count> -c -I <client-ip> -a <server-ip>

OPTIONS:
  -o, --output FILE     Markdown report output file (default: rping-report.md)
                        Can also be set via the REPORT_FILE environment variable.
  -c, --count N         Number of ping-pong iterations per test (default: 10)
                        Can also be set via the RPING_COUNT environment variable.
  -h, --help            Show this help message and exit.

EXAMPLES:
  $(basename "$0") node[1-4]
  $(basename "$0") --count 20 --output report.md node[1-4]
  REPORT_FILE=out.md RPING_COUNT=5 $(basename "$0") node[1-4]

REQUIREMENTS:
  - nodeset      (ClusterShell)
  - ibdev2netdev, rping  available on each remote host
  - Each IB interface must have an IP address assigned (IPoIB or RoCE)
  - Passwordless SSH access as root to all target nodes

OUTPUT:
  A Markdown file containing:
  - A test summary table (date, hosts, iterations, pass/fail counts)
  - A description of the tool and what is tested
  - A connection matrix (client × server) with per-cell status icons
  - A table of failed links with their failure reason

EXIT CODES:
  0   All tests passed (or completed without a fatal error)
  1   Fatal error (e.g. unable to kill a stale rping process)
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
REPORT_FILE="${REPORT_FILE:-rping-report.md}"
RPING_COUNT="${RPING_COUNT:-10}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            REPORT_FILE="$2"; shift 2 ;;
        -c|--count)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            RPING_COUNT="$2"; shift 2 ;;
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

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log_info()  { echo "[INFO] $*" ; }
log_warn()  { echo "[WARN] $*" ; }
log_error() { echo "[ERROR] $*" ; }
log_fatal() { echo ; echo "[FATAL] $*" ; exit 1 ; }

function kill_rping() {
    local hostname="$1"

    if ! ssh -n root@"$hostname" pgrep rping &>/dev/null; then
        return
    fi

    log_info "Killing rping on host $hostname"
    ssh -n root@"$hostname" pkill rping
    sleep 1

    if ssh -n root@"$hostname" pgrep rping &>/dev/null; then
        log_warn "Hard killing rping on host $hostname"
        ssh -n root@"$hostname" pkill -9 rping
        sleep 3
    fi

    if ssh -n root@"$hostname" pgrep rping &>/dev/null; then
        log_fatal "Not able to kill rping on host $hostname"
    fi
}

# ---------------------------------------------------------------------------
# Discover host / interface information
# ---------------------------------------------------------------------------
log_info "Discovering host information..."

declare -A hosts   # hosts["hostname:if"] = "dev:ip"

for hostname in $( nodeset -e "$@" ); do
    log_info "    - Discovering IB information of host $hostname"
    while IFS= read -r line0; do
        grep -qE 'Up' <<<"$line0" || continue

        local_dev=$( awk '{ print $1 }' <<<"$line0" )
        local_if=$(  awk '{ print $5 }' <<<"$line0" )
        local_ip=$(  ssh -n root@"$hostname" ip address show "$local_if" \
                     | sed -n -E '/inet/s#^[[:blank:]]+inet[[:space:]]+([0-9][0-9.]+)/.+$#\1#p' )

        if [[ -z "$local_ip" ]]; then
            log_warn "        No IP address found for interface $local_if on $hostname — skipping."
            continue
        fi

        hosts["$hostname:$local_if"]="$local_dev:$local_ip"
        log_info "        Found: interface=$local_if, dev=$local_dev, ip=$local_ip"
    done < <( ssh -n root@"$hostname" ibdev2netdev )
done

mapfile -t sorted_keys < <( printf '%s\n' "${!hosts[@]}" | sort )

if [[ ${#sorted_keys[@]} -eq 0 ]]; then
    log_fatal "No active InfiniBand interfaces with IP addresses found on the specified hosts."
fi

# ---------------------------------------------------------------------------
# Run rping tests and collect results
# ---------------------------------------------------------------------------
declare -A results

for key0 in "${sorted_keys[@]}"; do
    server_hostname=$( cut -d: -f1 <<<"$key0" )
    server_if=$(       cut -d: -f2 <<<"$key0" )
    server_dev=$(      cut -d: -f1 <<<"${hosts[$key0]}" )
    server_ip=$(       cut -d: -f2 <<<"${hosts[$key0]}" )

    echo
    kill_rping "$server_hostname"

    log_info "Starting rping server: host=$server_hostname, interface=$server_if"
    ssh -n root@"$server_hostname" \
        "rping -v -s -P -a $server_ip &>/tmp/rping-server.log &"

    timeout=3
    while ! ssh -n root@"$server_hostname" pgrep rping &>/dev/null \
          && (( timeout > 0 )); do
        sleep 1
        (( timeout-- )) || true
    done

    if ! ssh -n root@"$server_hostname" pgrep rping &>/dev/null; then
        echo
        log_error "Could not start rping server: host=$server_hostname, interface=$server_if"
        log_error "Server command : rping -v -s -P -a $server_ip"
        log_error "Server log:"
        ssh -n root@"$server_hostname" cat /tmp/rping-server.log
        for key1 in "${sorted_keys[@]}"; do
            client_hostname=$( cut -d: -f1 <<<"$key1" )
            [[ "$client_hostname" == "$server_hostname" ]] && continue
            results["$key1->$key0"]="server_start_failed"
        done
        continue
    fi

    server_pid=$( ssh -n root@"$server_hostname" pgrep rping )
    log_info "Server started with PID: $server_pid"

    for key1 in "${sorted_keys[@]}"; do
        client_hostname=$( cut -d: -f1 <<<"$key1" )

        if [[ "$client_hostname" == "$server_hostname" ]]; then
            results["$key1->$key0"]="self"
            continue
        fi

        client_if=$(  cut -d: -f2 <<<"$key1" )
        client_dev=$( cut -d: -f1 <<<"${hosts[$key1]}" )
        client_ip=$(  cut -d: -f2 <<<"${hosts[$key1]}" )

        kill_rping "$client_hostname"
        log_info "    - Pinging server from client: host=$client_hostname, interface=$client_if"

        if ! ssh -n root@"$client_hostname" \
                "rping -v -C $RPING_COUNT -c -I $client_ip -a $server_ip \
                 &>/tmp/rping-client.log" ; then
            echo
            log_error "    - $client_hostname:$client_if =X=> $server_hostname:$server_if"
            log_error "Server command : rping -v -s -P -a $server_ip"
            log_error "Client command : rping -v -C $RPING_COUNT -c -I $client_ip -a $server_ip"
            log_error "Client log:"
            ssh -n root@"$client_hostname" cat /tmp/rping-client.log
            results["$key1->$key0"]="test_failed"
            continue
        fi

        results["$key1->$key0"]="ok"
    done

    kill_rping "$server_hostname"
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
        echo "# InfiniBand rping Test Report"
        echo
        # ── Test summary ──────────────────────────────────────────────────
        echo "## Test Summary"
        echo
        echo "| Parameter | Value |"
        echo "|-----------|-------|"
        echo "| Date | $( date -u '+%Y-%m-%d %H:%M:%S UTC' ) |"
        echo "| Tool | \`rping\` |"
        echo "| Iterations per test | ${RPING_COUNT} |"
        echo "| Hosts | $( printf '%s\n' "${_keys[@]}" | cut -d: -f1 | sort -u | tr '\n' ',' | sed 's/,$//' | sed 's/,/, /g' ) |"
        echo "| Total links tested | ${total} |"
        echo "| Passed | ${ok_count} |"
        echo "| Failed | $(( total - ok_count )) |"
        echo
        # ── Tool description ──────────────────────────────────────────────
        echo "## Tool Description"
        echo
        echo "### rping — ${TOOL_SHORT_DESC}"
        echo
        echo "${TOOL_LONG_DESC}" | fold -s -w 80
        echo
        echo "**Server command:** \`rping -v -s -P -a <server-ip>\`"
        echo
        echo "**Client command:** \`rping -v -C <count> -c -I <client-ip> -a <server-ip>\`"
        echo
        # ── What is tested ────────────────────────────────────────────────
        echo "## What Is Tested"
        echo
        cat <<'WHAT'
Each active InfiniBand interface (with an assigned IP address) on every node is
exercised as both a **server** and a **client**. For every (client, server) pair
across different hosts:

1. A server process is started on the server node, binding to its IB interface
   IP address and waiting for an RDMA CM connection.
2. A client process is started on the client node, connecting to the server via
   RDMA CM, then performing a configurable number of RDMA Write ping-pong
   iterations before disconnecting and exiting.
3. The test is marked **passed** (✅) if the client exits with code 0, or
   **failed** (❌) otherwise.
4. Server and client processes are cleaned up after each server iteration,
   regardless of outcome.

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
