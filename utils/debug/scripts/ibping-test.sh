#!/bin/bash

# set -x
set -eu -o pipefail

# ---------------------------------------------------------------------------
# Tool description
# ---------------------------------------------------------------------------
TOOL_SHORT_DESC="InfiniBand ping utility using MAD (Management Datagrams)"
TOOL_LONG_DESC="ibping probes an InfiniBand port by sending Management Datagram (MAD) \
packets directly to a target Port GUID. Because it operates at the IB subnet-management \
layer it does not require IP addresses on the interfaces — only an active IB link and \
subnet manager. It is typically the first connectivity test performed on a new fabric, \
validating that the physical layer, SFP/QSFP cables, and subnet manager routing tables \
are all working correctly."

# ---------------------------------------------------------------------------
# Help / usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] <nodeset>

Test InfiniBand connectivity between nodes using ibping.
Nodes are specified as a ClusterShell nodeset expression (e.g. "node[1-4]").

TOOL DESCRIPTION:
  ibping — ${TOOL_SHORT_DESC}.

  ${TOOL_LONG_DESC}

  Server command : ibping -S -C <dev> -P <port>
  Client command : ibping -e -c <count> -C <dev> -P <port> -G <server-guid>

OPTIONS:
  -o, --output FILE     Markdown report output file (default: ibping-report.md)
                        Can also be set via the REPORT_FILE environment variable.
  -c, --count N         Number of packets sent per ibping test (default: 3)
                        Can also be set via the IBPING_COUNT environment variable.
  -h, --help            Show this help message and exit.

EXAMPLES:
  $(basename "$0") node[1-4]
  $(basename "$0") --count 10 --output report.md node[1-4]
  REPORT_FILE=out.md IBPING_COUNT=5 $(basename "$0") node[1-4]

REQUIREMENTS:
  - nodeset  (ClusterShell)
  - ibdev2netdev, ibstat, ibping  available on each remote host
  - A running subnet manager (e.g. OpenSM) on the fabric
  - Passwordless SSH access as root to all target nodes

OUTPUT:
  A Markdown file containing:
  - A test summary table (date, hosts, pass/fail counts)
  - A description of the tool and what is tested
  - A connection matrix (client × server) with per-cell status icons
  - A table of failed links with their failure reason

EXIT CODES:
  0   All tests passed (or completed without a fatal error)
  1   Fatal error (e.g. unable to kill a stale ibping process)
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
REPORT_FILE="${REPORT_FILE:-ibping-report.md}"
IBPING_COUNT="${IBPING_COUNT:-3}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            REPORT_FILE="$2"; shift 2 ;;
        -c|--count)
            [[ $# -lt 2 ]] && { echo "[ERROR] Option $1 requires an argument." >&2; usage >&2; exit 1; }
            IBPING_COUNT="$2"; shift 2 ;;
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

function kill_ibping() {
    local hostname="$1"

    if ! ssh -n root@"$hostname" pgrep ibping &>/dev/null; then
        return
    fi

    log_info "Killing ibping on host $hostname"
    ssh -n root@"$hostname" pkill ibping
    sleep 1

    if ssh -n root@"$hostname" pgrep ibping &>/dev/null; then
        log_warn "Hard killing ibping on host $hostname"
        ssh -n root@"$hostname" pkill -9 ibping
        sleep 3
    fi

    if ssh -n root@"$hostname" pgrep ibping &>/dev/null; then
        log_fatal "Not able to kill ibping on host $hostname"
    fi
}

# ---------------------------------------------------------------------------
# Discover host / interface information
# ---------------------------------------------------------------------------
log_info "Discovering host information..."

declare -A hosts   # hosts["hostname:if"] = "dev:port:guid"

for hostname in $( nodeset -e "$@" ); do
    log_info "    - Discovering IB information of host $hostname"
    while IFS= read -r line0; do
        grep -qE 'Up' <<<"$line0" || continue

        local_dev=$(  awk '{ print $1 }' <<<"$line0" )
        local_port=$( awk '{ print $3 }' <<<"$line0" )
        local_if=$(   awk '{ print $5 }' <<<"$line0" )
        local_guid=$( ssh -n root@"$hostname" ibstat "$local_dev" "$local_port" \
                      | sed -n -E "/GUID/s/^Port GUID: (0x[[:alnum:]]+)$/\1/p" )

        hosts["$hostname:$local_if"]="$local_dev:$local_port:$local_guid"
        log_info "        Found: interface=$local_if, dev=$local_dev, port=$local_port, guid=$local_guid"
    done < <( ssh -n root@"$hostname" ibdev2netdev )
done

mapfile -t sorted_keys < <( printf '%s\n' "${!hosts[@]}" | sort )

if [[ ${#sorted_keys[@]} -eq 0 ]]; then
    log_fatal "No active InfiniBand interfaces found on the specified hosts."
fi

# ---------------------------------------------------------------------------
# Run ibping tests and collect results
# ---------------------------------------------------------------------------
declare -A results

for key0 in "${sorted_keys[@]}"; do
    for key1 in "${sorted_keys[@]}"; do
        h0=$( cut -d: -f1 <<<"$key0" )
        h1=$( cut -d: -f1 <<<"$key1" )
        [[ "$h0" == "$h1" ]] && results["$key1->$key0"]="self"
    done
done

for key0 in "${sorted_keys[@]}"; do
    server_hostname=$( cut -d: -f1 <<<"$key0" )
    server_if=$(       cut -d: -f2 <<<"$key0" )
    server_dev=$(      cut -d: -f1 <<<"${hosts[$key0]}" )
    server_port=$(     cut -d: -f2 <<<"${hosts[$key0]}" )
    server_guid=$(     cut -d: -f3 <<<"${hosts[$key0]}" )

    echo
    kill_ibping "$server_hostname"

    log_info "Starting ibping server: host=$server_hostname, interface=$server_if"
    ssh -n root@"$server_hostname" \
        "ibping -S -C $server_dev -P $server_port &>/tmp/ibping-server.log &"

    timeout=3
    while ! ssh -n root@"$server_hostname" pgrep ibping &>/dev/null \
          && (( timeout > 0 )); do
        sleep 1
        (( timeout-- )) || true
    done

    if ! ssh -n root@"$server_hostname" pgrep ibping &>/dev/null; then
        echo
        log_error "Could not start ibping server: host=$server_hostname, interface=$server_if"
        log_error "Server command : ibping -S -C $server_dev -P $server_port"
        log_error "Server log:"
        ssh -n root@"$server_hostname" cat /tmp/ibping-server.log
        for key1 in "${sorted_keys[@]}"; do
            client_hostname=$( cut -d: -f1 <<<"$key1" )
            [[ "$client_hostname" == "$server_hostname" ]] && continue
            results["$key1->$key0"]="server_start_failed"
        done
        continue
    fi

    server_pid=$( ssh -n root@"$server_hostname" pgrep ibping )
    log_info "Server started with PID: $server_pid"

    for key1 in "${sorted_keys[@]}"; do
        client_hostname=$( cut -d: -f1 <<<"$key1" )
        [[ "$client_hostname" == "$server_hostname" ]] && continue

        client_if=$(   cut -d: -f2 <<<"$key1" )
        client_dev=$(  cut -d: -f1 <<<"${hosts[$key1]}" )
        client_port=$( cut -d: -f2 <<<"${hosts[$key1]}" )

        kill_ibping "$client_hostname"
        log_info "    - Pinging server from client: host=$client_hostname, interface=$client_if"

        if ! ssh -n root@"$client_hostname" \
                "ibping -e -c $IBPING_COUNT -C $client_dev -P $client_port \
                 -G $server_guid &>/tmp/ibping-client.log" ; then
            echo
            log_error "    - $client_hostname:$client_if =X=> $server_hostname:$server_if"
            log_error "Server command : ibping -S -C $server_dev -P $server_port"
            log_error "Client command : ibping -e -c $IBPING_COUNT -C $client_dev -P $client_port -G $server_guid"
            log_error "Client log:"
            ssh -n root@"$client_hostname" cat /tmp/ibping-client.log
            results["$key1->$key0"]="ping_failed"
            continue
        fi

        if ! ssh -n root@"$client_hostname" \
                "grep -qE '${IBPING_COUNT} received, 0% packet loss' /tmp/ibping-client.log" ; then
            echo
            log_error "    - $client_hostname:$client_if =X=> $server_hostname:$server_if  (packet loss)"
            log_error "Server command : ibping -S -C $server_dev -P $server_port"
            log_error "Client command : ibping -e -c $IBPING_COUNT -C $client_dev -P $client_port -G $server_guid"
            log_error "Client log:"
            ssh -n root@"$client_hostname" cat /tmp/ibping-client.log
            results["$key1->$key0"]="packet_loss"
            continue
        fi

        results["$key1->$key0"]="ok"
    done

    kill_ibping "$server_hostname"
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
        echo "# InfiniBand ibping Test Report"
        echo
        # ── Test summary ──────────────────────────────────────────────────
        echo "## Test Summary"
        echo
        echo "| Parameter | Value |"
        echo "|-----------|-------|"
        echo "| Date | $( date -u '+%Y-%m-%d %H:%M:%S UTC' ) |"
        echo "| Tool | \`ibping\` |"
        echo "| Packets per test | ${IBPING_COUNT} |"
        echo "| Hosts | $( printf '%s\n' "${_keys[@]}" | cut -d: -f1 | sort -u | tr '\n' ',' | sed 's/,$//' | sed 's/,/, /g' ) |"
        echo "| Total links tested | ${total} |"
        echo "| Passed | ${ok_count} |"
        echo "| Failed | $(( total - ok_count )) |"
        echo
        # ── Tool description ──────────────────────────────────────────────
        echo "## Tool Description"
        echo
        echo "### ibping — ${TOOL_SHORT_DESC}"
        echo
        echo "${TOOL_LONG_DESC}" | fold -s -w 80
        echo
        echo "**Server command:** \`ibping -S -C <dev> -P <port>\`"
        echo
        echo "**Client command:** \`ibping -e -c <count> -C <dev> -P <port> -G <server-guid>\`"
        echo
        # ── What is tested ────────────────────────────────────────────────
        echo "## What Is Tested"
        echo
        cat <<'WHAT'
Each active InfiniBand port on every node is exercised as both a **server** and
a **client**. For every (client, server) pair across different hosts:

1. The server Port GUID is retrieved via `ibstat` and the server process is
   started, listening for MAD ping packets.
2. The client sends a fixed number of MAD ping packets to the server Port GUID.
3. The test is marked **passed** (✅) if the client exits with code 0 **and**
   the client log confirms 0% packet loss, or **failed** otherwise.
4. Server and client processes are cleaned up after each server iteration,
   regardless of outcome.

Same-host pairs are skipped (—) since loopback is not meaningful here.
No IP addresses are required — only active IB links and a running subnet manager.
WHAT
        echo
        # ── Legend ───────────────────────────────────────────────────────
        echo "## Legend"
        echo
        echo "| Symbol | Meaning |"
        echo "|--------|---------|"
        echo "| ✅ | Connection OK (0% packet loss) |"
        echo "| ❌ | Ping command failed |"
        echo "| ⚠️  | Packet loss detected |"
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
                        ping_failed)         symbol="❌" ;;
                        packet_loss)         symbol="⚠️"  ;;
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
                    ping_failed)         label="❌ Ping failed" ;;
                    packet_loss)         label="⚠️  Packet loss" ;;
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
