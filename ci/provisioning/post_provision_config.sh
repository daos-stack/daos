#!/bin/bash

# shellcheck source=ci/common.sh
. ci/common.sh

export PS4='+ ${HOSTNAME%%.*}:${BASH_SOURCE:+$BASH_SOURCE:}$LINENO:${FUNCNAME:+$FUNCNAME():} '

rm -f ci_key*
ssh-keygen -N "" -f ci_key
cat << "EOF" > ci_key_ssh_config
host *
    CheckHostIp no
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    LogLevel error
EOF

# shellcheck disable=SC1091
source ci/provisioning/post_provision_config_common_functions.sh
# shellcheck disable=SC1091
source ci/junit.sh

: "${DISTRO:=EL_7}"
DSL_REPO_var="DAOS_STACK_$(toupper "$DISTRO")_LOCAL_REPO"
DSG_REPO_var="DAOS_STACK_$(toupper "$DISTRO")_GROUP_REPO"
DSA_REPO_var="DAOS_STACK_$(toupper "$DISTRO")_APPSTREAM_REPO"
: "${DAOS_STACK_EL_7_LOCAL_REPO:=}"
: "${DAOS_STACK_EL_7_GROUP_REPO:=}"
: "${DAOS_STACK_EL_7_APPSTREAM_REPO:=}"
: "${CONFIG_POWER_ONLY:=}"
: "${INST_RPMS:=}"
: "${INST_REPOS:=}"
: "${GPG_KEY_URLS:=}"
: "${REPOSITORY_URL:=}"
: "${JENKINS_URL:=}"

retry_cmd 300 clush -B -S -l root -w "$NODESTRING" -c ci_key* --dest=/tmp/

if ! retry_cmd 2400 clush -B -S -l root -w "$NODESTRING" \
           "export PS4=$PS4
           MY_UID=$(id -u)
           CONFIG_POWER_ONLY=$CONFIG_POWER_ONLY
           INST_REPOS=\"$INST_REPOS\"
           INST_RPMS=\$(eval echo $INST_RPMS)
           GPG_KEY_URLS=\"$GPG_KEY_URLS\"
           REPOSITORY_URL=\"$REPOSITORY_URL\"
           JENKINS_URL=\"$JENKINS_URL\"
           DAOS_STACK_LOCAL_REPO=\"${!DSL_REPO_var:-}\"
           DAOS_STACK_GROUP_REPO=\"${!DSG_REPO_var:-}\"
           DAOS_STACK_EL_8_APPSTREAM_REPO=\"${!DSA_REPO_var:-}\"
           DISTRO=\"$(toupper "$DISTRO")\"
           FOR_DAOS=${FOR_DAOS:-true}
           REMOTE_ACCT=\"${REMOTE_ACCT:-jenkins}\"
           DAOS_STACK_RETRY_DELAY_SECONDS=\"${DAOS_STACK_RETRY_DELAY_SECONDS}\"
           DAOS_STACK_RETRY_COUNT=\"${DAOS_STACK_RETRY_COUNT}\"
           BUILD_URL=\"${BUILD_URL}\"
           STAGE_NAME=\"${STAGE_NAME}\"
           OPERATIONS_EMAIL=\"${OPERATIONS_EMAIL}\"
           COMMIT_MESSAGE=\"${COMMIT_MESSAGE-}\"
           REPO_FILE_URL=\"$REPO_FILE_URL\"
           $(cat ci/stacktrace.sh)
           $(cat ci/junit.sh)
           $(cat ci/provisioning/post_provision_config_common_functions.sh)
           $(cat ci/provisioning/post_provision_config_common.sh)
           $(cat ci/provisioning/post_provision_config_nodes_"${DISTRO}".sh)
           $(cat ci/provisioning/post_provision_config_nodes.sh)"; then
    report_junit post_provision_config.sh results.xml "$NODESTRING"
    exit 1
fi

if [ -d .git ]; then
    git log --format=%s -n 1 HEAD | \
      retry_cmd 60 ssh -i ci_key -l "${REMOTE_ACCT:-jenkins}" "${NODESTRING%%,*}" \
                       "cat >/tmp/commit_title"
    git log --pretty=format:%h --abbrev-commit --abbrev=7 |
      retry_cmd 60 ssh -i ci_key -l "${REMOTE_ACCT:-jenkins}" "${NODESTRING%%,*}" \
                       "cat >/tmp/commit_list"
    if [ "${SKIPLIST_MOUNT:-x}" != "x" ]; then
        retry_cmd 600 ssh root@"${NODESTRING%%,*}" "mkdir -p /scratch && " \
                                                   "mount ${SKIPLIST_MOUNT} /scratch"
    fi
fi
