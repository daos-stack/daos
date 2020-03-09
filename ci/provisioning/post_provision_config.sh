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

DAOS_STACK_LOCAL_REPO_var="DAOS_STACK_${DISTRO}_LOCAL_REPO"
DAOS_STACK_GROUP_REPO_var="DAOS_STACK_${DISTRO}_GROUP_REPO"

clush -B -l root -w "$NODESTRING" -c ci_key* --dest=/tmp/
clush -B -S -l root -w "$NODESTRING" "MY_UID=$MY_UID
                                      CONFIG_POWER_ONLY=$CONFIG_POWER_ONLY
                                      INST_REPOS=\"$INST_REPOS\"
                                      INST_RPMS=\$(eval echo $INST_RPMS)
                                      GPG_KEY_URLS=\"$GPG_KEY_URLS\"
                                      REPOSITORY_URL=\"$REPOSITORY_URL\"
                                      JENKINS_URL=\"$JENKINS_URL\"
                                      DAOS_STACK_LOCAL_REPO=\"${!DAOS_STACK_LOCAL_REPO_var}\"
                                      DAOS_STACK_GROUP_REPO=\"${!DAOS_STACK_GROUP_REPO_var}\"
                                      DISTRO=\"$DISTRO\"
                                      $(cat ci/provisioning/post_provision_config_nodes_"${DISTRO}".sh)
                                      $(cat ci/provisioning/post_provision_config_nodes.sh)"