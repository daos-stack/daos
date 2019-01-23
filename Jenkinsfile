#!/usr/bin/groovy
/* Copyright (C) 2016-2019 Intel Corporation
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
//@Library(value="pipeline-lib@debug") _

def singleNodeTest() {
    provisionNodes NODELIST: env.NODELIST,
                   node_count: 1,
                   snapshot: true
    runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
            script: """set -x
                       . ./.build_vars-Linux.sh
                       CART_BASE=\${SL_PREFIX%/install*}
                       NODELIST=$nodelist
                       NODE=\${NODELIST%%,*}
                       trap 'set +e; set -x; ssh -i ci_key jenkins@\$NODE "set -ex; sudo umount \$CART_BASE"' EXIT
                       ssh -i ci_key jenkins@\$NODE "set -x
                           set -e
                           sudo mkdir -p \$CART_BASE
                           sudo mount -t nfs \$HOSTNAME:\$PWD \$CART_BASE
                           cd \$CART_BASE
                           if RUN_UTEST=false bash -x utils/run_test.sh; then
                               echo \"run_test.sh exited successfully with \\\${PIPESTATUS[0]}\"
                           else
                               rc=\\\${PIPESTATUS[0]}
                               echo \"run_test.sh exited failure with \\\$rc\"
                           fi"
                       mkdir -p install/Linux/TESTING/
                       scp -i ci_key -r jenkins@\$NODE:\$CART_BASE/install/Linux/TESTING/testLogs \
                                        install/Linux/TESTING/
                       exit \$rc""",
          junit_files: null
}

def arch="-Linux"

pipeline {
    agent any

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
        stage('Pre-build') {
            parallel {
                stage('checkpatch') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW,
                                   ignored_files: "src/control/vendor/*"
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
            // abort other builds if/when one fails to avoid wasting time
            // and resources
            failFast true
            parallel {
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}"
                        // this really belongs in the test stage CORCI-530
                        sh '''scons utest --utest-mode=memcheck
                              mv build/Linux/src/utest{,_valgrind}
                              scons utest'''
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: ".build_vars${arch}.*"
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: '''build/Linux/src/utest_valgrind/utest.log,
                                                           build/Linux/src/utest_valgrind/test_output,
                                                           build/Linux/src/utest/utest.log,
                                                           build/Linux/src/utest/test_output'''
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        */
                        }
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-centos7",
                                         tools: [ gcc4(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when { branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-centos7-clang",
                                         tools: [ clang(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    when { branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu:18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu18",
                                         tools: [ gcc4(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on Ubuntu 18.04 with Clang') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu:18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu18-clang",
                                         tools: [ clang(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on Leap 15') {
                    when { branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap:15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15",
                                         tools: [ gcc4(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when { branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap:15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-clang",
                                         tools: [ clang(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
                stage('Build on Leap 15 with Intel-C') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap:15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                            args '-v /opt/intel:/opt/intel'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "icc"
                    }
                    post {
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                        always {
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                        }
                        */
                        success {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-intelc",
                                         tools: [ intel(), cppCheck() ],
                                         filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                   excludeFile('_build\\.external-Linux\\/.*')]
                        /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Single-node') {
                    agent {
                        label 'ci_vm1'
                    }
                    environment {
                        CART_TEST_MODE = 'native'
                    }
                    steps {
                        singleNodeTest()
                    }
                    post {
                        always {
                            /* Uncomment this on a day when unit testing works without
                             * having to build the test in the test phase
                             archiveArtifacts artifacts: '''install/Linux/TESTING/testLogs/**,
                                                            build/Linux/src/utest/utest.log,
                                                            build/Linux/src/utest/test_output'''
                             */
                             archiveArtifacts artifacts: 'install/Linux/TESTING/testLogs/**'
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
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
                stage('Single-node-valgrind') {
                    agent {
                        label 'ci_vm1'
                    }
                    environment {
                        CART_TEST_MODE = 'memcheck'
                    }
                    steps {
                        singleNodeTest()
                    }
                    post {
                        always {
                            /* Uncomment this on a day when unit testing works without
                             * having to build the test in the test phase
                            sh '''mv install/Linux/TESTING/testLogs{,_valgrind}
                                  mv build/Linux/src/utest{,_valgrind}'''
                            archiveArtifacts artifacts: '''install/Linux/TESTING/testLogs_valgrind/**,
                                                           build/Linux/src/utest_valgrind/utest.log,
                                                           build/Linux/src/utest_valgrind/test_output'''
                             */
                            sh 'mv install/Linux/TESTING/testLogs{,_valgrind}'
                            archiveArtifacts artifacts: 'install/Linux/TESTING/testLogs_valgrind/**'
                        /* when JENKINS-39203 is resolved, can probably use stepResult
                           here and remove the remaining post conditions
                           stepResult name: env.STAGE_NAME,
                                   context: 'build/' + env.STAGE_NAME,
                                    result: ${currentBuild.currentResult}
                        */
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
                stage('Two-node') {
                    agent {
                        label 'ci_vm2'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 2,
                                       snapshot: true
                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/exascale/jenkins',
                                    checkoutDir: 'jenkins',
                                    credentialsId: 'daos-gerrit-read'

                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/coral/scony_python-junit',
                                    checkoutDir: 'scony_python-junit',
                                    credentialsId: 'daos-gerrit-read'

                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                           bash -x ./multi-node-test.sh 2 ''' +
                                           env.NODELIST,
                                junit_files: "CART_2-node_junit.xml"
                    }
                    post {
                        always {
                            junit 'CART_2-node_junit.xml'
                            archiveArtifacts artifacts: 'install/Linux/TESTING/testLogs-2_node/**'
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
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
                stage('Three-node') {
                    agent {
                        label 'ci_vm3'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 3,
                                       snapshot: true
                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/exascale/jenkins',
                                    checkoutDir: 'jenkins',
                                    credentialsId: 'daos-gerrit-read'

                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/coral/scony_python-junit',
                                    checkoutDir: 'scony_python-junit',
                                    credentialsId: 'daos-gerrit-read'

                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                           bash -x ./multi-node-test.sh 3 ''' +
                                           env.NODELIST,
                                junit_files: "CART_3-node_junit.xml"
                    }
                    post {
                        always {
                            junit 'CART_3-node_junit.xml'
                            archiveArtifacts artifacts: 'install/Linux/TESTING/testLogs-3_node/**'
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
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
                stage('Five-node') {
                    agent {
                        label 'ci_vm5'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 5,
                                       snapshot: true
                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/exascale/jenkins',
                                    checkoutDir: 'jenkins',
                                    credentialsId: 'daos-gerrit-read'

                        checkoutScm url: 'ssh://review.hpdd.intel.com:29418/coral/scony_python-junit',
                                    checkoutDir: 'scony_python-junit',
                                    credentialsId: 'daos-gerrit-read'

                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                           bash -x ./multi-node-test.sh 5 ''' +
                                           env.NODELIST,
                                junit_files: "CART_5-node_junit.xml"
                    }
                    post {
                        always {
                            junit 'CART_5-node_junit.xml'
                            archiveArtifacts artifacts: 'install/Linux/TESTING/testLogs-5_node/**'
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
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
            }
        }
    }
}
