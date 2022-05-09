#!/usr/bin/groovy
/* Copyright (C) 2019-2021 Intel Corporation
 * All rights reserved.
 *
 * This file is part of the DAOS Project. It is subject to the license terms
 * in the LICENSE file found in the top-level directory of this distribution
 * and at https://img.shields.io/badge/License-BSD--2--Clause--Patent-blue.svg.
 * No part of the DAOS Project, including this file, may be copied, modified,
 * propagated, or distributed except according to the terms contained in the
 * LICENSE file.
 */

// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value="pipeline-lib@your_branch") _

// Should try to figure this out automatically
String base_branch = "release/2.0"
// For master, this is just some wildly high number
next_version = "2.1.0"

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

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

    /* DAOS-10336: disable timed weekly runs for release/2.0
    triggers {
        cron(env.BRANCH_NAME.startsWith('weekly-testing') ? 'H 0 * * 6' : '')
    }
    */

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
        COVFN_DISABLED = cachedCommitPragma(pragma: 'Skip-fnbullseye', def_val: 'true')
        SCONS_FAULTS_ARGS = sconsFaultsArgs()
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        ansiColor('xterm')
    }

    parameters {
        string(name: 'BuildPriority',
               defaultValue: getPriority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
        string(name: 'TestTag',
               defaultValue: "full_regression",
               description: 'Test-tag to use for this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'CI_FUNCTIONAL_VM9_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'CI_NVME_3_LABEL',
               defaultValue: 'ci_nvme3',
               description: 'Label to use for 3 node NVMe tests')
        string(name: 'CI_NVME_5_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 5 node NVMe tests')
        string(name: 'CI_NVME_9_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node NVMe tests')
    }

    stages {
        stage('Get Commit Message') {
            steps {
                script {
                    env.COMMIT_MESSAGE = sh(script: 'git show -s --format=%B',
                                            returnStdout: true).trim()
                }
            }
        }
        stage('Cancel Previous Builds') {
            when { changeRequest() }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                expression { ! skipStage() }
            }
            parallel {
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on CentOS 7')
                stage('Functional on EL 8') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on EL 8')
                stage('Functional on Leap 15') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    } // post
                } // stage('Functional on Leap 15')
                stage('Functional on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    } // post
                } // stage('Functional on Ubuntu 20.04')
                stage('Functional Hardware Small') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label params.CI_NVME_3_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional_Hardware_Small')
                stage('Functional Hardware Medium') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.CI_NVME_5_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional_Hardware_Medium')
                stage('Functional Hardware Large') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label params.CI_NVME_9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: base_branch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version,
                                                                     "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional_Hardware_Large')
            } // parallel
        } // stage('Test')
    } //stages
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
