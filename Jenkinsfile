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

def arch = "-Linux"
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def component_repos = ""
def cart_repo = "cart@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
def cart_repos = component_repos + ' ' + cart_repo
//def cart_rpms = "openpa libfabric pmix ompi mercury"
// don't need to install any RPMs for testing yet
def cart_rpms = ""

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (env.BRANCH_NAME != "weekly-testing" &&
     env.BRANCH_NAME != "master" &&
     env.BRANCH_NAME != "daos_devel")) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *' : '')
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
        SSH_KEY_ARGS="-ici_key"
        CLUSH_ARGS="-o$SSH_KEY_ARGS"
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
            /* Don't use failFast here as whilst it avoids using extra resources
             * and gives faster results for PRs it's also on for master where we
             * do want complete results in the case of partial failure
             */
            //failFast true
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression {
                    sh script: 'git show -s --format=%B | grep "^Skip-build: true"',
                       returnStatus: true
                }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
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
                                      if git show -s --format=%B | grep "^Skip-build: true"; then
                                          exit 0
                                      fi
                                      make CHROOT_NAME="epel-7-x86_64" -C utils/rpms chrootbuild'''
                    }
                    post {
                        success {
                            sh label: "Collect artifacts",
                               script: '''mockroot=/var/lib/mock/epel-7-x86_64
                                          (cd $mockroot/result/ &&
                                           cp -r . $OLDPWD/artifacts/centos7/)
                                          createrepo artifacts/centos7/
                                          cat $mockroot/result/{root,build}.log'''
                            publishToRepository product: 'cart',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'el-7',
                                                repo_dir: 'artifacts/centos7/'
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
                               script: '''mockroot=/var/lib/mock/epel-7-x86_64
                                          artdir=$PWD/artifacts/centos7
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/centos7/**'
                        }
                    }
                }
                stage('Build RPM on SLES 12.3') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { false }
                            environment name: 'SLES12_3_DOCKER', value: 'true'
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
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/sles12.3/
                              mkdir -p artifacts/sles12.3/
                              if git show -s --format=%B | grep "^Skip-build: true"; then
                                  exit 0
                              fi
                              make CHROOT_NAME="suse-12.3-x86_64" -C utils/rpms chrootbuild'''
                    }
                    post {
                        success {
                            sh label: "Collect artifacts",
                               script: '''mockroot=/var/lib/mock/suse-12.3-x86_64
                                          (cd $mockroot/result/ &&
                                           cp -r . $OLDPWD/artifacts/sles12.3/)
                                          createrepo artifacts/sles12.3/
                                          cat $mockroot/result/{root,build}.log'''
                            publishToRepository product: 'cart',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'sles-12',
                                                repo_dir: 'artifacts/sles12.3/'
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
                               script: '''mockroot=/var/lib/mock/suse-12.3-x86_64
                                          cat $mockroot/result/{root,build}.log
                                          artdir=$PWD/artifacts/sles12.3
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/sles12.3/**'
                        }
                    }
                }
                stage('Build RPM on Leap 42.3') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { false }
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
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/leap42.3/
                              mkdir -p artifacts/leap42.3/
                              if git show -s --format=%B | grep "^Skip-build: true"; then
                                  exit 0
                              fi
                              make CHROOT_NAME="opensuse-leap-42.3-x86_64" -C utils/rpms chrootbuild'''
                    }
                    post {
                        success {
                            sh label: "Collect artifacts",
                               script: '''mockroot=/var/lib/mock/opensuse-leap-42.3-x86_64
                                          (cd $mockroot/result/ &&
                                           cp -r . $OLDPWD/artifacts/leap42.3/)
                                          createrepo artifacts/leap42.3/
                                          cat $mockroot/result/{root,build}.log'''
                            publishToRepository product: 'cart',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'leap-42',
                                                repo_dir: 'artifacts/leap42.3/'
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
                               script: '''mockroot=/var/lib/mock/opensuse-leap-42.3-x86_64
                                          cat $mockroot/result/{root,build}.log
                                          artdir=$PWD/artifacts/leap42.3
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/leap42.3/**'
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
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/leap15/
                              mkdir -p artifacts/leap15/
                              if git show -s --format=%B | grep "^Skip-build: true"; then
                                  exit 0
                              fi
                              make CHROOT_NAME="opensuse-leap-15.1-x86_64" -C utils/rpms chrootbuild'''
                    }
                    post {
                        success {
                            sh label: "Collect artifacts",
                               script: '''(cd /var/lib/mock/opensuse-leap-15.1-x86_64/result/ &&
                                           cp -r . $OLDPWD/artifacts/leap15/)
                                          createrepo artifacts/leap15/'''
                            publishToRepository product: 'cart',
                                                format: 'yum',
                                                maturity: 'stable',
                                                tech: 'leap-15',
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
                                          cat $mockroot/result/{root,build}.log
                                          artdir=$PWD/artifacts/leap15
                                          if srpms=$(ls _topdir/SRPMS/*); then
                                              cp -af $srpms $artdir
                                          fi
                                          (if cd $mockroot/result/; then
                                               cp -r . $artdir
                                           fi)
                                          cat $mockroot/result/{root,build}.log \
                                              2>/dev/null || true'''
                        }
                        cleanup {
                            archiveArtifacts artifacts: 'artifacts/leap15/**'
                        }
                    }
                }
                stage('Build DEB on Ubuntu 18.04') {
                    when {
                        expression { false }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.04'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) ' +
                              ' --build-arg JENKINS_URL=' + env.JENKINS_URL +
                              ' --build-arg CACHEBUST=' +
                              currentBuild.startTimeInMillis
                        }
                    }
                    steps {
                        githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/ubuntu18.04/
                              mkdir -p artifacts/ubuntu18.04/
                              : "${DEBEMAIL:="$env.DAOS_EMAIL"}"
                              : "${DEBFULLNAME:="$env.DAOS_FULLNAME"}"
                              export DEBEMAIL
                              export DEBFULLNAME
                              make -C utils/rpms debs'''
                    }
                    post {
                        success {
                            sh '''ln -v \
                                   _topdir/BUILD/*{.build,.changes,.deb,.dsc,.gz,.xz} \
                                   artifacts/ubuntu18.04/
                                  pushd artifacts/ubuntu18.04/
                                    dpkg-scanpackages . /dev/null | \
                                      gzip -9c > Packages.gz
                                  popd'''
                            archiveArtifacts artifacts: 'artifacts/ubuntu18.04/**'
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unsuccessful {
                            sh script: "cat _topdir/BUILD/*.build",
                               returnStatus: true
                            archiveArtifacts artifacts: 'artifacts/ubuntu18.04/**'
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE"
                        }
                    }
                }
                stage('Build DEB on Ubuntu 18.10') {
                    when {
                        expression { false }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.10'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) ' +
                              ' --build-arg JENKINS_URL=' + env.JENKINS_URL +
                              ' --build-arg CACHEBUST=' +
                              currentBuild.startTimeInMillis
                        }
                    }
                    steps {
                        githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        sh label: env.STAGE_NAME,
                           script: '''rm -rf artifacts/ubuntu18.10/
                              mkdir -p artifacts/ubuntu18.10/
                              : "${DEBEMAIL:="$env.DAOS_EMAIL"}"
                              : "${DEBFULLNAME:="$env.DAOS_FULLNAME"}"
                              export DEBEMAIL
                              export DEBFULLNAME
                              make -C utils/rpms debs'''
                    }
                    post {
                        success {
                            sh '''ln -v \
                                   _topdir/BUILD/*{.build,.changes,.deb,.dsc,.gz,.xz} \
                                   artifacts/ubuntu18.10/
                                  pushd artifacts/ubuntu18.10/
                                    dpkg-scanpackages . /dev/null | \
                                      gzip -9c > Packages.gz
                                  popd'''
                            archiveArtifacts artifacts: 'artifacts/ubuntu18.10/**'
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unsuccessful {
                            sh script: "cat _topdir/BUILD/*.build",
                               returnStatus: true
                            archiveArtifacts artifacts: 'artifacts/ubuntu18.10/**'
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE"
                        }
                    }
                }
                stage('Build master CentOS 7') {
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
                        sconsBuild(scons_exe: "scons-3",
                                   clean: "_build.external${arch}",
                                   scons_args: '--build-config=utils/build-master.config')
                        // this really belongs in the test stage CORCI-530
                        sh '''scons-3 utest --utest-mode=memcheck
                              mv build/Linux/src/utest{,_valgrind}
                              scons-3 utest'''
                        stash name: 'CentOS-master-install', includes: 'install/**'
                        stash name: 'CentOS-master-build-vars', includes: ".build_vars${arch}.*"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-master-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
                            }
                            archiveArtifacts artifacts: '''build/Linux/src/utest_valgrind/utest.log,
                                                           build/Linux/src/utest_valgrind/test_output,
                                                           build/Linux/src/utest/utest.log,
                                                           build/Linux/src/utest/test_output'''
                        }
                        unsuccessful {
                            sh "mv config${arch}.log config.log-master"
                            archiveArtifacts artifacts: 'config.log-master'
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
                                                '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild(scons_exe: "scons-3",
                                   clean: "_build.external${arch}")
                        // this really belongs in the test stage CORCI-530
                        sh '''scons-3 utest --utest-mode=memcheck
                              mv build/Linux/src/utest{,_valgrind}
                              scons-3 utest'''
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: ".build_vars${arch}.*"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
                            }
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
                        /* temporarily moved into stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        */
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
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild(scons_exe: "scons-3",
                                   clean: "_build.external${arch}",
                                   COMPILER: "clang")
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                        branch 'master'
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
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite", COMPILER: "clang"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                stage('Build on SLES 12.3') {
                    when {
                        beforeAgent true
                        allOf {
                            environment name: 'SLES12_3_DOCKER', value: 'true'
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.sles.12.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-sles12.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite"
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-sles12.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-sles12.3-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-sles12.3-gcc',
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
                stage('Build on Leap 42.3') {
                    when {
                        beforeAgent true
                        allOf {
                            environment name: 'LEAP42_3_DOCKER', value: 'true'
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.42.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap42.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite"
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: false,
                                             id: "analysis-leap42.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap42.3-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap42.3-gcc',
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
                        branch 'master'
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
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                        branch 'master'
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
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite", COMPILER: "clang"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
                stage('Build on Leap 15 with Intel-C') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
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
                        sconsBuild clean: "_build.external${arch} .sconsign${arch}.dblite", COMPILER: "icc"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-intelc",
                                             tools: [ intel(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external-Linux\\/.*'),
                                                       excludeFile('_build\\.external-Linux\\/.*')]
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
        stage('Test') {
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression { env.NO_CI_TESTING != 'true' }
                expression {
                    sh script: 'git show -s --format=%B | grep "^Skip-test: true"',
                       returnStatus: true
                }
            }
            parallel {
                stage('Single-node') {
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       snapshot: true,
                                       inst_repos: component_repos,
                                       inst_rpms: cart_rpms
                        timeout (time: 30, unit: 'MINUTES') {
                            runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                    script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                               export CART_TEST_MODE=native
                                               bash -x ./multi-node-test.sh 1 ''' +
                                               env.NODELIST + ''' one_node''',
                                    junit_files: "install/Linux/TESTING/avocado/job-results/CART_1node/*/*.xml"
                        }
                    }
                    post {
                        always {
                            sh '''rm -rf install/Linux/TESTING/avocado/job-results/CART_1node/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      mv install/Linux/TESTING/avocado/job-results/CART_1node/* \
                                         "$STAGE_NAME/" || true
                                      mv install/Linux/TESTING/testLogs-1_node \
                                         "$STAGE_NAME/" || true
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/results.xml'
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
                stage('Single-node-valgrind') {
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       snapshot: true,
                                       inst_repos: component_repos,
                                       inst_rpms: cart_rpms
                        timeout (time: 30, unit: 'MINUTES') {
                            runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                    script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                               export CART_TEST_MODE=memcheck
                                               bash -x ./multi-node-test.sh 1 ''' +
                                               env.NODELIST + ''' one_node''',
                                    junit_files: "install/Linux/TESTING/avocado/job-results/CART_1vgdnode/*/*.xml"
                            }
                    }
                    post {
                        always {
                            sh '''rm -rf install/Linux/TESTING/avocado/job-results/CART_1vgdnode/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      mv install/Linux/TESTING/avocado/job-results/CART_1vgdnode/* \
                                         "$STAGE_NAME/" || true
                                      mv install/Linux/TESTING/testLogs-1vgd_node \
                                         "$STAGE_NAME/" || true
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            publishValgrind (
                                failBuildOnInvalidReports: true,
                                failBuildOnMissingReports: true,
                                failThresholdDefinitelyLost: '0',
                                failThresholdInvalidReadWrite: '0',
                                failThresholdTotal: '0',
                                pattern: '**/*.memcheck',
                                publishResultsForAbortedBuilds: false,
                                publishResultsForFailedBuilds: false,
                                sourceSubstitutionPaths: '',
                                unstableThresholdDefinitelyLost: '',
                                unstableThresholdInvalidReadWrite: '',
                                unstableThresholdTotal: ''
                                )
                            junit env.STAGE_NAME + '/*/results.xml'
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
                stage('Two-node') {
                    agent {
                        label 'ci_vm2'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 2,
                                       snapshot: true,
                                       inst_repos: component_repos,
                                       inst_rpms: cart_rpms
                        timeout (time: 30, unit: 'MINUTES') {
                            runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                    script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                               export CART_TEST_MODE=none
                                               bash -x ./multi-node-test.sh 2 ''' +
                                               env.NODELIST + ''' two_node''',
                                    junit_files: "install/Linux/TESTING/avocado/job-results/CART_2node/*/*.xml"
                        }
                    }
                    post {
                        always {
                            sh '''rm -rf install/Linux/TESTING/avocado/job-results/CART_2node/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      mv install/Linux/TESTING/avocado/job-results/CART_2node/* \
                                         "$STAGE_NAME/" || true
                                      mv install/Linux/TESTING/testLogs-2_node \
                                         "$STAGE_NAME/" || true
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/results.xml'
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
                stage('Three-node') {
                    agent {
                        label 'ci_vm3'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 3,
                                       snapshot: true,
                                       inst_repos: component_repos,
                                       inst_rpms: cart_rpms
                        timeout (time: 30, unit: 'MINUTES') {
                            runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                    script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                               export CART_TEST_MODE=none
                                               bash -x ./multi-node-test.sh 3 ''' +
                                               env.NODELIST + ''' three_node''',
                                    junit_files: "install/Linux/TESTING/avocado/job-results/CART_3node/*/*.xml"
                        }
                    }
                    post {
                        always {
                            sh '''rm -rf install/Linux/TESTING/avocado/job-results/CART_3node/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      mv install/Linux/TESTING/avocado/job-results/CART_3node/* \
                                         "$STAGE_NAME/" || true
                                      mv install/Linux/TESTING/testLogs-3_node \
                                         "$STAGE_NAME/" || true
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/results.xml'
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
                stage('Five-node') {
                    agent {
                        label 'ci_vm5'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 5,
                                       snapshot: true,
                                       inst_repos: component_repos,
                                       inst_rpms: cart_rpms
                        timeout (time: 30, unit: 'MINUTES') {
                            runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                    script: '''export PDSH_SSH_ARGS_APPEND="-i ci_key"
                                               export CART_TEST_MODE=none
                                               bash -x ./multi-node-test.sh 5 ''' +
                                               env.NODELIST + ''' five_node''',
                                    junit_files: "install/Linux/TESTING/avocado/job-results/CART_5node/*/*.xml"
                        }
                    }
                    post {
                        always {
                            sh '''rm -rf install/Linux/TESTING/avocado/job-results/CART_5node/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      mv install/Linux/TESTING/avocado/job-results/CART_5node/* \
                                         "$STAGE_NAME/" || true
                                      mv install/Linux/TESTING/testLogs-5_node \
                                         "$STAGE_NAME/" || true
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            junit env.STAGE_NAME + '/*/results.xml'
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
            }
        }
    }
}
