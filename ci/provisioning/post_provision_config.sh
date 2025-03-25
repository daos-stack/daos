#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
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
source ci/junit.sh


: "${MLNX_VER_NUM:=24.04-0.6.6.0}"

: "${DISTRO:=EL_7}"

retry_cmd 300 clush -B -S -l root -w "$NODESTRING" -c ci_key* --dest=/tmp/

function create_host_file() {
        local node_string="$1"
        local output_file="${2:-./hosts}"
        local input_file="${3:-}"
        rm -rf "$output_file" 2>/dev/null
        if [ -n "$input_file" ]; then
                cp "$input_file" "$output_file"
        fi
        IFS=',' read -ra NODES <<< "$node_string"
        for node in "${NODES[@]}"; do
                ip_address=$(nslookup "$node" 2>/dev/null | awk '/^Address: / {print $2}' | head -n 1)
                long_name=$(nslookup "$node" 2>/dev/null | awk '/^Name:/ {print $2}' | head -n 1)
                if [ -n "$ip_address" ] && [ -n "$long_name" ]; then
                        echo "$ip_address $long_name $node" >> "$output_file"
                else
                        echo "ERROR: Could not resolve $node"
                        return 1
                fi
        done
        return 0
}

if create_host_file "$NODESTRING" "./hosts" "/etc/hosts"; then
  retry_cmd 300 clush -B -S -l root -w "$NODESTRING" -c ./hosts --dest=/etc/hosts
else
  echo "ERROR: Failed to create host file"
fi


# shellcheck disable=SC2001
sanitized_commit_message="$(echo "$COMMIT_MESSAGE" | sed -e 's/\(["\$]\)/\\\1/g')"

if ! retry_cmd 2400 clush -B -S -l root -w "$NODESTRING" \
           "export PS4='$PS4'
           MY_UID=$(id -u)
           CONFIG_POWER_ONLY=${CONFIG_POWER_ONLY:-}
           INST_REPOS=\"${INST_REPOS:-}\"
           INST_RPMS=\"${INST_RPMS:-}\"
           GPG_KEY_URLS=\"${GPG_KEY_URLS:-}\"
           REPOSITORY_URL=\"${REPOSITORY_URL:-}\"
           JENKINS_URL=\"${JENKINS_URL:-}\"
           DISTRO=\"$DISTRO\"
           DAOS_STACK_RETRY_DELAY_SECONDS=\"$DAOS_STACK_RETRY_DELAY_SECONDS\"
           DAOS_STACK_RETRY_COUNT=\"$DAOS_STACK_RETRY_COUNT\"
           MLNX_VER_NUM=\"$MLNX_VER_NUM\"
           BUILD_URL=\"$BUILD_URL\"
           STAGE_NAME=\"$STAGE_NAME\"
           OPERATIONS_EMAIL=\"$OPERATIONS_EMAIL\"
           COMMIT_MESSAGE=\"$sanitized_commit_message\"
           REPO_FILE_URL=\"$REPO_FILE_URL\"
           ARTIFACTORY_URL=\"${ARTIFACTORY_URL:-}\"
           BRANCH_NAME=\"${BRANCH_NAME:-}\"
           CHANGE_TARGET=\"${CHANGE_TARGET:-}\"
           CI_RPM_TEST_VERSION=\"${CI_RPM_TEST_VERSION:-}\"
           DAOS_VERSION=\"${DAOS_VERSION:-}\"
           CI_PR_REPOS=\"${CI_PR_REPOS:-}\"
           REPO_PATH=\"${REPO_PATH:-}\"
           ARTIFACTS_URL=\"${ARTIFACTS_URL:-}\"
           COVFN_DISABLED=\"${COVFN_DISABLED:-true}\"
           DAOS_CI_INFO_DIR=\"${DAOS_CI_INFO_DIR:-wolf-2:/export/scratch}\"
           CI_SCONS_ARGS=\"${CI_SCONS_ARGS:-}\"
           $(cat ci/stacktrace.sh)
           $(cat ci/junit.sh)
           $(cat ci/provisioning/post_provision_config_common_functions.sh)
           $(cat ci/provisioning/post_provision_config_common.sh)
           $(cat ci/provisioning/post_provision_config_nodes_"$DISTRO".sh)
           $(cat ci/provisioning/post_provision_config_nodes.sh)"; then
    report_junit post_provision_config.sh results.xml "$NODESTRING"
    exit 1
fi

git log --format=%B -n 1 HEAD | sed -ne '1s/^\([A-Z][A-Z]*-[0-9][0-9]*\) .*/\1/p' \
                                     -e '/^Fixes:/{s/^Fixes: *//;s/ /\
/g;p}' | \
  retry_cmd 60 ssh -i ci_key -l jenkins "${NODELIST%%,*}" \
                                     "cat >/tmp/commit_fixes"
git log --pretty=format:%h --abbrev-commit --abbrev=7 |
  retry_cmd 60 ssh -i ci_key -l jenkins "${NODELIST%%,*}" "cat >/tmp/commit_list"
