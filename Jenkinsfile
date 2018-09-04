pipeline {
    agent none

    environment {
        SHELL = '/bin/bash'
    }

    // triggers {
        // Jenkins instances behind firewalls can't get webhooks
        // sadly, this doesn't seem to work
        // pollSCM('* * * * *')
        // See if cron works since pollSCM above isn't
        // cron('H 22 * * *')
    // }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
    }

    stages {
        stage('Pre-build') {
            parallel {
                stage('check_modules.sh') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u)'
                        }
                    }
                    steps {
                        githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'PENDING'
                        sh '''pushd scons_local
                              git fetch https://review.hpdd.intel.com/coral/scons_local refs/changes/13/33013/9
                              popd
                              git submodule update --init --recursive
                              utils/check_modules.sh'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'pylint.log', allowEmptyArchive: true
                        }
                    }
                    post {
                        success {
                            githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'SUCCESS'
                        }
                    }
                    post {
                        unstable {
                            githubNotify description: 'checkmodules.sh',  context: 'checkmodules.sh', status: 'FAILURE'
                        }
                    }
                }
            }
        }
        stage('Build') {
            parallel {
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos:7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u)'
                        }
                    }
                    steps {
                        githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'PENDING'
                        checkout scm
                        /* don't need to keep rebuilding this while testing the pipeline
                        sh '''git submodule update --init --recursive
                              scons -c
                              # scons -c is not perfect so get out the big hammer
                              rm -rf _build.external install build
                              pushd scons_local
                              git fetch https://review.hpdd.intel.com/coral/scons_local refs/changes/13/33013/5
                              popd
                              utils/fetch_go_packages.sh -i .
                              SCONS_ARGS="--update-prereq=all --build-deps=yes USE_INSTALLED=all install"
                              if ! scons $SCONS_ARGS; then
                                  if ! scons --config=force $SCONS_ARGS; then
                                      rc=\${PIPESTATUS[0]}
                                      cat config.log || true
                                      exit \$rc
                                  fi
                              fi'''
                        */
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: '.build_vars.*'
                        stash name: 'CentOS-tests', includes: 'build/src/rdb/raft/src/tests_main, build/src/common/tests/btree_direct, build/src/common/tests/btree, src/common/tests/btree.sh, build/src/common/tests/sched, build/src/client/api/tests/eq_tests, src/vos/tests/evt_ctl.sh, build/src/vos/vea/tests/vea_ut, src/rdb/raft_tests/raft_tests.py'
                    }
                    post {
                        success {
                            githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'SUCCESS'
                        }
                    }
                    post {
                        unstable {
                            githubNotify description: 'CentOS 7 Build',  context: 'build/centos7', status: 'FAILURE'
                        }
                    }
                }
                stage('Build on Ubuntu 16.04') {
                    agent {
                        label 'docker_runner'
                    }
                    steps {
                        githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'PENDING'
                        echo "Building on Ubuntu is broken for the moment"
                    }
                    post {
                        success {
                            githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'SUCCESS'
                        }
                    }
                    post {
                        unstable {
                            githubNotify description: 'Ubuntu 18 Build',  context: 'build/ubuntu18', status: 'FAILURE'
                        }
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Functional quick') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        unstash 'CentOS-build-vars'
                        sh '''bash ftest.sh quick
                              mv install/tmp/daos.log daos-Functional-quick.log'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'daos-Functional-quick.log, src/tests/ftest/avocado/job-results/**'
                            junit 'src/tests/ftest/avocado/job-results/*/results.xml'
                        }
                    }
                }
                stage('run_test.sh') {
                    agent {
                        label 'single'
                    }
                    steps {
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-tests'
                        unstash 'CentOS-install'
                        unstash 'CentOS-build-vars'
                        sh '''HOSTPREFIX=wolf-53 bash -x utils/run_test.sh --init
                              mv /tmp/daos.log daos-run_test.sh.log'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'daos-run_test.sh.log'
                        }
                    }
                }
                stage('DaosTestMulti All') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'mv daos{,-DaosTestMulti-All}.log
                                    [ -f results.xml ] && mv -f results{,-DaosTestMulti-All}.xml' EXIT
                              bash DaosTestMulti.sh || true'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'daos-DaosTestMulti-All.log, results-DaosTestMulti-All.xml'
                            junit allowEmptyResults: true, testResults: 'results-DaosTestMulti-All.xml'
                        }
                    }
                }
                stage('DaosTestMulti Degraded') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'mv daos{,-DaosTestMulti-Degraded}.log
                                    [ -f results.xml ] && mv -f results{,-DaosTestMulti-Degraded}.xml' EXIT
                              bash DaosTestMulti.sh -d || true'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'daos-DaosTestMulti-Degraded.log, results-DaosTestMulti-Degraded.xml'
                            junit allowEmptyResults: true, testResults: 'results-DaosTestMulti-Degraded.xml'
                        }
                    }
                }
                stage('DaosTestMulti Rebuild') {
                    agent {
                        label 'cluster_provisioner'
                    }
                    steps {
                        dir('install') {
                            deleteDir()
                        }
                        unstash 'CentOS-install'
                        sh '''trap 'mv daos{,-DaosTestMulti-Rebuild}.log
                                    [ -f results.xml ] && mv -f results{,-DaosTestMulti-Rebuild}.xml' EXIT
                              bash DaosTestMulti.sh -r || true'''
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'daos-DaosTestMulti-Rebuild.log, results-DaosTestMulti-Rebuild.xml'
                            junit allowEmptyResults: true, testResults: 'results-DaosTestMulti-Rebuild.xml'
                        }
                    }
                }
            }
        }
    }
}
