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

def daos_branch = env.CHANGE_BRANCH ? env.CHANGE_BRANCH : env.GIT_BRANCH
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
    (env.BRANCH_NAME != "weekly-testing" &&
     env.BRANCH_NAME != daos_branch)) {
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
        stage('Play') {
            agent {
                dockerfile {
                    filename 'Dockerfile.centos.7'
                    dir 'utils/docker'
                    label 'docker_runner'
                    additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                }
            }
            steps {
                emailext subject: "environment",
                         to: 'brian.murrell@intel.com',
                         body: sh(script: 'env | sort', returnStdout: true)
                //sh label: "Send environment",
                //   script: 'env | sort | mail -s env brian.murrell@intel.com'
                sh label: "Playground",
                   script: '''if [ -z "${env.CHANGE_ID}" ]; then
                                  mb_modifier="^"
                              fi
                              git merge-base origin/''' + daos_branch + '''$mb_modifier HEAD
                              git diff-tree --no-commit-id --name-only                       \
                                $(git merge-base origin/''' + daos_branch + '''$mb_modifier HEAD) HEAD | \
                                grep -v -e "^doc$"
                              git log --graph --pretty=format:'%h -%d %s (%cr) <%an>' --abbrev-commit | head'''
            }
        }
    }
    post {
        unsuccessful {
            notifyBrokenBranch branches: daos_branch
        }
    }
}
