#!/usr/bin/groovy
/* Copyright (C) 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value="pipeline-lib@your_branch") _

def arch = ""
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def rpm_test_pre = '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                      nodelist=(${NODELIST//,/ })
                      scp -i ci_key src/tests/ftest/data/daos_server_baseline.yaml jenkins@${nodelist[0]}:/tmp
                      ssh -i ci_key jenkins@${nodelist[0]} "set -ex
                      repo_file_base=\"*_job_\${JOB_NAME%%/*}_job_\"
                      for repo in openpa libfabric pmix   \
                                  ompi mercury spdk isa-l \
                                  fio dpdk protobuf-c     \
                                  fuse pmdk argobots raft \
                                  cart@daos_devel; do     \
                          if [[ \\\$repo = *@* ]]; then
                              branch=\"\\\${repo#*@}\"
                              repo=\"\\\${repo%@*}\"
                          else
                              branch=\"master\"
                          fi\n'''

def rpm_test_daos_test = '''me=\\\$(whoami)
                            for dir in server agent; do
                                sudo mkdir /var/run/daos_\\\$dir
                                sudo chmod 0755 /var/run/daos_\\\$dir
                                sudo chown \\\$me:\\\$me /var/run/daos_\\\$dir
                            done
                            sudo mkdir -p /mnt/daos
                            sudo mount -t tmpfs -o size=16777216k tmpfs /mnt/daos
                            sudo cp /tmp/daos_server_baseline.yaml /usr/etc/daos_server.yml
                            cat /usr/etc/daos_server.yml
                            coproc orterun -np 1 -H \\\$HOSTNAME --enable-recovery -x DAOS_SINGLETON_CLI=1  daos_server -c 1 -a /tmp -o /usr/etc/daos_server.yml
                            trap 'set -x; kill -INT \\\$COPROC_PID' EXIT
                            line=\"\"
                            while [[ \"\\\$line\" != *started\\\\ on\\\\ rank\\\\ 0* ]]; do
                                read line <&\\\${COPROC[0]}
                                echo \"Server stdout: \\\$line\"
                            done
                            echo \"Server started!\"
                            daos_agent &
                            AGENT_PID=\\\$!
                            trap 'set -x; kill -INT \\\$AGENT_PID \\\$COPROC_PID' EXIT
                            orterun -np 1 -x OFI_INTERFACE=eth0 -x CRT_ATTACH_INFO_PATH=/tmp -x DAOS_SINGLETON_CLI=1 daos_test -m'''

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *' : '')
    }

    environment {
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        BAHTTPS_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTP_PROXY="' + env.HTTP_PROXY + '" --build-arg http_proxy="' + env.HTTP_PROXY + '"' : ''}"
        BAHTTP_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTPS_PROXY="' + env.HTTPS_PROXY + '" --build-arg https_proxy="' + env.HTTPS_PROXY + '"' : ''}"
        UID=sh(script: "id -u", returnStdout: true)
        BUILDARGS = "--build-arg NOBUILD=1 --build-arg UID=$env.UID $env.BAHTTP_PROXY $env.BAHTTPS_PROXY"
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
    }

    stages {
        stage('Cancel Previous Builds') {
            when { changeRequest() }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Pre-build') {
            parallel {
                stage('checkpatch') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW,
                                   ignored_files: "src/control/vendor/*:src/mgmt/*.pb-c.[ch]:src/iosrv/*.pb-c.[ch]:src/security/*.pb-c.[ch]"
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'pylint.log', allowEmptyArchive: true
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
            }
        }
        stage('Build') {
            /* Don't use failFast here as whilst it avoids using extra resources
             * and gives faster results for PRs it's also on for master where we
	     * do want complete results in the case of partial failure
	     */
            //failFast true
            parallel {
                stage('Build RPM on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile-mockbuild.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) --build-arg JENKINS_URL=' +
                                                env.JENKINS_URL
                            args  '--group-add mock --cap-add=SYS_ADMIN --privileged=true'
                        }
                    }
                    steps {
                         githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/centos7/
                                      mkdir -p artifacts/centos7/
                              if make -C utils/rpms srpm; then
                                  if make -C utils/rpms mockbuild; then
                                      (cd /var/lib/mock/epel-7-x86_64/result/ &&
                                       cp -r . $OLDPWD/artifacts/centos7/)
                                      createrepo artifacts/centos7/
                                  else
                                      rc=\${PIPESTATUS[0]}
                                      (cd /var/lib/mock/epel-7-x86_64/result/ &&
                                       cp -r . $OLDPWD/artifacts/centos7/)
                                      cp -af utils/rpms/_topdir/SRPMS artifacts/centos7/
                                      exit \$rc
                                  fi
                              else
                                  exit \${PIPESTATUS[0]}
                              fi'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'artifacts/centos7/**'
                        }
                        success {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unstable {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE"
                        }
                        failure {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "FAILURE"
                        }
                    }
                }
                stage('Build RPM on SLES 12.3') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile-rpmbuild.sles.12.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) --build-arg JENKINS_URL=' +
                                                env.JENKINS_URL +
                                                 " --build-arg CACHEBUST=${currentBuild.startTimeInMillis}"
                        }
                    }
                    steps {
                         githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/sles12.3/
                              mkdir -p artifacts/sles12.3/
                              rm -rf utils/rpms/_topdir/SRPMS
                              if make -C utils/rpms srpm; then
                                  rm -rf utils/rpms/_topdir/RPMS
                                  if make -C utils/rpms rpms; then
                                      ln utils/rpms/_topdir/{RPMS/*,SRPMS}/*  artifacts/sles12.3/
                                      createrepo artifacts/sles12.3/
                                  else
                                      exit \${PIPESTATUS[0]}
                                  fi
                              else
                                  exit \${PIPESTATUS[0]}
                              fi'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'artifacts/sles12.3/**'
                        }
                        success {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unstable {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE"
                        }
                        failure {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "FAILURE"
                        }
                    }
                }
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-centos7-gcc'
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: ".build_vars${arch}.*"
                        stash name: 'CentOS-tests',
                                    includes: '''build/src/rdb/raft/src/tests_main,
                                                 build/src/common/tests/btree_direct,
                                                 build/src/common/tests/btree,
                                                 build/src/common/tests/sched,
                                                 build/src/common/tests/drpc_tests,
                                                 build/src/common/tests/acl_api_tests,
                                                 build/src/common/tests/acl_util_tests,
                                                 build/src/common/tests/acl_util_real,
                                                 build/src/iosrv/tests/drpc_progress_tests,
                                                 build/src/control/src/github.com/daos-stack/daos/src/control/mgmt,
                                                 build/src/client/api/tests/eq_tests,
                                                 build/src/iosrv/tests/drpc_handler_tests,
                                                 build/src/iosrv/tests/drpc_listener_tests,
                                                 build/src/security/tests/cli_security_tests,
                                                 build/src/security/tests/srv_acl_tests,
                                                 build/src/vos/vea/tests/vea_ut,
                                                 build/src/common/tests/umem_test,
                                                 scons_local/build_info/**,
                                                 src/common/tests/btree.sh,
                                                 src/control/run_go_tests.sh,
                                                 src/rdb/raft_tests/raft_tests.py,
                                                 src/vos/tests/evt_ctl.sh'''
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            sh '''rm -rf daos-devel/
                                  mkdir daos-devel/
                                  mv install/{lib,include} daos-devel/'''
                            archiveArtifacts artifacts: 'daos-devel/**'
                            sh "rm -rf _build.external${arch}"
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        unstable {
                            sh 'mv config.log config.log-centos7-gcc'
                            archiveArtifacts artifacts: 'config.log-centos7-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-centos7-gcc'
                            archiveArtifacts artifacts: 'config.log-centos7-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when { beforeAgent true
                           branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-centos7-clang'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-centos7-clang'
                            archiveArtifacts artifacts: 'config.log-centos7-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-centos7-clang'
                            archiveArtifacts artifacts: 'config.log-centos7-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    when { beforeAgent true
                           branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu18.04 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-ubuntu18.04-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-ubuntu18.04-gcc'
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-ubuntu18.04-gcc'
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04 with Clang') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu18.04 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-ubuntu18.04-clag'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-ubuntu18.04-clang'
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-ubuntu18.04-clang'
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on SLES 12.3') {
                    when { beforeAgent true
                           environment name: 'SLES12_3_DOCKER', value: 'true' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.sles.12.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-sles12.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-sles12.3-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-sles12.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                                */
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-sles12.3-gcc'
                            archiveArtifacts artifacts: 'config.log-sles12.3-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-sles12.3-gcc'
                            archiveArtifacts artifacts: 'config.log-sles12.3-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 42.3') {
                    when { beforeAgent true
                           environment name: 'LEAP42_3_DOCKER', value: 'true' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.42.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap42.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-leap42.3-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap42.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                                */
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-leap42.3-gcc'
                            archiveArtifacts artifacts: 'config.log-leap42.3-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-leap42.3-gcc'
                            archiveArtifacts artifacts: 'config.log-leap42.3-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15') {
                    when { beforeAgent true
                           branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-leap15-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-leap15-gcc'
                            archiveArtifacts artifacts: 'config.log-leap15-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-leap15-gcc'
                            archiveArtifacts artifacts: 'config.log-leap15-gcc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when { beforeAgent true
                           branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-leap15-clang'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-leap15-clang'
                            archiveArtifacts artifacts: 'config.log-leap15-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-leap15-clang'
                            archiveArtifacts artifacts: 'config.log-leap15-clang'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                            args '-v /opt/intel:/opt/intel'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "icc",
                                   failure_artifacts: 'config.log-leap15-icc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-intelc",
                                             tools: [ intel(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh 'mv config.log config.log-leap15-intelc'
                            archiveArtifacts artifacts: 'config.log-leap15-intelc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh 'mv config.log config.log-leap15-intelc'
                            archiveArtifacts artifacts: 'config.log-leap15-intelc'
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
            }
        }
        stage('Unit Test') {
            parallel {
                stage('run_test.sh') {
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       snapshot: true
                        runTest stashes: [ 'CentOS-tests', 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export SSH_KEY_ARGS="-i ci_key"
                                           export PDSH_SSH_ARGS_APPEND="$SSH_KEY_ARGS"
                                           # JENKINS-52781 tar function is breaking symlinks
					   rm -rf test_results
					   mkdir test_results
                                           rm -f build/src/control/src/github.com/daos-stack/daos/src/control
                                           mkdir -p build/src/control/src/github.com/daos-stack/daos/src/
                                           ln -s ../../../../../../../../src/control build/src/control/src/github.com/daos-stack/daos/src/control
                                           . ./.build_vars.sh
                                           DAOS_BASE=${SL_PREFIX%/install*}
                                           NODE=${NODELIST%%,*}
                                           ssh $SSH_KEY_ARGS jenkins@$NODE "set -x
                                               set -e
                                               sudo bash -c 'echo \"1\" > /proc/sys/kernel/sysrq'
                                               if grep /mnt/daos\\  /proc/mounts; then
                                                   sudo umount /mnt/daos
                                               else
                                                   if [ ! -d /mnt/daos ]; then
                                                       sudo mkdir -p /mnt/daos
                                                   fi
                                               fi
                                               trap 'set +e; set -x; sudo umount /mnt/daos' EXIT
                                               sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
                                               sudo mkdir -p $DAOS_BASE
                                               trap 'set +e; set -x
                                                     cd
                                                     sudo umount /mnt/daos
                                                     sudo umount \"$DAOS_BASE\" || {
                                                         echo "Failed to unmount $DAOS_BASE"
                                                         ps axf
                                                     }' EXIT
                                               sudo mount -t nfs $HOSTNAME:$PWD $DAOS_BASE
					       # set CMOCKA envs here
					       export CMOCKA_MESSAGE_OUTPUT="xml"
					       export CMOCKA_XML_FILE="$DAOS_BASE/test_results/%g.xml"
                                               cd $DAOS_BASE
                                               OLD_CI=false utils/run_test.sh
                                               rm -rf run_test.sh/
                                               mkdir run_test.sh/
                                               [ -f /tmp/daos.log ] && mv /tmp/daos.log run_test.sh/
                                               # servers can sometimes take a while to stop when the test is done
                                               x=0
                                               while [ \"\\\$x\" -lt \"10\" ] &&
                                                     pgrep '(orterun|daos_server|daos_io_server)'; do
                                                   sleep 1
                                                   let x=\\\$x+1
                                               done"''',
                              junit_files: 'test_results/*.xml'
                    }
                    post {
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                        always {
				sh 'python utils/fix_cmocka_xml.py'
				junit 'test_results/*.xml'
				archiveArtifacts artifacts: 'run_test.sh/**'
                        }
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Functional') {
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       snapshot: true
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export SSH_KEY_ARGS="-i ci_key"
                                           export PDSH_SSH_ARGS_APPEND="$SSH_KEY_ARGS"
                                           test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=regression,vm
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-9)
                                           ./ftest.sh "$test_tag" $tnodes''',
                                junit_files: "src/tests/ftest/avocado/*/*/*.xml src/tests/ftest/*_results.xml",
                                failure_artifacts: env.STAGE_NAME
                    }
                    post {
                        always {
                            sh '''rm -rf src/tests/ftest/avocado/*/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      ls *daos{,_agent}.log* >/dev/null && mv *daos{,_agent}.log* "$STAGE_NAME/"
                                      mv src/tests/ftest/avocado/* \
                                         $(ls src/tests/ftest/*.stacktrace || true) "$STAGE_NAME/"
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/*/results.xml, src/tests/ftest/*_results.xml'
                            archiveArtifacts artifacts: env.STAGE_NAME + '/**'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Functional_Hardware') {
                    agent {
                        label 'ci_nvme9'
                    }
                    steps {
                        // First snapshot provision the VM at beginning of list
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       snapshot: true
                        // Then just reboot the physical nodes
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       power_only: true
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export SSH_KEY_ARGS="-i ci_key"
                                           export PDSH_SSH_ARGS_APPEND="$SSH_KEY_ARGS"
                                           test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag-hw:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,hw
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-9)
                                           ./ftest.sh "$test_tag" $tnodes''',
                                junit_files: "src/tests/ftest/avocado/*/*/*.xml src/tests/ftest/*_results.xml",
                                failure_artifacts: env.STAGE_NAME
                    }
                    post {
                        always {
                            sh '''rm -rf src/tests/ftest/avocado/*/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      ls *daos{,_agent}.log* >/dev/null && mv *daos{,_agent}.log* "$STAGE_NAME/"
                                      mv src/tests/ftest/avocado/* \
                                         $(ls src/tests/ftest/*.stacktrace || true) "$STAGE_NAME/"
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/*/results.xml, src/tests/ftest/*_results.xml'
                            archiveArtifacts artifacts: env.STAGE_NAME + '/**'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Test CentOS 7 RPMs') {
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       distro: 'el7.6',
                                       node_count: 1,
                                       snapshot: true
                        runTest script: "${rpm_test_pre}" +
                                    '''     sudo yum-config-manager --add-repo=\${JENKINS_URL}job/\${JOB_NAME%%/*}/job/\\\${repo}/job/\\\${branch}/lastSuccessfulBuild/artifact/artifacts/centos7/
                                            sudo bash -c \\\"echo \\\\\\"gpgcheck = False\\\\\\" >> /etc/yum.repos.d/\\\${repo_file_base}\\\${repo}_job_\\\${branch}_lastSuccessfulBuild_artifact_artifacts_centos7_.repo\\\"

                                        done
                                        sudo yum-config-manager --add-repo=\${BUILD_URL}artifact/artifacts/centos7/
                                        sudo bash -c \\\"echo \\\\\\"gpgcheck = False\\\\\\" >> /etc/yum.repos.d/\\\${repo_file_base}daos_job_\${JOB_BASE_NAME}_\${BUILD_ID}_artifact_artifacts_centos7_.repo\\\"
                                        # work around openmpi -> ompi
                                        sudo yum -y erase metabench mdtest simul IOR
                                        sudo yum -y install daos-client
                                        sudo yum -y history rollback last-1
                                        sudo yum -y install daos-server
                                        sudo yum -y install daos-tests\n''' +
                                        "${rpm_test_daos_test}" + '"',
                                junit_files: null,
                                failure_artifacts: env.STAGE_NAME
                    }
                }
                stage('Test SLES12.3 RPMs') {
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       distro: 'sles12sp3',
                                       node_count: 1,
                                       snapshot: true
                        runTest script: "${rpm_test_pre}" +
                                     '''    sudo zypper --non-interactive ar --gpgcheck-allow-unsigned -f \${JENKINS_URL}job/\${JOB_NAME%%/*}/job/\\\${repo}/job/\\\${branch}/lastSuccessfulBuild/artifact/artifacts/sles12.3/ \\\$repo
                                        done
                                        sudo zypper --non-interactive ar --gpgcheck-allow-unsigned -f \${JENKINS_URL}job/\${JOB_NAME%%/*}/job/python-pathlib/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ python-pathlib
                                        sudo zypper --non-interactive ar --gpgcheck-allow-unsigned -f \${BUILD_URL}artifact/artifacts/sles12.3/ daos
                                        sudo zypper --non-interactive ar -f https://download.opensuse.org/repositories/science:/HPC:/SLE12SP3_Missing/SLE_12_SP3/ hwloc
                                        sudo zypper --non-interactive ar https://download.opensuse.org/repositories/home:/jhli/SLE_15/home:jhli.repo
                                        sudo zypper --non-interactive ar https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/devel:libraries:c_c++.repo
                                        sudo zypper --non-interactive --gpg-auto-import-keys ref
                                        sudo zypper --non-interactive rm openmpi libfabric1
                                        sudo zypper --non-interactive in daos-client
                                        sudo zypper --non-interactive in daos-server
                                        sudo zypper --non-interactive in daos-tests\n''' +
                                        "${rpm_test_daos_test}" + '"',
                                junit_files: null,
                                failure_artifacts: env.STAGE_NAME
                    }
                }
            }
        }
    }
}
