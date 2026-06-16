#!/bin/bash

#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#

set -eux

: "${STAGE_NAME:=junit_file_create}"

junit_sanitized_stage() {
    local stage="${STAGE_NAME}"

    stage="$(echo "$stage" | sed 's/[^a-zA-Z0-9_]/_/g' | sed 's/__*/_/g')"
    echo "${stage:-unknown_stage}"
}

junit_classname() {
    # Keep package stable and class readable for Jenkins views.
    echo "infrastructure.$(junit_sanitized_stage)"
}

junit_result() {
    local name="$1"
    local msg="$2"
    local stacktrace="${3:-}"
    local classname
    classname="$(junit_classname)"
    if [ -z "$stacktrace" ] && declare -F stacktrace > /dev/null; then
        stacktrace="$(stacktrace "Called from" 1 || true)"
    fi

    echo -e "$msg"

    cat <<EOF > results.xml
    <testcase classname="$classname" name="$name" time="0">
    <failure message="$msg" type="TestFail"><![CDATA[$stacktrace]]></failure>
  </testcase>
EOF
}

junit_pass_result() {
    local name="$1"
    local msg="${2:-success}"
        local classname
        classname="$(junit_classname)"

    echo -e "$msg"

    cat <<EOF > results.xml
    <testcase classname="$classname" name="$name" time="0">
    <system-out><![CDATA[$msg]]></system-out>
  </testcase>
EOF
}

junit_on_error() {
    local rc=$?

    # Prevent recursive ERR trap loops while collecting error diagnostics.
    trap - ERR
    set +e

    local step="${DAOS_FAILURE_STEP:-unknown}"
    local cmd="${BASH_COMMAND:-unknown}"
    local msg="Unhandled error in ${STAGE_NAME} step=${step} rc=${rc}"
    local test_name="${JUNIT_TESTCASE_NAME:-UnhandledError}"
    local trace

    trace="Failing command: ${cmd}"
    if declare -F stacktrace > /dev/null; then
        trace+=$'\n'
        trace+="$(stacktrace "Called from" 1 || true)"
    fi

    junit_result "$test_name" "$msg" "$trace" || true
}

expand_junit_nodes() {
    local nodes="$1"
    local expanded=""

    if command -v nodeset > /dev/null 2>&1; then
        expanded="$(nodeset -e "$nodes" 2>/dev/null || true)"
    fi

    if [ -n "$expanded" ]; then
        tr ' ' '\n' <<< "$expanded" | sed '/^$/d'
        return
    fi

    tr ',' '\n' <<< "$nodes" | sed '/^$/d'
}

compute_node_position() {
    local nodelist="$1"
    local target_node="$2"
    local pos=0

    for node in ${nodelist//,/ }; do
        ((pos++)) || true
        if [ "$node" = "$target_node" ]; then
            echo "$pos"
            return 0
        fi
    done
    echo "0"
    return 1
}

count_xml_tag() {
    local tag="$1"
    shift

    # grep exits 1 on no matches; force a zero count instead of
    # triggering ERR trap.
    (grep -Eho "<${tag}([[:space:]>])" "$@" || true) | wc -l
}

report_junit() {
    local name="$1"
    local results="$2"
    local nodes="$3"
    local rcopy_rc=0
    local artifacts_rc=0
    local missing_results=0
    local tests=0
    local failures=0
    local errors=0
    local skipped=0

    local results_files
    local expected_nodes
    local node
    local short_node
    local file
    local result_node
    local existing_node_results=0
    local class_name
    local test_name_base

    class_name="$(junit_classname)"
    test_name_base="${JUNIT_TESTCASE_BASE:-$name}"

    if ! clush -o '-i ci_key' -l root -w "$nodes" --rcopy "$results"; then
        rcopy_rc=$?
        echo "ERROR: Failed to copy $results from nodes=$nodes rc=$rcopy_rc"
    fi

    readarray -t results_files < <(find . -maxdepth 1 -name "$results.*")
    readarray -t expected_nodes < <(expand_junit_nodes "$nodes")

    declare -A node_has_result=()
    for file in "${results_files[@]}"; do
        result_node="$(basename "$file")"
        result_node="${result_node#"$results."}"
        node_has_result["$result_node"]=1
        node_has_result["${result_node%%.*}"]=1
    done

    local node_pos=0
    for node in "${expected_nodes[@]}"; do
        ((node_pos++)) || true
        short_node="${node%%.*}"
        if [ -n "${node_has_result[$node]:-}" ] || [ -n "${node_has_result[$short_node]:-}" ]; then
            ((existing_node_results++)) || true
            continue
        fi

        missing_results=1
        file="./${results}.${node}"
        cat <<EOF > "$file"
    <testcase classname="$class_name" name="$test_name_base Node $node_pos" time="0">
    <error message="Missing node JUnit results" type="InfrastructureError"><![CDATA[Unable to retrieve $results from node=$node]]></error>
  </testcase>
EOF
        results_files+=("$file")
    done

    if [ ${#results_files[@]} -eq 0 ]; then
        missing_results=1
        file="./${results}.unknown"
        cat <<EOF > "$file"
    <testcase classname="$class_name" name="$test_name_base" time="0">
    <error message="No JUnit results were generated" type="InfrastructureError"><![CDATA[nodes=${nodes} results=${results}]]></error>
  </testcase>
EOF
        results_files+=("$file")
    fi

    tests=$(count_xml_tag testcase "${results_files[@]}")
    failures=$(count_xml_tag failure "${results_files[@]}")
    errors=$(count_xml_tag error "${results_files[@]}")
    skipped=$(count_xml_tag skipped "${results_files[@]}")

    if [ "$tests" -eq 0 ]; then
        tests=${#expected_nodes[@]}
        if [ "$tests" -eq 0 ]; then
            tests=${#results_files[@]}
        fi
    fi

    mkdir -p "$STAGE_NAME"/framework/

    cat <<EOF > "$STAGE_NAME"/framework/framework_results.xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuite errors="$errors" failures="$failures" name="$name" skipped="$skipped"
           tests="$tests" time="0" timestamp="$(date +%FT%T)">
$(cat "${results_files[@]}")
</testsuite>
EOF

    if ! clush -o '-i ci_key' -l root -w "$nodes" --rcopy /var/tmp/artifacts \
                       --dest "$STAGE_NAME"/framework/; then
        artifacts_rc=$?
        echo "WARNING: Failed to copy /var/tmp/artifacts from nodes=$nodes rc=$artifacts_rc"
    fi

    # Send mail notification for infrastructure errors
    if [ "$rcopy_rc" -ne 0 ] || [ "$missing_results" -ne 0 ]; then
        local mail_msg="Infrastructure error in ${STAGE_NAME}\n"
        if [ "$rcopy_rc" -ne 0 ]; then
            mail_msg+="Failed to copy results from nodes. rc=$rcopy_rc\n"
        fi
        if [ "$missing_results" -ne 0 ]; then
            mail_msg+="Missing or incomplete JUnit results: $results\n"
            mail_msg+="Nodes: $nodes\n"
            mail_msg+="Results collected from $existing_node_results of ${#expected_nodes[@]} nodes\n"
        fi
        mail_msg+="See Jenkins console: ${BUILD_URL:-}\n"
        if declare -F send_mail > /dev/null; then
            send_mail "Infrastructure error in $STAGE_NAME" "$mail_msg" || true
        fi
    fi

    if [ "$rcopy_rc" -ne 0 ]; then
        return 1
    fi

    return 0
}

# create this dir so that the remote copy doesn't fail if nothing actually populates it
mkdir -p /var/tmp/artifacts

# functions should inherit the ERR trap
set -E

# set an error trap to create a junit result for any unhandled error
trap 'junit_on_error' ERR
