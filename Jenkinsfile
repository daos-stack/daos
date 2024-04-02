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

// Should try to figure this out automatically
/* groovylint-disable-next-line CompileStatic, VariableName */
String base_branch = 'release/2.4'
// For master, this is just some wildly high number
next_version = '2.5'

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
/* groovylint-disable-next-line UnusedVariable, VariableName */
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!(env.BRANCH_NAME =~ branchTypeRE('testing')) &&
     !(env.BRANCH_NAME =~ branchTypeRE('release')) &&
     env.BRANCH_NAME != 'master')) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    // Weekly timed builds disabled on 2-Apr-2024
    // triggers {
    //     /* groovylint-disable-next-line AddEmptyString */
    //     cron(env.BRANCH_NAME == 'weekly-2.4-testing' ? 'TZ=UTC\n0 0 * * 6' : '')
    // }

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
               description: 'Test-tag to use for this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'TestNvme',
               defaultValue: '',
               description: 'The launch.py --nvme argument to use for the Functional test ' +
                            'stages of this run (i.e. auto, auto_md_on_ssd, auto:-3DNAND, ' +
                            '0000:81:00.0, etc.)')
        string(name: 'TestRepeat',
               defaultValue: '',
               description: 'Test-repeat to use for this run.  Specifies the number of times to ' +
                            'repeat each functional test. CAUTION: only use in combination with ' +
                            'a reduced number of tests specified with the TestTag parameter.')
        string(name: 'TestProvider',
               defaultValue: '',
               description: 'Test-provider to use for this run.  Specifies the default provider ' +
                            'to use the daos_server config file when running functional tests' +
                            '(the launch.py --provider argument;  i.e. "ucx+dc_x", "ofi+verbs", '+
                            '"ofi+tcp")')
        string(name: 'BaseBranch',
               defaultValue: base_branch,
               description: 'The base branch to run against (i.e. master, release/x.y or a PR\'s branch)')
        string(name: 'CI_RPM_TEST_VERSION',
               defaultValue: '',
               description: 'Package version to use instead of building. example: 1.3.103-1, 1.2-2')
        string(name: 'CI_PR_REPOS',
               defaultValue: '',
               description: 'Additional repository used for locating packages for the build and ' +
                            'test nodes, in the project@PR-number[:build] format.')
        string(name: 'CI_HARDWARE_DISTRO',
               defaultValue: '',
               description: 'Distribution to use for CI Hardware Tests')
        string(name: 'CI_EL8_TARGET',
               defaultValue: '',
               description: 'Image to used for EL 8 CI tests.  I.e. el8, el8.3, etc.')
        string(name: 'CI_LEAP15_TARGET',
               defaultValue: '',
               description: 'Image to use for OpenSUSE Leap CI tests.  I.e. leap15, leap15.2, etc.')
        string(name: 'CI_UBUNTU20.04_TARGET',
               defaultValue: '',
               description: 'Image to used for Ubuntu 20 CI tests.  I.e. ubuntu20.04, etc.')
        booleanParam(name: 'CI_FUNCTIONAL_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 8 test stage')
        booleanParam(name: 'CI_FUNCTIONAL_leap15_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on Leap 15 test stage' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_FUNCTIONAL_ubuntu20_TEST',
                     defaultValue: false,
                     description: 'Run the Functional on Ubuntu 20.04 test stage' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_medium_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium test stage')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large test stage')
        string(name: 'FUNCTIONAL_VM_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large tests')
        string(name: 'CI_BUILD_DESCRIPTION',
               defaultValue: '',
               description: 'A description of the build')
    }

    stages {
        stage('Get Commit Message') {
            steps {
                script {
                    env.COMMIT_MESSAGE = sh(script: 'git show -s --format=%B',
                                            returnStdout: true).trim()
                    Map pragmas = [:]
                    // can't use eachLine() here: https://issues.jenkins.io/browse/JENKINS-46988/
                    env.COMMIT_MESSAGE.split('\n').each { line ->
                        String key, value
                        try {
                            (key, value) = line.split(':', 2)
                            if (key.contains(' ')) {
                                return
                            }
                            pragmas[key.toLowerCase()] = value
                        /* groovylint-disable-next-line CatchArrayIndexOutOfBoundsException */
                        } catch (ArrayIndexOutOfBoundsException ignored) {
                            // ignore and move on to the next line
                        }
                    }
                    env.pragmas = pragmas
                }
            }
        }
        stage('Cancel Previous Builds') {
            when {
                expression { !skipStage() }
            }
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
                 stage('Functional on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'EL8-VM9-label', def_val: params.FUNCTIONAL_VM_LABEL)
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        echo 'Checking out ' + (env.BaseBranch ?: base_branch) + ' (base_branch is ' + base_branch + ')'
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch ?: base_branch,
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
                } // stage('Functional on EL 8')
                stage('Functional on Leap 15.5') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'Leap15-VM9-label', def_val: params.FUNCTIONAL_VM_LABEL)
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        echo 'Checking out ' + (env.BaseBranch ?: base_branch) + ' (base_branch is ' + base_branch + ')'
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch ?: base_branch,
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
                } // stage('Functional on Leap 15.4')
                stage('Functional on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'Ubuntu-VM9-label', def_val: params.FUNCTIONAL_VM_LABEL)
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        echo 'Checking out ' + (env.BaseBranch ?: base_branch) + ' (base_branch is ' + base_branch + ')'
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch ?: base_branch,
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
                stage('Functional Hardware Medium') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        echo 'Checking out ' + (env.BaseBranch ?: base_branch) + ' (base_branch is ' + base_branch + ')'
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch ?: base_branch,
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
                        label params.FUNCTIONAL_HARDWARE_LARGE_LABEL
                    }
                    steps {
                        // Need to get back onto base_branch for ci/
                        echo 'Checking out ' + (env.BaseBranch ?: base_branch) + ' (base_branch is ' + base_branch + ')'
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: env.BaseBranch ?: base_branch,
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
            } // parallel
        } // stage('Test')
    } //stages
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
