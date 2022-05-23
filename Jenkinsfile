#!/usr/bin/groovy
/* groovylint-disable DuplicateStringLiteral, NestedBlockDepth */
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
@Library(value="pipeline-lib@DAOS-9989") _

// Should try to figure this out automatically
/* groovylint-disable-next-line CompileStatic, VariableName */
String base_branch = 'master'
// For master, this is just some wildly high number
next_version = '1000'

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
/* groovylint-disable-next-line UnusedVariable, VariableName */
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith('weekly-testing') &&
     !env.BRANCH_NAME.startsWith('release/') &&
     env.BRANCH_NAME != 'master')) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        /* groovylint-disable-next-line AddEmptyString */
        cron(env.BRANCH_NAME == 'master' ? 'TZ=America/Toronto\n0 0 * * *\n' : '' +
             /* groovylint-disable-next-line AddEmptyString */
             env.BRANCH_NAME == 'release/1.2' ? 'TZ=America/Toronto\n0 12 * * *\n' : '' +
             env.BRANCH_NAME.startsWith('weekly-testing') ? 'H 0 * * 6' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = '-ici_key'
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
               /* groovylint-disable-next-line UnnecessaryGetter */
               defaultValue: getPriority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
        string(name: 'TestTag',
               defaultValue: 'full_regression',
               description: 'Test-tag to use for the weekly Functional Hardware Small/Medium/Large stages of this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'TestTagTCP',
               defaultValue: 'pr daily_regression',
               description: 'Test-tag to use for the Functional Hardware Small/Medium/Large TCP stages of this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'TestTagUCX',
               defaultValue: 'pr daily_regression',
               description: 'Test-tag to use for the Functional Hardware Small/Medium/Large UCX stages of this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'BaseBranch',
               defaultValue: base_branch,
               description: 'The base branch to run weekly-testing against (i.e. master, or a PR\'s branch)')
        booleanParam(name: 'CI_small_TEST',
                     defaultValue: true,
                     description: 'Run the Small Cluster CI tests')
        booleanParam(name: 'CI_medium_TEST',
                     defaultValue: true,
                     description: 'Run the Medium Cluster CI tests')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Large Cluster CI tests')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_SMALL_TCP',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Small TCP test stage')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_MEDIUM_TCP',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Medium TCP test stage')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_LARGE_TCP',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Large TCP test stage')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_SMALL_UCX',
                     defaultValue: false,
                     description: 'Run the CI Functional Hardware Small UCX test stage')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_MEDIUM_UCX',
                     defaultValue: false,
                     description: 'Run the CI Functional Hardware Medium UCX test stage')
        booleanParam(name: 'CI_FUNCTIONAL_HARDWARE_LARGE_UCX',
                     defaultValue: false,
                     description: 'Run the CI Functional Hardware Large UCX test stage')
        string(name: 'CI_FUNCTIONAL_VM9_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'CI_NVME_3_LABEL',
               defaultValue: 'ci_nvme3',
               description: 'Label to use for 3 node Functional Hardware Small stage')
        string(name: 'CI_NVME_5_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium stage')
        string(name: 'CI_NVME_9_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large stage')
        string(name: 'CI_HW_SMALL_TCP_LABEL',
               defaultValue: 'ci_nvme3',
               description: 'Label to use for 3 node Functional Hardware Small TCP stage')
        string(name: 'CI_HW_MEDIUM_TCP_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium TCP stage')
        string(name: 'CI_HW_LARGE_TCP_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large TCP stage')
        string(name: 'CI_HW_SMALL_UCX_LABEL',
               defaultValue: 'ci_nvme3',
               description: 'Label to use for 3 node Functional Hardware Small UCX stage')
        string(name: 'CI_HW_MEDIUM_UCX_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium UCX stage')
        string(name: 'CI_HW_LARGE_UCX_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large UCX stage')
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
                expression { !skipStage() }
            }
            parallel {
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on CentOS 7')
                stage('Functional on CentOS 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on CentOS 8')
                stage('Functional on Leap 15') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
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
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
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
                        expression { !skipStage() }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label params.CI_NVME_3_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
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
                        expression { !skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.CI_NVME_5_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
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
                        expression { !skipStage() }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label params.CI_NVME_9_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional_Hardware_Large')
                stage('Functional Hardware Small TCP') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label params.CI_HW_SMALL_TCP_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_tag: params.TestTagTCP,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ofi+tcp',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Small TCP')
                stage('Functional Hardware Medium TCP') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.CI_HW_MEDIUM_TCP_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_tag: params.TestTagTCP,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ofi+tcp',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Medium TCP')
                stage('Functional Hardware Large TCP') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label params.CI_HW_LARGE_TCP_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal"),
                                       test_tag: params.TestTagTCP,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ofi+tcp',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Large TCP')
                stage('Functional Hardware Small UCX') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label params.CI_HW_SMALL_UCX_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal") + "mercury-ucx",
                                       test_tag: params.TestTagUCX,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ucx+dc_x',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Small UCX')
                stage('Functional Hardware Medium UCX') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.CI_HW_MEDIUM_UCX_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal") + "mercury-ucx",
                                       test_tag: params.TestTagUCX,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ucx+dc_x',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Medium UCX')
                stage('Functional Hardware Large UCX') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label params.CI_HW_LARGE_UCX_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch,
                                    withSubmodules: true
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "tests-internal") + "mercury-ucx",
                                       test_tag: params.TestTagUCX,
                                       ftest_arg: '--nvme=auto:-3DNAND --provider=ucx+dc_x',
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional Hardware Large UCX')
            } // parallel
        } // stage('Test')
    } //stages
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
