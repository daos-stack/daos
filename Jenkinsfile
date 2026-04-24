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

// Name of branch to be tested
base_branch = 'master'

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

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
/* groovylint-disable-next-line UnusedVariable, VariableName */
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith('provider-testing') &&
     !env.BRANCH_NAME.startsWith('weekly-testing') &&
     !env.BRANCH_NAME.startsWith('release/') &&
     env.BRANCH_NAME != 'master')) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        /* groovylint-disable-next-line AddEmptyString */
        cron(env.BRANCH_NAME == 'provider-testing' ? 'TZ=UTC\n0 0 * * 0' : '')
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
               defaultValue: 'pr daily_regression',
               description: 'Test-tag to use for the Functional Hardware Small/Medium/Large stages of this run (i.e. pr, daily_regression, full_regression, etc.)')
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
        string(name: 'BaseBranch',
               defaultValue: base_branch,
               description: 'The base branch to run testing against (i.e. master, or a PR\'s branch)')
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
        booleanParam(name: 'CI_medium_tcp_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Medium TCP MD on SSD test stages')
        booleanParam(name: 'CI_medium_tcp_provider_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Medium TCP Provider test stage')
        booleanParam(name: 'CI_large_tcp_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Large TCP MD on SSD test stages')
        booleanParam(name: 'CI_medium_verbs_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Medium Verbs MD on SSD test stages')
        booleanParam(name: 'CI_large_verbs_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the CI Functional Hardware Large Verbs MD on SSD test stages')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_TCP_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium TCP MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_TCP_PROVIDER_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium TCP Provider MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_TCP_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large TCP MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_MD_ON_SSD_LABEL',
               defaultValue: 'ci_ofed5',
               description: 'Label to use for 5 node Functional Hardware Medium Verbs MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_VERBS_MD_ON_SSD_LABEL',
               defaultValue: 'ci_ofed9',
               description: 'Label to use for 9 node Functional Hardware Large Verbs MD on SSD stage')
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
        stage('Prepare Environment Variables') {
            // TODO: Could/should these be moved to the environment block?
            parallel {
                stage('Get Commit Message') {
                    steps {
                        pragmasToEnv()
                    }
                }
                stage('Determine Release Branch') {
                    steps {
                        script {
                            env.RELEASE_BRANCH = releaseBranch()
                            echo 'Release branch == ' + env.RELEASE_BRANCH
                        }
                    }
                }
            }
        }
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
                    parallel(
                        'Functional Hardware Medium TCP MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium TCP MD on SSD',
                            pragma_suffix: '-hw-medium-tcp-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_TCP_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            other_packages: 'mercury-libfabric',
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'test_create_max_pool',
                            default_nvme: 'auto',
                            provider: 'ofi+tcp',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium TCP Provider MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium TCP Provider MD on SSD',
                            pragma_suffix: '-hw-medium-tcp-provider-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_TCP_PROVIDER_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            other_packages: 'mercury-libfabric',
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'test_daos_management',
                            default_nvme: 'auto',
                            provider: 'ofi+tcp',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large TCP MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large TCP MD on SSD',
                            pragma_suffix: '-hw-large-tcp-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_TCP_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            other_packages: 'mercury-libfabric',
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'test_daos_dfs_sys',
                            default_nvme: 'auto',
                            provider: 'ofi+tcp',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium Verbs MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium Verbs MD on SSD',
                            pragma_suffix: '-hw-medium-verbs-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            other_packages: 'mercury-libfabric',
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'test_create_max_pool',
                            default_nvme: 'auto',
                            provider: 'ofi+verbs;ofi_rxm',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large Verbs MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large Verbs MD on SSD',
                            pragma_suffix: '-hw-large-verbs-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_VERBS_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            other_packages: 'mercury-libfabric',
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'test_daos_dfs_sys',
                            default_nvme: 'auto',
                            provider: 'ofi+verbs;ofi_rxm',
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
