#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

export PS4='+ ${HOSTNAME%%.*}:${BASH_SOURCE:+$BASH_SOURCE:}$LINENO:${FUNCNAME:+$FUNCNAME():} '

rm -f ci_key*
ssh-keygen -m PEM -N "" -f ci_key
cat << "EOF" > ci_key_ssh_config
host *
    CheckHostIp no
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    TCPKeepAlive yes
    LogLevel error
EOF

# shellcheck disable=SC1091
source ci/provisioning/post_provision_config_common_functions.sh
# shellcheck disable=SC1091
source ci/stacktrace.sh
# shellcheck disable=SC1091
source ci/junit.sh

# This script needs to be able to run outside of CI for testing.
# Before running the script, environment variables may be needed for
# the specific site.

: "${MLNX_VER_NUM:=3.2.1}"
: "${PYTHON_VERSION:=3.11}"

# This is tangled and needs a better fix as it has DISTRO being passed
# as EL_8 for EL_9, yet other places expect DISTRO to really be EL_8 and
# not EL_9.

# As caller has to be fixed later first set defaults for use outside of CI
: "${DISTRO:=unknown}"

# When running outside of CI, we can assume that this is run on the target
# system, and if DISTRO is unknown, we can look it up.
if [[ "$DISTRO" == unknown ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    : "${ID_LIKE:=rhel}"
    : "${ID:=unknown}"
    : "${VERSION_ID:=8}"
    prefix="EL"
    version="${VERSION_ID%%.*}"
    if [[ "$ID_LIKE" == *suse* ]]; then
        prefix="LEAP"
    elif [[ "$ID" == *ubuntu* ]]; then
        prefix="UBUNTU"
        version="$VERSION_ID"
    fi
    DISTRO="${prefix}_${version}"
fi

# Helper scripts should be distro family specific not distro version specific
FAMILY="${DISTRO%%_*}"

# NODELIST is all the nodes in a CI cluster comma separated - do not use here.
# NODESTRING is only the nodes in the requested CI cluster.
: "${NODESTRING:=localhost}"
ORDERED_NODES="$NODESTRING"

: "${COMMIT_MESSAGE:=$(git log -1 --pretty=%B)}"
: "${ARTIFACTORY_URL:=}"
: "${REPO_FILE_URL:=}"
if [ -n "$ARTIFACTORY_URL" ] && [ -z "$REPO_FILE_URL" ]; then
    REPO_FILE_URL="$ARTIFACTORY_URL/repo-files/"
fi
# This is an NFS share for looking up current test information.
: "${DAOS_CI_INFO_DIR:=}"
: "${JUNIT_NODE_RESULTS_ROOT:=${STAGE_NAME}/hardware_prep}"
: "${JUNIT_NODE_RESULTS_NODE_PREFIX:=post_provision_}"

# CI user can be any user that is not expected to be on the test systems.
: "${CI_USER:=jenkins}"

: "${DAOS_FAILURE_STEP:=startup}"

DAOS_FAILURE_STEP="copy_ci_keys"
retry_cmd 300 clush -B -S -l root -w "$NODESTRING" -c ci_key* --dest=/tmp/

DAOS_FAILURE_STEP="copy_query_node_interfaces"
retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                    -c ci/provisioning/query_node_interfaces.sh --dest=/var/tmp/
retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                    chmod +x /var/tmp/query_node_interfaces.sh

function resolve_host_ip() {
    local hostname="$1"

    if command -v getent >/dev/null 2>&1; then
        getent ahostsv4 "$hostname" | awk 'NF { print $1; exit }'
        return 0
    fi

    if command -v dig >/dev/null 2>&1; then
        dig +short A "$hostname" | awk 'NF { print; exit }'
        return 0
    fi

    return 1
}

function create_host_file() {
    local node_string="$1"
    local output_file="${2:-./hosts}"
    local NODES

    rm -rf "$output_file" 2>/dev/null

    # Parse node list as comma-separated hostnames in preserved cluster order.
    if ! IFS=',' read -ra NODES <<< "$node_string"; then
        echo "ERROR: Failed to parse node string: $node_string"
        return "$ERROR_FATAL"
    fi

    local node_index=0
    for node in "${NODES[@]}"; do
        node_index=$((node_index + 1))
        local node_num
        if ! node_num=$(printf "%03d" "$node_index"); then
            echo "ERROR: Failed to format node index '$node_index'"
            return "$ERROR_FATAL"
        fi

        local node_ip
        node_ip=$(resolve_host_ip "$node" 2>/dev/null || true)
        if [ -z "$node_ip" ]; then
            echo "ERROR: Could not resolve host '$node'"
            return "$ERROR_FATAL"
        fi

        local short_node
        short_node="${node%%.*}"
        if [ "$short_node" = "$node" ]; then
            echo "$node_ip $node test-$node_num" >> "$output_file"
        else
            echo "$node_ip $node $short_node test-$node_num" >> "$output_file"
        fi
    done

    # Resolve REPOSITORY_URL and ARTIFACTORY_URL
    local repo_host
    local artifactory_host

    if [ -n "${REPOSITORY_URL:-}" ]; then
        repo_host=$(echo "$REPOSITORY_URL" | sed -E 's|^[^:]+://([^/]+).*|\1|')
        local repo_ip
        repo_ip=$(resolve_host_ip "$repo_host" 2>/dev/null || true)
        if [ -n "$repo_ip" ]; then
            echo "$repo_ip $repo_host" >> "$output_file"
        fi
    fi

    if [ -n "${ARTIFACTORY_URL:-}" ]; then
        artifactory_host=$(echo "$ARTIFACTORY_URL" |
            sed -E 's|^[^:]+://([^/]+).*|\1|')
        local artifactory_ip
        artifactory_ip=$(
            resolve_host_ip "$artifactory_host" 2>/dev/null || true)
        if [ -n "$artifactory_ip" ]; then
            echo "$artifactory_ip $artifactory_host" >> "$output_file"
        fi
    fi

    return 0
}

if [ "$NODESTRING" != "localhost" ]; then
    DAOS_FAILURE_STEP="update_hosts_file"
    if ! create_host_file "$ORDERED_NODES" "./hosts"; then
        # Fatal error - exit immediately
        echo "ERROR: Failed to create host file"
        exit "$ERROR_FATAL"
    fi
    # Distribute generated hosts block and merge with stock /etc/hosts.
    retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                        -c ci/provisioning/update_daos_hosts_block.sh \
                        --dest=/var/tmp/
    retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                        chmod +x /var/tmp/update_daos_hosts_block.sh
    retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                        -c ./hosts --dest=/var/tmp/daos_hosts.generated
    update_hosts_cmd="/var/tmp/update_daos_hosts_block.sh"
    update_hosts_cmd+=" /var/tmp/daos_hosts.generated"
    retry_cmd 300 clush -B -S -l root -w "$NODESTRING" \
                        "$update_hosts_cmd"
fi


# shellcheck disable=SC2001
sanitized_commit_message="$(echo "$COMMIT_MESSAGE" | sed -e 's/\(["\$]\)/\\\1/g')"

build_remote_post_provision_payload() {
    local script_path
    local remote_script_files=(
        "ci/stacktrace.sh"
        "ci/junit.sh"
        "ci/provisioning/post_provision_config_common_functions.sh"
        "ci/provisioning/post_provision_config_common.sh"
        "ci/provisioning/post_provision_config_nodes_${FAMILY}.sh"
        "ci/provisioning/post_provision_config_nodes.sh"
    )

    REMOTE_POST_PROVISION_PAYLOAD="export PS4='$PS4'
           MY_UID=$(id -u)
           CI_USER=\"${CI_USER}\"
           CONFIG_POWER_ONLY=${CONFIG_POWER_ONLY:-}
           INST_REPOS=\"${INST_REPOS:-}\"
           INST_RPMS=\"${INST_RPMS:-}\"
           GPG_KEY_URLS=\"${GPG_KEY_URLS:-}\"
           REPOSITORY_URL=\"${REPOSITORY_URL:-}\"
           JENKINS_URL=\"${JENKINS_URL:-}\"
           DISTRO=\"$DISTRO\"
           DAOS_STACK_RETRY_DELAY_SECONDS=\"${DAOS_STACK_RETRY_DELAY_SECONDS:-}\"
           DAOS_STACK_RETRY_COUNT=\"${DAOS_STACK_RETRY_COUNT:-}\"
           MLNX_VER_NUM=\"$MLNX_VER_NUM\"
           BUILD_URL=\"${BUILD_URL:-}\"
           STAGE_NAME=\"${STAGE_NAME:-}\"
           OPERATIONS_EMAIL=\"${OPERATIONS_EMAIL:-}\"
           DAOS_SMTP_RELAY=\"${DAOS_SMTP_RELAY:-}\"
           NODELIST=\"${ORDERED_NODES:-}\"
           COMMIT_MESSAGE=\"$sanitized_commit_message\"
           REPO_FILE_URL=\"$REPO_FILE_URL\"
           ARTIFACTORY_URL=\"${ARTIFACTORY_URL}\"
           BRANCH_NAME=\"${BRANCH_NAME:-}\"
           CHANGE_TARGET=\"${CHANGE_TARGET:-}\"
           CI_RPM_TEST_VERSION=\"${CI_RPM_TEST_VERSION:-}\"
           DAOS_VERSION=\"${DAOS_VERSION:-}\"
           CI_PR_REPOS=\"${CI_PR_REPOS:-}\"
           REPO_PATH=\"${REPO_PATH:-}\"
           ARTIFACTS_URL=\"${ARTIFACTS_URL:-}\"
           COVFN_DISABLED=\"${COVFN_DISABLED:-true}\"
           DAOS_CI_INFO_DIR=\"${DAOS_CI_INFO_DIR}\"
           CI_SCONS_ARGS=\"${CI_SCONS_ARGS:-}\"
           PYTHON_VERSION=\"${PYTHON_VERSION}\""

    for script_path in "${remote_script_files[@]}"; do
        if [ ! -r "$script_path" ]; then
            echo "ERROR: Missing remote payload file: $script_path"
            return 2
        fi
        if ! bash -n "$script_path"; then
            echo "ERROR: Syntax check failed for remote payload file: $script_path"
            return 2
        fi
        REMOTE_POST_PROVISION_PAYLOAD+=$'\n'
        REMOTE_POST_PROVISION_PAYLOAD+="$(<"$script_path")"
    done

    return 0
}

DAOS_FAILURE_STEP="remote_payload_build"
if ! build_remote_post_provision_payload; then
    junit_result "remote_payload_build" "Failed to build remote post-provision payload"
    report_junit post_provision_config.sh \
                 post_provision_results.xml "$ORDERED_NODES" || true
    exit 1
fi

DAOS_FAILURE_STEP="remote_post_provision"
if ! retry_cmd 2400 clush -B -S -l root -w "$NODESTRING" \
           "$REMOTE_POST_PROVISION_PAYLOAD"; then
    report_junit post_provision_config.sh \
                 post_provision_results.xml "$ORDERED_NODES"
    exit 1
fi

DAOS_FAILURE_STEP="report_junit"
if ! report_junit post_provision_config.sh \
                 post_provision_results.xml "$ORDERED_NODES"; then
    echo "ERROR: Failed to collect node JUnit results"
    exit 1
fi

DAOS_FAILURE_STEP="publish_commit_metadata"
git log --format=%B -n 1 HEAD | sed -ne '1s/^\([A-Z][A-Z]*-[0-9][0-9]*\) .*/\1/p' \
                                     -e '/^Fixes:/{s/^Fixes: *//;s/ /\
/g;p}' | \
    retry_cmd 60 ssh -i ci_key -l "$CI_USER" "${ORDERED_NODES%%,*}" \
                                     "cat >/tmp/commit_fixes"
git log --pretty=format:%h --abbrev-commit --abbrev=7 |
    retry_cmd 60 ssh -i ci_key -l "$CI_USER" "${ORDERED_NODES%%,*}" \
        "cat >/tmp/commit_list"
