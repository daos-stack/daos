#!/usr/bin/groovy
/* groovylint-disable-next-line LineLength */
/* groovylint-disable DuplicateMapLiteral, DuplicateNumberLiteral */
/* groovylint-disable DuplicateStringLiteral, NestedBlockDepth, VariableName */
/* Copyright 2019-2023 Intel Corporation
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
//@Library(value='pipeline-lib@your_branch') _

/* groovylint-disable-next-line CompileStatic */
job_status_internal = [:]

// groovylint-disable-next-line MethodParameterTypeRequired, NoDef
void job_status_update(String name=env.STAGE_NAME, def value=currentBuild.currentResult) {
    jobStatusUpdate(job_status_internal, name, value)
}

// groovylint-disable-next-line MethodParameterTypeRequired, NoDef
void job_step_update(def value=currentBuild.currentResult) {
    // job_status_update(env.STAGE_NAME, value)
    jobStatusUpdate(job_status_internal, env.STAGE_NAME, value)
}

// For master, this is just some wildly high number
// For release branches, its the subsequent DAOS version
next_version = '2.7.0'

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith('daily-testing') &&
     !env.BRANCH_NAME.startsWith('weekly-testing') &&
     !env.BRANCH_NAME.startsWith('release/') &&
     !env.BRANCH_NAME.startsWith('ci-') &&
     env.BRANCH_NAME != 'master')) {
    currentBuild.result = 'SUCCESS'
    return
}

// The docker agent setup and the provisionNodes step need to know the
// UID that the build agent is running under.
cached_uid = 0
Integer getuid() {
    if (cached_uid == 0) {
        cached_uid = sh(label: 'getuid()',
                        script: 'id -u',
                        returnStdout: true).trim()
    }
    return cached_uid
}

String vm9_label(String distro) {
    return cachedCommitPragma(
        pragma: distro + '-VM9-label',
        def_val: cachedCommitPragma(pragma: 'VM9-label', def_val: params.FUNCTIONAL_VM_LABEL))
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        /* groovylint-disable-next-line AddEmptyString */
        cron(env.BRANCH_NAME == 'weekly-2.6-testing' ? 'TZ=UTC\n0 6 * * 6' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = '-ici_key'
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
        COVFN_DISABLED = cachedCommitPragma(pragma: 'Skip-fnbullseye', def_val: 'true')
        REPO_FILE_URL = repoFileUrl(env.REPO_FILE_URL)
        SCONS_FAULTS_ARGS = sconsFaultsArgs()
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        ansiColor('xterm')
        buildDiscarder(logRotator(artifactDaysToKeepStr: '100', daysToKeepStr: '730'))
    }

    parameters {
        string(name: 'BuildPriority',
               /* groovylint-disable-next-line UnnecessaryGetter */
               defaultValue: getPriority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
        string(name: 'TestTag',
               defaultValue: '',
               description: 'Test-tag to use for this run (i.e. pr, daily_regression, full_regression, etc.)')
        // The TestNvme and TestRepeat parameter definitions are purposely excluded. The functional
        // test stage launch.py --nvme argument is hard-coded in each stage definition to avoid the
        // stages from duplicating testing.
        string(name: 'TestProvider',
               defaultValue: '',
               description: 'Test-provider to use for the non-Provider Functional Hardware test ' +
                            'stages.  Specifies the default provider to use the daos_server ' +
                            'config file when running functional tests (the launch.py ' +
                            '--provider argument; i.e. "ucx+dc_x", "ofi+verbs", "ofi+tcp")')
        string(name: 'CI_RPM_TEST_VERSION',
               defaultValue: '',
               description: 'Package version to use instead of latest. example: 1.3.103-1, 1.2-2')
        string(name: 'BaseBranch',
               defaultValue: 'release/2.6',
               description: 'The base branch to run daily-testing against (i.e. master, or a PR\'s branch)')
        // TODO: add parameter support for per-distro CI_PR_REPOS
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
        string(name: 'CI_EL9_TARGET',
               defaultValue: '',
               description: 'Image to used for EL 9 CI tests.  I.e. el9, el9.1, etc.')
        string(name: 'CI_LEAP15_TARGET',
               defaultValue: '',
               description: 'Image to use for OpenSUSE Leap CI tests.  I.e. leap15, leap15.2, etc.')
        string(name: 'CI_UBUNTU20.04_TARGET',
               defaultValue: '',
               description: 'Image to used for Ubuntu 20 CI tests.  I.e. ubuntu20.04, etc.')
        booleanParam(name: 'CI_FUNCTIONAL_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 8 test stage')
        booleanParam(name: 'CI_FUNCTIONAL_el9_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 9 test stage')
        booleanParam(name: 'CI_FUNCTIONAL_leap15_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on Leap 15 test stage')
        booleanParam(name: 'CI_FUNCTIONAL_ubuntu20_TEST',
                     defaultValue: false,
                     description: 'Run the Functional on Ubuntu 20.04 test stage' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_medium_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium test stage')
        booleanParam(name: 'CI_medium_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium MD on SSD test stage')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large test stage')
        booleanParam(name: 'CI_large_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large MD on SSD test stage')
        string(name: 'FUNCTIONAL_VM_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large tests')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for the Functional Hardware Large MD on SSD stage')
        string(name: 'CI_STORAGE_PREP_LABEL',
               defaultValue: '',
               description: 'Label for cluster to do a DAOS Storage Preparation')
        string(name: 'CI_PROVISIONING_POOL',
               defaultValue: '',
               description: 'The pool of images to provision test nodes from')
        string(name: 'CI_BUILD_DESCRIPTION',
               defaultValue: '',
               description: 'A description of the build')
    }

    stages {
        stage('Set Description') {
            steps {
                script {
                    if (params.CI_BUILD_DESCRIPTION) {
                        buildDescription params.CI_BUILD_DESCRIPTION
                    }
                }
            }
        }
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
        stage('Check PR') {
            when { changeRequest() }
            parallel {
                stage('Branch name check') {
                    when { changeRequest() }
                    steps {
                        script {
                            if (env.CHANGE_ID.toInteger() > 9742 && !env.CHANGE_BRANCH.contains('/')) {
                                error('Your PR branch name does not follow the rules. Please rename it ' +
                                      'according to the rules described here: ' +
                                      'https://daosio.atlassian.net/l/cp/UP1sPTvc#branch_names.  ' +
                                      'Once you have renamed your branch locally to match the ' +
                                      'format, close this PR and open a new one using the newly renamed ' +
                                      'local branch.')
                            }
                        }
                    }
                }
            } // parallel
        } // stage('Check PR')
        stage('Cancel Previous Builds') {
            when {
                beforeAgent true
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
            steps {
                script {
                    // Note: The functional test steps define 'nvme' instead of 'default_nvme' to
                    // force the launch.py --nvme argument.  This means the 'Test-nvme' commit
                    // pragmas will be ignored. This is to avoid multiple parallel test stages
                    // from duplicating testing.
                    parallel(
                        'Functional on EL 8': getFunctionalTestStage(
                            name: 'Functional on EL 8',
                            pragma_suffix: '-vm',
                            distro: 'el8',
                            base_branch: params.BaseBranch,
                            label: vm9_label('EL8'),
                            next_version: next_version,
                            stage_tags: 'vm',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: true,
                            job_status: job_status_internal
                        ),
                        'Functional on EL 9': getFunctionalTestStage(
                            name: 'Functional on EL 9',
                            pragma_suffix: '-vm',
                            distro: 'el9',
                            base_branch: params.BaseBranch,
                            label: vm9_label('EL9'),
                            next_version: next_version,
                            stage_tags: 'vm',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional on Leap 15.5': getFunctionalTestStage(
                            name: 'Functional on Leap 15.5',
                            pragma_suffix: '-vm',
                            distro: 'leap15',
                            base_branch: params.BaseBranch,
                            label: vm9_label('Leap15'),
                            next_version: next_version,
                            stage_tags: 'vm',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional on Ubuntu 20.04': getFunctionalTestStage(
                            name: 'Functional on Ubuntu 20.04',
                            pragma_suffix: '-vm',
                            distro: 'ubuntu20',
                            base_branch: params.BaseBranch,
                            label: vm9_label('Ubuntu'),
                            next_version: next_version,
                            stage_tags: 'vm',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: false,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium': getFunctionalTestStage(
                            name: 'Functional Hardware Medium',
                            pragma_suffix: '-hw-medium',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,-provider',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium MD on SSD',
                            pragma_suffix: '-hw-medium-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_MD_ON_SSD_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,-provider',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto_md_on_ssd',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large': getFunctionalTestStage(
                            name: 'Functional Hardware Large',
                            pragma_suffix: '-hw-large',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,large',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large MD on SSD',
                            pragma_suffix: '-hw-large-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_MD_ON_SSD_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,large',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: isPr() ? 'always_passes' : 'full_regression',
                            nvme: 'auto_md_on_ssd',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                    )
                }
            }
        } // stage('Test')
    } //stages
    post {
        always {
            job_status_update('final_status')
            jobStatusWrite(job_status_internal)
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
