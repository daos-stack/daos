#!/bin/bash

# shellcheck source=ci/common.sh
. ci/common.sh

DSL_REPO_var="DAOS_STACK_$(toupper "${DISTRO:-}")_LOCAL_REPO"

ssh -i ci_key jenkins@"$NODE" \
    "INST_REPOS=\"${INST_REPOS:-}\"                                        \
     INST_RPMS=\"${INST_RPMS:-}\"                                          \
     NODESTRING=${NODESTRING:-}                                            \
     NODE_COUNT=${NODE_COUNT:-}                                            \
     REMOTE_ACCT=${REMOTE_ACCT:-vagrant}                                   \
     WORKDIR=$PWD                                                          \
     REPOSITORY_URL=\"$REPOSITORY_URL\"                                    \
     JENKINS_URL=\"$JENKINS_URL\"                                          \
     OPERATIONS_EMAIL=\"$OPERATIONS_EMAIL\"                                \
     DISTRO=${DISTRO:-}                                                    \
     DAOS_STACK_$(toupper "${DISTRO:-}")_LOCAL_REPO=\"${!DSL_REPO_var:-}\" \
     TEST_TAG=\"${TEST_TAG:-}\"                                            \
     WITH_VALGRIND=\"${WITH_VALGRIND:-}\"                                  \
     STAGE_NAME=\"${STAGE_NAME}\"                                          \
     ${PWD}/ci/vagrant/main_node.sh \"$1\"" || exit 1
