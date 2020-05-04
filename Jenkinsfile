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

def doc_only_change() {
    def rc = sh script: 'if [ "' + env.CHANGE_ID + '''" = "null" ]; then
                              mb_modifier="^"
                         fi
                         git diff-tree --no-commit-id --name-only \
                           $(git merge-base origin/''' + target_branch +
                      '''$mb_modifier HEAD) HEAD | \
                           grep -v -e "^doc$"''',
                returnStatus: true

    return rc == 1
}

def skip_stage(String stage) {
    return commitPragma(pragma: 'Skip-' + stage).contains('true')
}

def quickbuild() {
    return commitPragma(pragma: 'Quick-build') == 'true'
}

target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
def arch = ""
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def qb_inst_rpms = ""
def daos_packages_version = ""
def el7_component_repos = ""
def component_repos = ""
def daos_repo = "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
def el7_daos_repos = el7_component_repos + ' ' + component_repos + ' ' + daos_repo
def functional_rpms  = "--exclude openmpi openmpi3 hwloc ndctl " +
                       "ior-hpc-cart-4-daos-0 mpich-autoload-cart-4-daos-0 " +
                       "romio-tests-cart-4-daos-0 hdf5-tests-cart-4-daos-0 " +
                       "mpi4py-tests-cart-4-daos-0 testmpio-cart-4-daos-0 fio"

def rpm_test_pre = '''if git show -s --format=%B | grep "^Skip-test: true"; then
                          exit 0
                      fi
                      nodelist=(${NODELIST//,/ })
                      src/tests/ftest/config_file_gen.py -n ${nodelist[0]} -a /tmp/daos_agent.yml -s /tmp/daos_server.yml
                      src/tests/ftest/config_file_gen.py -n $nodelist -d /tmp/dmg.yml
                      scp -i ci_key /tmp/daos_agent.yml jenkins@${nodelist[0]}:/tmp
                      scp -i ci_key /tmp/dmg.yml jenkins@${nodelist[0]}:/tmp
                      scp -i ci_key /tmp/daos_server.yml jenkins@${nodelist[0]}:/tmp
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
                            sudo cp /tmp/daos_server.yml /etc/daos/daos_server.yml
                            sudo cp /tmp/daos_agent.yml /etc/daos/daos_agent.yml
                            sudo cp /tmp/dmg.yml /etc/daos/daos.yml
                            cat /etc/daos/daos_server.yml
                            cat /etc/daos/daos_agent.yml
                            cat /etc/daos/daos.yml
                            module load mpi/openmpi3-x86_64
                            coproc daos_server --debug start -t 1 --recreate-superblocks
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
                            OFI_INTERFACE=eth0 daos_test -m'''

def rpm_scan_pre = '''set -ex
                      lmd_tarball='maldetect-current.tar.gz'
                      if test -e "${lmd_tarball}"; then
                        zflag="-z ${lmd_tarball}"
                      else
                        zflag=
                      fi
                      curl http://rfxn.com/downloads/${lmd_tarball} \
                        ${zflag} --silent --show-error --fail -o ${lmd_tarball}
                      nodelist=(${NODELIST//,/ })
                      scp -i ci_key ${lmd_tarball} \
                        jenkins@${nodelist[0]}:/var/tmp
                      ssh -i ci_key jenkins@${nodelist[0]} "set -ex\n'''

def rpm_scan_test = '''lmd_src=\\\"maldet-current\\\"
                       lmd_tarball=\\\"maldetect-current.tar.gz\\\"
                       rm -rf /var/tmp/\\\${lmd_src}
                       mkdir -p /var/tmp/\\\${lmd_src}
                       tar -C /var/tmp/\\\${lmd_src} --strip-components=1 \
                         -xf /var/tmp/\\\${lmd_tarball}
                       pushd /var/tmp/\\\${lmd_src}
                         sudo ./install.sh
                         sudo ln -s /usr/local/maldetect/ /bin/maldet
                       popd
                       sudo freshclam
                       rm -f /var/tmp/clamscan.out
                       rm /var/tmp/\\\${lmd_tarball}
                       rm -rf /var/tmp/\\\${lmd_src}
                       sudo clamscan -d /usr/local/maldetect/sigs/rfxn.ndb \
                                -d /usr/local/maldetect/sigs/rfxn.hdb -r \
                                --exclude-dir=/usr/local/maldetect \
                                --exclude-dir=/usr/share/clamav \
                                --exclude-dir=/var/lib/clamav \
                                --exclude-dir=/sys \
                                --exclude-dir=/proc \
                                --exclude-dir=/dev \
                                --infected / | tee /var/tmp/clamscan.out
                       rm -f /var/tmp/maldetect.xml
                       if grep 'Infected files: 0$' /var/tmp/clamscan.out; then
                         cat << EOF_GOOD > /var/tmp/maldetect.xml
<testsuite skip=\\\"0\\\" failures=\\\"0\\\" errors=\\\"0\\\" tests=\\\"1\\\" name=\\\"Malware_Scan\\\">
  <testcase name=\\\"Malware_scan\\\" classname=\\\"ClamAV\\\"/>
</testsuite>
EOF_GOOD
                       else
                         cat << EOF_BAD > /var/tmp/maldetect.xml
<testsuite skip=\\\"0\\\" failures=\\\"1\\\" errors=\\\"0\\\" tests=\\\"1\\\" name=\\\"Malware_Scan\\\">
  <testcase name=\\\"Malware_scan\\\" classname=\\\"ClamAV\\\">
    <failure message=\\\"Malware Detected\\\" type=\\\"error\\\">
      <![CDATA[ \\\"\\\$(cat /var/tmp/clamscan.out)\\\" ]]>
    </failure>
  </testcase>
</testsuite>
EOF_BAD
                       fi'''

def rpm_scan_post = '''rm -f ${WORKSPACE}/maldetect.xml
                       scp -i ci_key \
                         jenkins@${nodelist[0]}:/var/tmp/maldetect.xml \
                         ${WORKSPACE}/maldetect.xml'''


// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith("weekly-testing") &&
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
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        QUICKBUILD_DEPS = sh(script: "rpmspec -q --srpm --requires utils/rpms/daos.spec 2>/dev/null",
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
                    not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                }
            }
            parallel {
                stage('checkpatch') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('checkpatch') }
                            expression { ! doc_only_change() }
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
                                   ignored_files: "src/control/vendor/*:src/include/daos/*.pb-c.h:src/common/*.pb-c.[ch]:src/mgmt/*.pb-c.[ch]:src/iosrv/*.pb-c.[ch]:src/security/*.pb-c.[ch]:*.crt:*.pem:*_test.go:src/cart/_structures_from_macros_.h"
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
                } // stage('checkpatch')
                stage('Python Bandit check') {
                    when {
                      beforeAgent true
                      expression {
                        ! commitPragma(pragma: 'Skip-python-bandit',
                                def_val: 'true').contains('true')
                      }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.code_scanning'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) --build-arg JENKINS_URL=' +
                                                env.JENKINS_URL
                        }
                    }
                    steps {
                         githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            runTest script: '''bandit --format xml -o bandit.xml \
                                                      -r $(git ls-tree --name-only HEAD)''',
                                    junit_files: "bandit.xml",
                                    ignore_failure: true
                        }
                    }
                    post {
                        always {
                            junit 'bandit.xml'
                        }
                    }
                } // stage('Python Bandit check')
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
                anyOf {
                    // always build branch landings as we depend on lastSuccessfulBuild
                    // always having RPMs in it
                    branch target_branch
                    allOf {
                        expression { ! skip_stage('build') }
                        expression { ! doc_only_change() }
                    }
                }
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
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
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
                                                '--build-arg QUICKBUILD=' + quickbuild() +
                                                ' --build-arg QUICKBUILD_DEPS="' + env.QUICKBUILD_DEPS +
                                                '" --build-arg REPOS="' + component_repos + '"'
                        }
                    }
                    steps {
                        // cycle through debug, release, and dev builds
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-centos7-gcc-debug',
                                   BUILD_TYPE: 'debug'
                        sconsBuild skip_clean: '1', failure_artifacts: 'config.log-centos7-gcc-release',
                                   BUILD_TYPE: 'release'
                        sconsBuild skip_clean: '1', failure_artifacts: 'config.log-centos7-gcc-dev',
                                   BUILD_TYPE: 'dev'
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: ".build_vars${arch}.*"
                        stash name: 'CentOS-tests',
                                    includes: '''build/dev/gcc/src/cart/src/utest/test_linkage,
                                                 build/dev/gcc/src/cart/src/utest/test_gurt,
                                                 build/dev/gcc/src/cart/src/utest/utest_hlc,
                                                 build/dev/gcc/src/cart/src/utest/utest_swim,
                                                 build/dev/gcc/src/rdb/raft/src/tests_main,
                                                 build/dev/gcc/src/common/tests/btree_direct,
                                                 build/dev/gcc/src/common/tests/btree,
                                                 build/dev/gcc/src/common/tests/sched,
                                                 build/dev/gcc/src/common/tests/drpc_tests,
                                                 build/dev/gcc/src/common/tests/acl_api_tests,
                                                 build/dev/gcc/src/common/tests/acl_valid_tests,
                                                 build/dev/gcc/src/common/tests/acl_util_tests,
                                                 build/dev/gcc/src/common/tests/acl_principal_tests,
                                                 build/dev/gcc/src/common/tests/acl_real_tests,
                                                 build/dev/gcc/src/common/tests/prop_tests,
                                                 build/dev/gcc/src/iosrv/tests/drpc_progress_tests,
                                                 build/dev/gcc/src/control/src/github.com/daos-stack/daos/src/control/mgmt,
                                                 build/dev/gcc/src/client/api/tests/eq_tests,
                                                 build/dev/gcc/src/iosrv/tests/drpc_handler_tests,
                                                 build/dev/gcc/src/iosrv/tests/drpc_listener_tests,
                                                 build/dev/gcc/src/mgmt/tests/srv_drpc_tests,
                                                 build/dev/gcc/src/security/tests/cli_security_tests,
                                                 build/dev/gcc/src/security/tests/srv_acl_tests,
                                                 build/dev/gcc/src/vos/vea/tests/vea_ut,
                                                 build/dev/gcc/src/common/tests/umem_test,
                                                 build/dev/gcc/src/bio/smd/tests/smd_ut,
                                                 utils/sl/build_info/**,
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
                            branch target_branch
                            expression { ! quickbuild() }
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
                            branch target_branch
                            expression { ! quickbuild() }
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
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! quickbuild() }
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
                            branch target_branch
                            expression { ! quickbuild() }
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
                            branch target_branch
                            expression { ! quickbuild() }
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
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! quickbuild() }
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
                allOf {
                    not { environment name: 'NO_CI_TESTING', value: 'true' }
                    // nothing to test if build was skipped
                    expression { ! skip_stage('build') }
                    // or it's a doc-only change
                    expression { ! doc_only_change() }
                    expression { ! skip_stage('test') }
                }
            }
            parallel {
                stage('run_test.sh') {
                    when {
                      beforeAgent true
                      expression { ! skip_stage('run_test') }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        script {
                            if (quickbuild()) {
                                // TODO: these should be gotten from the Requires: of RPMs
                                qb_inst_rpms = " spdk-tools mercury boost-devel"
                            }
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_component_repos + ' ' + component_repos,
                                       inst_rpms: 'gotestsum openmpi3 hwloc-devel argobots ' +
                                                  "fuse3-libs fuse3 " +
                                                  'libisa-l-devel libpmem libpmemobj protobuf-c ' +
                                                  'spdk-devel libfabric-devel pmix numactl-devel ' +
                                                  'libipmctl-devel' + qb_inst_rpms
                        runTest stashes: [ 'CentOS-tests', 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''# JENKINS-52781 tar function is breaking symlinks
                                           rm -rf test_results
                                           mkdir test_results
                                           . ./.build_vars.sh
                                           rm -f ${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control
                                           mkdir -p ${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/
                                           ln -s ../../../../../../../../src/control ${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control
                                           DAOS_BASE=${SL_PREFIX%/install*}
                                           rm -f dnt.*.memcheck.xml test.out
                                           NODE=${NODELIST%%,*}
                                           ssh $SSH_KEY_ARGS jenkins@$NODE "set -x
                                               set -e
                                               sudo bash -c 'echo \"1\" > /proc/sys/kernel/sysrq'
                                               if grep /mnt/daos\\  /proc/mounts; then
                                                   sudo umount /mnt/daos
                                               else
                                                   sudo mkdir -p /mnt/daos
                                               fi
                                               sudo mkdir -p /mnt/daos
                                               sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
                                               sudo mkdir -p $DAOS_BASE
                                               sudo mount -t nfs $HOSTNAME:$PWD $DAOS_BASE
                                               sudo cp $DAOS_BASE/install/bin/daos_admin /usr/bin/daos_admin
                                               sudo chown root /usr/bin/daos_admin
                                               sudo chmod 4755 /usr/bin/daos_admin
                                               /bin/rm $DAOS_BASE/install/bin/daos_admin
                                               sudo ln -sf $SL_PREFIX/share/spdk/scripts/setup.sh /usr/share/spdk/scripts
                                               sudo ln -sf $SL_PREFIX/share/spdk/scripts/common.sh /usr/share/spdk/scripts
                                               sudo ln -s $SL_PREFIX/include  /usr/share/spdk/include
                                               # set CMOCKA envs here
                                               export CMOCKA_MESSAGE_OUTPUT="xml"
                                               export CMOCKA_XML_FILE="$DAOS_BASE/test_results/%g.xml"
                                               cd $DAOS_BASE
                                               IS_CI=true OLD_CI=false utils/run_test.sh
                                               ./utils/node_local_test.py all | tee test.out"''',
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
                                      rm -rf $DAOS_BASE/run_test.sh $DAOS_BASE/vm_test
                                      NODE=${NODELIST%%,*}
                                      ssh $SSH_KEY_ARGS jenkins@$NODE "set -x
                                          cd $DAOS_BASE
                                          mkdir run_test.sh
                                          mkdir vm_test
                                          if ls /tmp/daos*.log > /dev/null; then
                                              mv /tmp/daos*.log run_test.sh/
                                          fi
                                          if ls /tmp/dnt*.log > /dev/null; then
                                              mv /tmp/dnt*.log vm_test/
                                          fi
                                          # servers can sometimes take a while to stop when the test is done
                                          x=0
                                          while [ \"\\\$x\" -lt \"10\" ] &&
                                                pgrep '(orterun|daos_server|daos_io_server)'; do
                                              sleep 1
                                              let x=\\\$x+1
                                          done
                                          if ! sudo umount /mnt/daos; then
                                              echo \"Failed to unmount /mnt/daos\"
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
                            archiveArtifacts artifacts: 'vm_test/**'
                            publishValgrind (
                                    failBuildOnInvalidReports: true,
                                    failBuildOnMissingReports: true,
                                    failThresholdDefinitelyLost: '',
                                    failThresholdInvalidReadWrite: '',
                                    failThresholdTotal: '',
                                    pattern: 'dnt.*.memcheck.xml',
                                    publishResultsForAbortedBuilds: false,
                                    publishResultsForFailedBuilds: true,
                                    sourceSubstitutionPaths: '',
                                    unstableThresholdDefinitelyLost: '0',
                                    unstableThresholdInvalidReadWrite: '0',
                                    unstableThresholdTotal: '0'
                            )
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         failOnError: true,
                                         name: "VM Testing",
                                         tool: clang(pattern: 'test.out',
                                                     name: 'VM test results',
                                                     id: 'VM_test')
                        }
                    }
                }
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                allOf {
                    not { environment name: 'NO_CI_TESTING', value: 'true' }
                    // nothing to test if build was skipped
                    expression { ! skip_stage('build') }
                    // or it's a doc-only change
                    expression { ! doc_only_change() }
                    expression { ! skip_stage('test') }
                }
            }
            parallel {
                stage('Coverity on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skip_stage('coverity-test') }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                                '$BUILDARGS ' +
                                                '--build-arg QUICKBUILD=true' +
                                                ' --build-arg QUICKBUILD_DEPS="' + env.QUICKBUILD_DEPS +
                                                '" --build-arg REPOS="' + component_repos + '"'
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
                        expression { ! skip_stage('func-test') }
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
                                                  ' ' + functional_rpms
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
                                  if daos_logs=$(find install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/* -maxdepth 0 -type f -size +1M); then
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
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-small') }
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
                                                  ' ' + functional_rpms
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
                                  if daos_logs=$(find install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/* -maxdepth 0 -type f -size +1M); then
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
                stage('Functional_Hardware_Medium') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-medium') }
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
                                                  ' ' + functional_rpms
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
                                  if daos_logs=$(find install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/* -maxdepth 0 -type f -size +1M); then
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
                stage('Functional_Hardware_Large') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-large') }
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
                                                  ' ' + functional_rpms
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
                                  if daos_logs=$(find install/lib/daos/TESTING/ftest/avocado/job-results/*/daos_logs/* -maxdepth 0 -type f -size +1M); then
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
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! skip_stage('test-centos-rpms') }
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
                                            "sudo yum -y install daos{,-client}-${daos_packages_version}\n" +
                                            "sudo yum -y history rollback last-1\n" +
                                            "sudo yum -y install daos{,-{server,client}}-${daos_packages_version}\n" +
                                            "sudo yum -y install daos{,-tests}-${daos_packages_version}\n" +
                                            "${rpm_test_daos_test}" + '"',
                                    junit_files: null,
                                    failure_artifacts: env.STAGE_NAME, ignore_failure: true
                        }
                    }
                } // stage('Test CentOS 7 RPMs')
                stage('Scan CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! skip_stage('scan-centos-rpms') }
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
                                       inst_rpms: 'environment-modules ' +
                                                  'clamav clamav-devel'
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            runTest script: rpm_scan_pre +
                                            "sudo yum -y install " +
                                            "daos{,-{client,server,tests}}-" +
                                            "${daos_packages_version}\n" +
                                            rpm_scan_test + '"\n' +
                                            rpm_scan_post,
                                    junit_files: 'maldetect.xml',
                                    failure_artifacts: env.STAGE_NAME, ignore_failure: true
                        }
                    }
                    post {
                        always {
                            junit 'maldetect.xml'
                        }
                    }
                } // stage('Scan CentOS 7 RPMs')
            }
        }
    }
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    }
}
