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

def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? 'H 0 * * *' : '')
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
        timestamps ()
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
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'pylint.log', allowEmptyArchive: true
                        }
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
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild(clean: '_build.external iof.conf')
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: '.build_vars.*'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile(".*\\/_build\\.external\\/.*"),
                                                       excludeFile("_build\\.external\\/.*")]
                            }
                        }
                        success {
                            sh "rm -rf _build.external"
                        }
                    }
                }
                stage('Build master CentOS 7') {
                    when { branch 'master' }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild(clean: '_build.external iof.conf',
                                   scons_args: '--build-config=utils/build-master.config')
                        stash name: 'CentOS-master-install', includes: 'install/**'
                        stash name: 'CentOS-master-build-vars', includes: ".build_vars.*"
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-master-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            archiveArtifacts artifacts: '**/*.log', allowEmptyArchive: true
                        }
                        success {
                            sh "rm -rf _build.external"
                        }
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu:18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu18.04 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild(clean: '_build.external iof.conf')
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile(".*\\/_build\\.external\\/.*"),
                                                       excludeFile("_build\\.external\\/.*")]
                            }
                        }
                        success {
                            sh "rm -rf _build.external"
                        }
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Single node') {
                    agent {
                        label 'ci_vm1'
                    }
                    options {
                        timeout(time: 60, unit: 'MINUTES')
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                           node_count: 1,
                           snapshot: true
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: """set -x
                                    . ./.build_vars.sh
                                    IOF_BASE=\${SL_PREFIX%/install*}
                                    NODELIST=$nodelist
                                    NODE=\${NODELIST%%,*}
                                    trap 'set +e; set -x; ssh -i ci_key jenkins@\$NODE "set -ex; sudo umount \$IOF_BASE"' EXIT
                                    ssh -i ci_key jenkins@\$NODE "set -x
                                        set -e
                                        sudo mkdir -p \$IOF_BASE
                                        sudo mount -t nfs \$HOSTNAME:\$PWD \$IOF_BASE
                                        cd \$IOF_BASE
                                        ln -s /usr/bin/fusermount install/bin/fusermount3
                                        pip3.4 install --user tabulate
                                        export TR_USE_VALGRIND=none
                                        export IOF_TESTLOG=test/output-centos
                                        nosetests-3.4 --xunit-testsuite-name=centos --xunit-file=nosetests-centos.xml --exe --with-xunit"
                                    exit 0
                                    """,
                                junit_files: 'nosetests-centos.xml'
                    }
                    post {
                        always {
                            junit 'nosetests-centos.xml'
                            archiveArtifacts artifacts: '**/*.log'
                        }
                        cleanup {
                            dir('test/output-centos') {
                                deleteDir()
                            }
                        }
                    }
                }
                stage('Single node cart-master') {
                    when { branch 'master' }
                    agent {
                        label 'ci_vm1'
                    }
                    options {
                        timeout(time: 60, unit: 'MINUTES')
                    }
                    /* To run a single test use this command:
                     * nosetests-3.4 --with-xunit --xunit-testsuite-name=master --xunit-file=nosetests-master.xml test/iof_test_local.py:Testlocal.test_use_ino"
                     */
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                           node_count: 1,
                           snapshot: true
                        runTest stashes: [ 'CentOS-master-install', 'CentOS-master-build-vars' ],
                                script: """set -x
                                    . ./.build_vars.sh
                                    IOF_BASE=\${SL_PREFIX%/install*}
                                    NODELIST=$nodelist
                                    NODE=\${NODELIST%%,*}
                                    trap 'set +e; set -x; ssh -i ci_key jenkins@\$NODE "set -ex; sudo umount \$IOF_BASE"' EXIT
                                    ssh -i ci_key jenkins@\$NODE "set -x
                                        set -e
                                        sudo mkdir -p \$IOF_BASE
                                        sudo mount -t nfs \$HOSTNAME:\$PWD \$IOF_BASE
                                        cd \$IOF_BASE
                                        ln -s /usr/bin/fusermount install/bin/fusermount3
                                        pip3.4 install --user tabulate
                                        export TR_USE_VALGRIND=none
                                        export IOF_TESTLOG=test/output-master
                                        nosetests-3.4 --with-xunit --xunit-testsuite-name=master --xunit-file=nosetests-master.xml test/iof_test_local.py:Testlocal.test_use_ino"
                                    exit 0
                                    """,
                                junit_files: 'nosetests-master.xml'
                    }
                    post {
                        always {
                            junit 'nosetests-master.xml'
                            archiveArtifacts artifacts: '**/*.log'
                        }
                        cleanup {
                            dir('test/output-master') {
                                deleteDir()
                            }
                        }
                    }
                }
                stage('Single node valgrind') {
                    agent {
                        label 'ci_vm1'
                    }
                    options {
                        timeout(time: 60, unit: 'MINUTES')
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                           node_count: 1,
                           snapshot: true
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: """set -x
                                    . ./.build_vars.sh
                                    IOF_BASE=\${SL_PREFIX%/install*}
                                    NODELIST=$nodelist
                                    NODE=\${NODELIST%%,*}
                                    trap 'set +e; set -x; ssh -i ci_key jenkins@\$NODE "set -ex; sudo umount \$IOF_BASE"' EXIT
                                    ssh -i ci_key jenkins@\$NODE "set -x
                                        set -e
                                        sudo mkdir -p \$IOF_BASE
                                        sudo mount -t nfs \$HOSTNAME:\$PWD \$IOF_BASE
                                        cd \$IOF_BASE
                                        ln -s /usr/bin/fusermount install/bin/fusermount3
                                        pip3.4 install --user tabulate
                                        export TR_USE_VALGRIND=memcheck
                                        export IOF_TESTLOG=test/output-memcheck
                                        nosetests-3.4 --xunit-testsuite-name=valgrind --xunit-file=nosetests-valgrind.xml --exe --with-xunit"
                                    exit 0
                                    """,
                        junit_files: 'nosetests-valgrind.xml'
                    }
                    post {
                        always {
                            junit 'nosetests-valgrind.xml'
                            archiveArtifacts artifacts: '**/*.log,**/*.memcheck'
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
                        }
                        cleanup {
                            dir('test/output-memcheck') {
                                deleteDir()
                            }
                        }
                    }
                }
             stage('Fault injection') {
                agent {
                    label 'ci_vm1'
                }
                options {
                    timeout(time: 60, unit: 'MINUTES')
                }
                steps {
                    provisionNodes NODELIST: env.NODELIST,
                        node_count: 1,
                        snapshot: true
                    runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                        script: """set -x
                            . ./.build_vars.sh
                            IOF_BASE=\${SL_PREFIX%/install*}
                            NODELIST=$nodelist
                            NODE=\${NODELIST%%,*}
                            trap 'set +e; set -x; ssh -i ci_key jenkins@\$NODE "set -ex; sudo umount \$IOF_BASE"' EXIT
                            ssh -i ci_key jenkins@\$NODE "set -x
                                set -e
                                sudo mkdir -p \$IOF_BASE
                                sudo mount -t nfs \$HOSTNAME:\$PWD \$IOF_BASE
                                cd \$IOF_BASE
                                ln -s /usr/bin/fusermount install/bin/fusermount3
                                pip3.4 install --user tabulate
                                ./test/iof_test_alloc_fail.py"
                            """
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: '**/*.log,**/*.memcheck'
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
                        }
                        cleanup {
                            dir('test/output') {
                                deleteDir()
                            }
                        }
                    }
                }
            }
        }
    }
}
