pipeline {
    agent none

    environment {
        SHELL = '/bin/bash'
    }

    triggers {
        // Jenkins instances behind firewalls can't get webhooks
        pollSCM('* * * * *')
        // See if cron works since pollSCM above isn't
        // cron('H 22 * * *')
    }

    stages {
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
                        checkout scm
                        sh '''git submodule update --init --recursive
                              scons -c
                              # scons -c is not perfect so get out the big hammer
                              rm -rf _build.external install build
                              pushd scons_local
                              git fetch https://review.hpdd.intel.com/coral/scons_local refs/changes/13/33013/4
                              git fetch https://review.hpdd.intel.com/coral/scons_local refs/changes/22/33022/2
                              git fetch https://review.hpdd.intel.com/coral/scons_local refs/changes/43/33043/4
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
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: '.build_vars.*'
                    }
                }
                stage('Build on Ubuntu 16.04') {
                    agent {
                        label 'docker_runner'
                    }
                    steps {
                        echo "Building on Ubuntu is broken for the moment"
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
