#!/bin/bash

set -eux

rm -f ci_key*
ssh-keygen -N "" -f ci_key
cat << "EOF" > ci_key_ssh_config
host wolf-*
    CheckHostIp no
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    LogLevel error
EOF

: "${DAOS_STACK_RETRY_DELAY_SECONDS:=60}"
: "${DAOS_STACK_RETRY_COUNT:=3}"
: "${BUILD_URL:=Not_in_jenkins}"
: "${OPERATIONS_EMAIL:=$USER@localhost}"

retry_cmd() {
    local command="$1"
    shift

    local tries=$DAOS_STACK_RETRY_COUNT
    while [ $tries -gt 0 ]; do
        if time $command "$@"; then
            # succeeded, return with success
            return 0
        fi
        # We hit an error
        (( tries-- ))
        {
          echo "Command $command $* failed on $HOSTNAME for $BUILD_URL"
          echo "Command status was ${PIPESTATUS[0]}"
          echo "Will retry $tries before giving up."
        } 2>&1 | mail -s "Command failed in $BUILD_URL" \
                      -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL"

        if [ $tries -gt 0 ]; then
            sleep "$DAOS_STACK_RETRY_DELAY_SECONDS"
        fi
    done
    return 1
}

: "${DISTRO:=EL_7}"
DSL_REPO_var="DAOS_STACK_${DISTRO}_LOCAL_REPO"
DSG_REPO_var="DAOS_STACK_${DISTRO}_GROUP_REPO"
DSA_REPO_var="DAOS_STACK_${DISTRO}_APPSTREAM_REPO"
: "${DAOS_STACK_EL_7_LOCAL_REPO:=}"
: "${DAOS_STACK_EL_7_GROUP_REPO:=}"
: "${DAOS_STACK_EL_7_APPSTREAM_REPO:=}"
: "${CONFIG_POWER_ONLY:=}"
: "${INST_RPMS:=}"
: "${INST_REPOS:=}"
: "${GPG_KEY_URLS:=}"
: "${REPOSITORY_URL:=}"
: "${JENKINS_URL:=}"

retry_cmd clush -B -S -l root -w "$NODESTRING" -c ci_key* --dest=/tmp/

retry_cmd clush -B -S -l root -w "$NODESTRING" \
           "MY_UID=$(id -u)
           CONFIG_POWER_ONLY=$CONFIG_POWER_ONLY
           INST_REPOS=\"$INST_REPOS\"
           INST_RPMS=\$(eval echo $INST_RPMS)
           GPG_KEY_URLS=\"$GPG_KEY_URLS\"
           REPOSITORY_URL=\"$REPOSITORY_URL\"
           JENKINS_URL=\"$JENKINS_URL\"
           DAOS_STACK_LOCAL_REPO=\"${!DSL_REPO_var}\"
           DAOS_STACK_GROUP_REPO=\"${!DSG_REPO_var:-}\"
           DAOS_STACK_EL_8_APPSTREAM_REPO=\"${!DSA_REPO_var:-}\"
           DISTRO=\"$DISTRO\"
           $(cat ci/provisioning/post_provision_config_nodes_"${DISTRO}".sh)
           $(cat ci/provisioning/post_provision_config_nodes.sh)"

git log --format=%s -n 1 HEAD | \
  retry_cmd ssh -i ci_key -l jenkins "${NODELIST%%,*}" \
                                     "cat >/tmp/commit_title"
git log --pretty=format:%h --abbrev-commit --abbrev=7 |
  retry_cmd ssh -i ci_key -l jenkins "${NODELIST%%,*}" "cat >/tmp/commit_list"
retry_cmd ssh root@"${NODELIST%%,*}" "mkdir -p /scratch && " \
                                     "mount wolf-2:/export/scratch /scratch"
