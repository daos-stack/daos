#!/usr/bin/groovy
/* Copyright (C) 2019-2020 Intel Corporation
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

def daos_branch = "master"
def arch = ""
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def daos_packages_version = ""
def el7_component_repos = ""
def component_repos = ""
def daos_repo = "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
def el7_daos_repos = el7_component_repos + ' ' + component_repos + ' ' + daos_repo
def functional_rpms  = "--exclude openmpi openmpi3 hwloc ndctl spdk-tools " +
                       "ior-hpc-cart-4-daos-0 mpich-autoload-cart-4-daos-0 " +
                       "romio-tests-cart-4-daos-0 hdf5-tests-cart-4-daos-0 " +
                       "mpi4py-tests-cart-4-daos-0 testmpio-cart-4-daos-0"

def rpm_test_pre = '''if git show -s --format=%B | grep "^Skip-test: true"; then
                          exit 0
                      fi
                      nodelist=(${NODELIST//,/ })
                      scp -i ci_key src/tests/ftest/data/daos_server_baseline.yaml \
                                    jenkins@${nodelist[0]}:/tmp
                      scp -i ci_key src/tests/ftest/data/daos_agent_baseline.yaml \
                                    jenkins@${nodelist[0]}:/tmp
                      ssh -i ci_key jenkins@${nodelist[0]} "set -ex\n'''

def rpm_test_daos_test = '''me=\\\$(whoami)
                            for dir in server agent; do
                                sudo mkdir /var/run/daos_\\\$dir
                                sudo chmod 0755 /var/run/daos_\\\$dir
                                sudo chown \\\$me:\\\$me /var/run/daos_\\\$dir
                            done
                            sudo mkdir /tmp/daos_sockets
                            sudo chmod 0755 /tmp/daos_sockets
                            sudo chown \\\$me:\\\$me /tmp/daos_sockets
                            sudo mkdir -p /mnt/daos
                            sudo mount -t tmpfs -o size=16777216k tmpfs /mnt/daos
                            sed -i -e \\\"/^access_points:/s/example/\\\$(hostname -s)/\\\" /tmp/daos_server_baseline.yaml
                            sed -i -e \\\"/^access_points:/s/example/\\\$(hostname -s)/\\\" /tmp/daos_agent_baseline.yaml
                            sudo cp /tmp/daos_server_baseline.yaml /etc/daos/daos_server.yml
                            sudo cp /tmp/daos_agent_baseline.yaml /etc/daos/daos_agent.yml
                            cat /etc/daos/daos_server.yml
                            cat /etc/daos/daos_agent.yml
                            module load mpi/openmpi3-x86_64
                            coproc orterun -np 1 -H \\\$HOSTNAME --enable-recovery daos_server --debug start -t 1 --recreate-superblocks
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
                            orterun -np 1 -x OFI_INTERFACE=eth0 daos_test -m'''

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (env.BRANCH_NAME != "weekly-testing" &&
     !env.BRANCH_NAME.startsWith("release/") &&
     env.BRANCH_NAME != "master")) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *\n' : '' +
             env.BRANCH_NAME == 'weekly-testing' ? 'H 0 * * 6' : '')
    }

    environment {
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        BAHTTPS_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTP_PROXY="' + env.HTTP_PROXY + '" --build-arg http_proxy="' + env.HTTP_PROXY + '"' : ''}"
        BAHTTP_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTPS_PROXY="' + env.HTTPS_PROXY + '" --build-arg https_proxy="' + env.HTTPS_PROXY + '"' : ''}"
        UID = sh(script: "id -u", returnStdout: true)
        BUILDARGS = "$env.BAHTTP_PROXY $env.BAHTTPS_PROXY "                   +
                    "--build-arg NOBUILD=1 --build-arg UID=$env.UID "         +
                    "--build-arg JENKINS_URL=$env.JENKINS_URL "               +
                    "--build-arg CACHEBUST=${currentBuild.startTimeInMillis}"
       QUICKBUILD = commitPragma(pragma: 'Quick-build').contains('true')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        CART_COMMIT = sh(script: "sed -ne 's/CART *= *\\(.*\\)/\\1/p' utils/build.config",
                         returnStdout: true).trim()
        QUICKBUILD_DEPS = sh(script: "rpmspec -q --define cart_sha1\\ ${env.CART_COMMIT} --srpm --requires utils/rpms/daos.spec 2>/dev/null",
                             returnStdout: true)
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
            when {
                beforeAgent true
                allOf {
                    not { branch 'weekly-testing' }
                    expression { env.CHANGE_TARGET != 'weekly-testing' }
                }
            }
            parallel {
                stage('checkpatch') {
                    when {
                      beforeAgent true
                      expression {
                        ! commitPragma(pragma: 'Skip-checkpatch').contains('true')
                      }
                    }
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
                                   ignored_files: "src/control/vendor/*:src/include/daos/*.pb-c.h:src/common/*.pb-c.[ch]:src/mgmt/*.pb-c.[ch]:src/iosrv/*.pb-c.[ch]:src/security/*.pb-c.[ch]:*.crt:*.pem:*_test.go"
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
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression { ! commitPragma(pragma: 'Skip-build').contains('true') }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
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
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            sh label: env.STAGE_NAME,
                               script: '''rm -rf artifacts/centos7/
                                          mkdir -p artifacts/centos7/
                                          make CHROOT_NAME="epel-7-x86_64" -C utils/rpms chrootbuild'''
                        }
                    }
                    post {
                        success {
                            sh label: "Build Log",
                               script: '''mockroot=/var/lib/mock/epel-7-x86_64
                                          (cd $mockroot/result/ &&
                                           cp -r . $OLDPWD/artifacts/centos7/)
                                          createrepo artifacts/centos7/
                                          rpm --qf %{version}-%{release}.%{arch} -qp artifacts/centos7/daos-server-*.x86_64.rpm > centos7-rpm-version
                                          cat $mockroot/result/{root,build}.log'''
                            stash name: 'CentOS-rpm-version', includes: 'centos7-rpm-version'
                            publishToRepository product: 'daos',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'el-7',
                                                publish_branch: daos_branch,
                                                repo_dir: 'artifacts/centos7/'
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unstable {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE", ignore_failure: true
                        }
                        failure {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "FAILURE", ignore_failure: true
                        }
                        unsuccessful {
                            sh label: "Build Log",
                               script: '''mockroot=/var/lib/mock/epel-7-x86_64
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true
                                          artdir=$PWD/artifacts/centos7
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/centos7/**'
                        }
                    }
                }
                stage('Build RPM on Leap 15') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            args '--privileged=true'
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
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            sh label: env.STAGE_NAME,
                               script: '''rm -rf artifacts/leap15/
                                  mkdir -p artifacts/leap15/
                                  if git show -s --format=%B | grep "^Skip-build: true"; then
                                      exit 0
                                  fi
                                  make CHROOT_NAME="opensuse-leap-15.1-x86_64" -C utils/rpms chrootbuild'''
                        }
                    }
                    post {
                        success {
                            sh label: "Build Log",
                               script: '''mockroot=/var/lib/mock/opensuse-leap-15.1-x86_64
                                          (cd $mockroot/result/ &&
                                           cp -r . $OLDPWD/artifacts/leap15/)
                                          createrepo artifacts/leap15/
                                          rpm --qf %{version}-%{release}.%{arch} -qp artifacts/centos7/daos-server-*.x86_64.rpm > leap15-rpm-version
                                          cat $mockroot/result/{root,build}.log'''
                            stash name: 'Leap-rpm-version', includes: 'leap15-rpm-version'
                            publishToRepository product: 'daos',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'leap-15',
                                                publish_branch: daos_branch,
                                                repo_dir: 'artifacts/leap15/'
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
                        unsuccessful {
                            sh label: "Build Log",
                               script: '''mockroot=/var/lib/mock/opensuse-leap-15.1-x86_64
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true
                                          artdir=$PWD/artifacts/leap15
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/leap15/**'
                        }
                    }
                }
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                                '$BUILDARGS ' +
                                                '--build-arg QUICKBUILD=' + env.QUICKBUILD +
                                                ' --build-arg QUICKBUILD_DEPS="' + env.QUICKBUILD_DEPS +
                                                '" --build-arg CART_COMMIT=-' + env.CART_COMMIT +
                                                ' --build-arg REPOS="' + component_repos + '"'
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
                                                 build/src/common/tests/acl_valid_tests,
                                                 build/src/common/tests/acl_util_tests,
                                                 build/src/common/tests/acl_principal_tests,
                                                 build/src/common/tests/acl_real_tests,
                                                 build/src/common/tests/prop_tests,
                                                 build/src/iosrv/tests/drpc_progress_tests,
                                                 build/src/control/src/github.com/daos-stack/daos/src/control/mgmt,
                                                 build/src/client/api/tests/eq_tests,
                                                 build/src/iosrv/tests/drpc_handler_tests,
                                                 build/src/iosrv/tests/drpc_listener_tests,
                                                 build/src/mgmt/tests/srv_drpc_tests,
                                                 build/src/security/tests/cli_security_tests,
                                                 build/src/security/tests/srv_acl_tests,
                                                 build/src/vos/vea/tests/vea_ut,
                                                 build/src/common/tests/umem_test,
                                                 build/src/bio/smd/tests/smd_ut,
                                                 scons_local/build_info/**,
                                                 src/common/tests/btree.sh,
                                                 src/control/run_go_tests.sh,
                                                 src/rdb/raft_tests/raft_tests.py,
                                                 src/vos/tests/evt_ctl.sh
                                                 src/control/lib/netdetect/netdetect.go'''
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
                            sh "rm -rf _build.external${arch}"
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-ubuntu18.04-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-ubuntu18.04-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on Leap 15') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C and TARGET_PREFIX') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression { env.QUICKBUILD != 'true' }
                        }
                    }
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
                                   TARGET_PREFIX: 'install/opt', failure_artifacts: 'config.log-leap15-icc'
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-intelc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                    }
                }
            }
        }
        stage('Unit Test') {
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression { env.NO_CI_TESTING != 'true' }
                expression { ! commitPragma(pragma: 'Skip-test').contains('true') }
            }
            parallel {
                stage('run_test.sh') {
                    when {
                      beforeAgent true
                      expression {
                        ! commitPragma(pragma: 'Skip-run_test').contains('true')
                      }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_component_repos + ' ' + component_repos,
                                       inst_rpms: 'gotestsum openmpi3 hwloc-devel argobots ' +
                                                  "cart-devel-${env.CART_COMMIT} fuse3-libs " +
                                                  'libisa-l-devel libpmem libpmemobj protobuf-c ' +
                                                  'spdk-devel libfabric-devel pmix numactl-devel ' +
                                                  'libipmctl-devel'
                        runTest stashes: [ 'CentOS-tests', 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''# JENKINS-52781 tar function is breaking symlinks
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
                                                   sudo mkdir -p /mnt/daos
                                               fi
                                               sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
                                               sudo mkdir -p $DAOS_BASE
                                               sudo mount -t nfs $HOSTNAME:$PWD $DAOS_BASE

                                               # copy daos_admin binary into \$PATH and fix perms
                                               sudo cp $DAOS_BASE/install/bin/daos_admin /usr/bin/daos_admin && \
                                                   sudo chown root /usr/bin/daos_admin && \
                                                   sudo chmod 4755 /usr/bin/daos_admin && \
                                                   mv $DAOS_BASE/install/bin/daos_admin \
                                                      $DAOS_BASE/install/bin/orig_daos_admin

                                               # set CMOCKA envs here
                                               export CMOCKA_MESSAGE_OUTPUT="xml"
                                               export CMOCKA_XML_FILE="$DAOS_BASE/test_results/%g.xml"
                                               cd $DAOS_BASE
                                               IS_CI=true OLD_CI=false utils/run_test.sh"''',
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
                            /* https://issues.jenkins-ci.org/browse/JENKINS-58952
                             * label is at the end
                            sh label: "Collect artifacts and tear down",
                               script '''set -ex */
                            sh script: '''set -ex
                                      . ./.build_vars.sh
                                      DAOS_BASE=${SL_PREFIX%/install*}
                                      NODE=${NODELIST%%,*}
                                      ssh $SSH_KEY_ARGS jenkins@$NODE "set -x
                                          cd $DAOS_BASE
                                          rm -rf run_test.sh/
                                          mkdir run_test.sh/
                                          if ls /tmp/daos*.log > /dev/null; then
                                              mv /tmp/daos*.log run_test.sh/
                                          fi
                                          # servers can sometimes take a while to stop when the test is done
                                          x=0
                                          while [ \"\\\$x\" -lt \"10\" ] &&
                                                pgrep '(orterun|daos_server|daos_io_server)'; do
                                              sleep 1
                                              let x=\\\$x+1
                                          done
                                          if ! sudo umount /mnt/daos; then
                                              echo \"Failed to unmount $DAOS_BASE\"
                                              ps axf
                                          fi
                                          cd
                                          if ! sudo umount \"$DAOS_BASE\"; then
                                              echo \"Failed to unmount $DAOS_BASE\"
                                              ps axf
                                          fi"
                                      # Note that we are taking advantage of the NFS mount here and if that
                                      # should ever go away, we need to pull run_test.sh/ from $NODE
                                      python utils/fix_cmocka_xml.py''',
                            label: "Collect artifacts and tear down"
                            junit 'test_results/*.xml'
                            archiveArtifacts artifacts: 'run_test.sh/**'
                        }
                    }
                }
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                allOf {
                    // expression { skipTest != true }
                    expression { env.NO_CI_TESTING != 'true' }
                    expression { ! commitPragma(pragma: 'Skip-test').contains('true') }
                }
            }
            parallel {
                stage('Coverity on CentOS 7') {
                    // Eventually this will only run on Master builds.
                    // Unfortunately for now, a PR build could break
                    // the quickbuild, which would not be detected until
                    // the master build fails.
//                    when {
//                        beforeAgent true
//                        anyOf {
//                            branch 'master'
//                            not {
//                                // expression returns false on grep match
//                                expression {
//                                    sh script: 'git show -s --format=%B |' +
//                                               ' grep "^Coverity-test: true"',
//                                    returnStatus: true
//                                }
//                            }
//                        }
//                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                                '$BUILDARGS ' +
                                                '--build-arg QUICKBUILD=true' +
                                                ' --build-arg QUICKBUILD_DEPS="' + env.QUICKBUILD_DEPS +
                                                '" --build-arg CART_COMMIT=-' + env.CART_COMMIT +
                                                ' --build-arg REPOS="' + component_repos + '"'
                        }
                    }
                    steps {
                        sh "rm -f coverity/daos_coverity.tgz"
                        sconsBuild coverity: "daos-stack/daos",
                                   clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-centos7-cov'
                    }
                    post {
                        success {
                            sh """rm -rf _build.external${arch}
                                  mkdir -p coverity
                                  rm -f coverity/*
                                  if [ -e cov-int ]; then
                                      tar czf coverity/daos_coverity.tgz cov-int
                                  fi"""
                            archiveArtifacts artifacts: 'coverity/daos_coverity.tgz',
                                             allowEmptyArchive: true
                        }
                        unsuccessful {
                            sh """mkdir -p coverity
                                  if [ -f config${arch}.log ]; then
                                      mv config${arch}.log coverity/config.log-centos7-cov
                                  fi
                                  if [ -f cov-int/build-log.txt ]; then
                                      mv cov-int/build-log.txt coverity/cov-build-log.txt
                                  fi"""
                            archiveArtifacts artifacts: 'coverity/cov-build-log.txt',
                                             allowEmptyArchive: true
                            archiveArtifacts artifacts: 'coverity/config.log-centos7-cov',
                                             allowEmptyArchive: true
                      }
                    }
                }
                stage('Functional') {
                    when {
                        beforeAgent true
                        expression {
                            ! commitPragma(pragma: 'Skip-func-test').contains('true')
                        }
                    }
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        unstash 'CentOS-rpm-version'
                        script {
                            daos_packages_version = readFile('centos7-rpm-version').trim()
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_daos_repos,
                                       inst_rpms: 'daos-' + daos_packages_version +
                                                  ' daos-client-' + daos_packages_version +
                                                  ' cart-' + env.CART_COMMIT + ' ' +
                                                  functional_rpms
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,-hw
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-9)
                                           # set DAOS_TARGET_OVERSUBSCRIBE env here
                                           export DAOS_TARGET_OVERSUBSCRIBE=0
                                           rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
                                           mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
                                           ./ftest.sh "$test_tag" $tnodes''',
                                junit_files: "install/lib/daos/TESTING/ftest/avocado/*/*/*.xml install/lib/daos/TESTING/ftest/*_results.xml",
                                failure_artifacts: 'Functional'
                    }
                    post {
                        always {
                            sh '''rm -rf install/lib/daos/TESTING/ftest/avocado/*/*/html/
                                  # Remove the latest avocado symlink directory to avoid inclusion in the
                                  # jenkins build artifacts
                                  unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
                                  rm -rf "Functional/"
                                  mkdir "Functional/"
                                  # compress those potentially huge DAOS logs
                                  if daos_logs=$(ls install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/*); then
                                      lbzip2 $daos_logs
                                  fi
                                  arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls -d install/lib/daos/TESTING/ftest/avocado/job-results/* 2>/dev/null)" && arts="$arts"$'\n'
                                  if [ -n "$arts" ]; then
                                      mv $(echo $arts | tr '\n' ' ') "Functional/"
                                  fi'''
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
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
                stage('Functional_Hardware_Small') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { env.DAOS_STACK_CI_HARDWARE_SKIP != 'true' }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test').contains('true')
                            }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test-small').contains('true')
                            }
                        }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label 'ci_nvme3'
                    }
                    steps {
                        unstash 'CentOS-rpm-version'
                        script {
                            daos_packages_version = readFile('centos7-rpm-version').trim()
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 3,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos,
                                       inst_rpms: 'daos-' + daos_packages_version +
                                                  ' daos-client-' + daos_packages_version +
                                                  ' cart-' + env.CART_COMMIT + ' ' +
                                                  functional_rpms
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag-hw-small:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,hw,small
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-3)
                                           clush -B -S -o '-i ci_key' -l root -w ${tnodes} \
                                             "set -x
                                              for i in 0 1; do
                                                if [ -e /sys/class/net/ib\\\$i ]; then
                                                  if ! ifconfig ib\\\$i | grep "inet "; then
                                                    {
                                                      echo \"Found interface ib\\\$i down after reboot on \\\$HOSTNAME\"
                                                      systemctl status
                                                      systemctl --failed
                                                      journalctl -n 500
                                                      ifconfig ib\\\$i
                                                      cat /sys/class/net/ib\\\$i/mode
                                                      ifup ib\\\$i
                                                    } | mail -s \"Interface found down after reboot\" $OPERATIONS_EMAIL
                                                  fi
                                                fi
                                              done"
                                           # set DAOS_TARGET_OVERSUBSCRIBE env here
                                           export DAOS_TARGET_OVERSUBSCRIBE=1
                                           rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
                                           mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
                                           ./ftest.sh "$test_tag" $tnodes "auto:Optane"''',
                                junit_files: "install/lib/daos/TESTING/ftest/avocado/*/*/*.xml install/lib/daos/TESTING/ftest/*_results.xml",
                                failure_artifacts: 'Functional'
                    }
                    post {
                        always {
                            sh '''rm -rf install/lib/daos/TESTING/ftest/avocado/*/*/html/
                                  # Remove the latest avocado symlink directory to avoid inclusion in the
                                  # jenkins build artifacts
                                  unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
                                  rm -rf "Functional/"
                                  mkdir "Functional/"
                                  # compress those potentially huge DAOS logs
                                  if daos_logs=$(ls install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/*); then
                                      lbzip2 $daos_logs
                                  fi
                                  arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls -d install/lib/daos/TESTING/ftest/avocado/job-results/* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls install/lib/daos/TESTING/ftest/*.stacktrace 2>/dev/null || true)"
                                  if [ -n "$arts" ]; then
                                      mv $(echo $arts | tr '\n' ' ') "Functional/"
                                  fi'''
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
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
                stage('Functional_Hardware_Medium') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { env.DAOS_STACK_CI_HARDWARE_SKIP != 'true' }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test').contains('true')
                            }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test-medium').contains('true')
                            }
                        }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label 'ci_nvme5'
                    }
                    steps {
                        unstash 'CentOS-rpm-version'
                        script {
                            daos_packages_version = readFile('centos7-rpm-version').trim()
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 5,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos,
                                       inst_rpms: 'daos-' + daos_packages_version +
                                                  ' daos-client-' + daos_packages_version +
                                                  ' cart-' + env.CART_COMMIT + ' ' +
                                                  functional_rpms
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag-hw-medium:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,hw,medium,ib2
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-5)
                                           clush -B -S -o '-i ci_key' -l root -w ${tnodes} \
                                             "set -x
                                              for i in 0 1; do
                                                if [ -e /sys/class/net/ib\\\$i ]; then
                                                  if ! ifconfig ib\\\$i | grep "inet "; then
                                                    {
                                                      echo \"Found interface ib\\\$i down after reboot on \\\$HOSTNAME\"
                                                      systemctl status
                                                      systemctl --failed
                                                      journalctl -n 500
                                                      ifconfig ib\\\$i
                                                      cat /sys/class/net/ib\\\$i/mode
                                                      ifup ib\\\$i
                                                    } | mail -s \"Interface found down after reboot\" $OPERATIONS_EMAIL
                                                  fi
                                                fi
                                              done"
                                           # set DAOS_TARGET_OVERSUBSCRIBE env here
                                           export DAOS_TARGET_OVERSUBSCRIBE=1
                                           rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
                                           mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
                                           ./ftest.sh "$test_tag" $tnodes "auto:Optane"''',
                                junit_files: "install/lib/daos/TESTING/ftest/avocado/*/*/*.xml install/lib/daos/TESTING/ftest/*_results.xml",
                                failure_artifacts: 'Functional'
                    }
                    post {
                        always {
                            sh '''rm -rf install/lib/daos/TESTING/ftest/avocado/*/*/html/
                                  # Remove the latest avocado symlink directory to avoid inclusion in the
                                  # jenkins build artifacts
                                  unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
                                  rm -rf "Functional/"
                                  mkdir "Functional/"
                                  # compress those potentially huge DAOS logs
                                  if daos_logs=$(ls install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/*); then
                                      lbzip2 $daos_logs
                                  fi
                                  arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls -d install/lib/daos/TESTING/ftest/avocado/job-results/* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls install/lib/daos/TESTING/ftest/*.stacktrace 2>/dev/null || true)"
                                  if [ -n "$arts" ]; then
                                      mv $(echo $arts | tr '\n' ' ') "Functional/"
                                  fi'''
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
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
                stage('Functional_Hardware_Large') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { env.DAOS_STACK_CI_HARDWARE_SKIP != 'true' }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test').contains('true')
                            }
                            expression {
                                ! commitPragma(pragma: 'Skip-func-hw-test-large').contains('true')
                            }
                        }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label 'ci_nvme9'
                    }
                    steps {
                        unstash 'CentOS-rpm-version'
                        script {
                            daos_packages_version = readFile('centos7-rpm-version').trim()
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos,
                                       inst_rpms: 'daos-' + daos_packages_version +
                                                  ' daos-client-' + daos_packages_version +
                                                  ' cart-' + env.CART_COMMIT + ' ' +
                                                  functional_rpms
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag-hw-large:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,hw,large
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-9)
                                           clush -B -S -o '-i ci_key' -l root -w ${tnodes} \
                                             "set -x
                                              for i in 0 1; do
                                                if [ -e /sys/class/net/ib\\\$i ]; then
                                                  if ! ifconfig ib\\\$i | grep "inet "; then
                                                    {
                                                      echo \"Found interface ib\\\$i down after reboot on \\\$HOSTNAME\"
                                                      systemctl status
                                                      systemctl --failed
                                                      journalctl -n 500
                                                      ifconfig ib\\\$i
                                                      cat /sys/class/net/ib\\\$i/mode
                                                      ifup ib\\\$i
                                                    } | mail -s \"Interface found down after reboot\" $OPERATIONS_EMAIL
                                                  fi
                                                fi
                                              done"
                                           # set DAOS_TARGET_OVERSUBSCRIBE env here
                                           export DAOS_TARGET_OVERSUBSCRIBE=1
                                           rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
                                           mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
                                           ./ftest.sh "$test_tag" $tnodes "auto:Optane"''',
                                junit_files: "install/lib/daos/TESTING/ftest/avocado/*/*/*.xml install/lib/daos/TESTING/ftest/*_results.xml",
                                failure_artifacts: 'Functional'
                    }
                    post {
                        always {
                            sh '''rm -rf install/lib/daos/TESTING/ftest/avocado/*/*/html/
                                  # Remove the latest avocado symlink directory to avoid inclusion in the
                                  # jenkins build artifacts
                                  unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
                                  rm -rf "Functional/"
                                  mkdir "Functional/"
                                  # compress those potentially huge DAOS logs
                                  if daos_logs=$(ls install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/*); then
                                      lbzip2 $daos_logs
                                  fi
                                  arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                                  arts="$arts$(ls -d install/lib/daos/TESTING/ftest/avocado/job-results/* 2>/dev/null)" && arts="$arts"$'\n'
                                  if [ -n "$arts" ]; then
                                      mv $(echo $arts | tr '\n' ' ') "Functional/"
                                  fi'''
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
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
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression {
                                ! commitPragma(pragma: 'Skip-test-centos-rpms').contains('true')
                            }
                        }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        unstash 'CentOS-rpm-version'
                        script {
                            daos_packages_version = readFile('centos7-rpm-version').trim()
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_daos_repos,
                                       inst_rpms: 'environment-modules'
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            runTest script: "${rpm_test_pre}" +
                                            "sudo yum -y install daos-client-${daos_packages_version}\n" +
                                            "sudo yum -y history rollback last-1\n" +
                                            "sudo yum -y install daos-server-${daos_packages_version}\n" +
                                            "sudo yum -y install daos-tests-${daos_packages_version}\n" +
                                            "${rpm_test_daos_test}" + '"',
                                    junit_files: null,
                                    failure_artifacts: env.STAGE_NAME, ignore_failure: true
                        }
                    }
                }
            }
        }
    }
    post {
        unsuccessful {
            notifyBrokenBranch branches: daos_branch
        }
    }
}
