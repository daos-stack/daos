#!/usr/bin/groovy
/* groovylint-disable-next-line LineLength */
/* groovylint-disable DuplicateMapLiteral, DuplicateNumberLiteral */
/* groovylint-disable DuplicateStringLiteral, NestedBlockDepth, VariableName */
/* Copyright 2019-2024 Intel Corporation
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

// Should try to figure this out automatically
/* groovylint-disable-next-line CompileStatic, VariableName */
String base_branch = 'master'

Map nlt_test() {
    // groovylint-disable-next-line NoJavaUtilDate
    Date startDate = new Date()
    try {
        unstash('nltr')
    } catch (e) {
        print 'Unstash failed, results from NLT stage will not be included'
    }
    sh label: 'Fault injection testing using NLT',
       script: './ci/docker_nlt.sh --class-name el8.fault-injection fi'
    List filesList = []
    filesList.addAll(findFiles(glob: '*.memcheck.xml'))
    int vgfail = 0
    int vgerr = 0
    if (filesList) {
        String rcs = sh label: 'Check for Valgrind errors',
               script: "grep -E '<error( |>)' ${filesList.join(' ')} || true",
               returnStdout: true
        if (rcs) {
            vfail = 1
        }
        String suite = sanitizedStageName()
        junitSimpleReport suite: suite,
                          file: suite + '_valgrind_results.xml',
                          fails: vgfail,
                          errors: vgerr,
                          name: 'Valgrind_Memcheck',
                          class: 'Valgrind',
                          message: 'Valgrind Memcheck error detected',
                          testdata: rcs
    }
    int runTime = durationSeconds(startDate)
    Map runData = ['nlttest_time': runTime]
    return runData
}

// For master, this is just some wildly high number
String next_version = '1000'

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith('code-coverage-testing') &&
     !env.BRANCH_NAME.startsWith('weekly-testing') &&
     !env.BRANCH_NAME.startsWith('release/') &&
     !env.BRANCH_NAME.startsWith('feature/') &&
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
    return cachedCommitPragma(pragma: distro + '-VM9-label',
                              def_val: cachedCommitPragma(pragma: 'VM9-label',
                                                          def_val: params.FUNCTIONAL_VM_LABEL))
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        /* groovylint-disable-next-line AddEmptyString */
        cron(env.BRANCH_NAME == 'code-coverage-testing' ? 'TZ=UTC\n0 12 * * 5' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = '-ici_key'
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
        COVFN_DISABLED = cachedCommitPragma(pragma: 'Skip-fnbullseye', def_val: 'false')
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
        // string(name: 'TestNvme',
        //        defaultValue: '',
        //        description: 'The launch.py --nvme argument to use for the Functional test ' +
        //                     'stages of this run (i.e. auto, auto_md_on_ssd, auto:-3DNAND, ' +
        //                     '0000:81:00.0, etc.).  Does not apply to MD on SSD stages.')
        string(name: 'BuildType',
               defaultValue: '',
               description: 'Type of build.  Passed to scons as BUILD_TYPE.  (I.e. dev, release, debug, etc.).  ' +
                            'Defaults to release on an RC or dev otherwise.')
        string(name: 'TestRepeat',
               defaultValue: '',
               description: 'Test-repeat to use for this run.  Specifies the ' +
                            'number of times to repeat each functional test. ' +
                            'CAUTION: only use in combination with a reduced ' +
                            'number of tests specified with the TestTag ' +
                            'parameter.')
        string(name: 'TestProvider',
               defaultValue: '',
               description: 'Test-provider to use for the non-Provider Functional Hardware test ' +
                            'stages.  Specifies the default provider to use the daos_server ' +
                            'config file when running functional tests (the launch.py ' +
                            '--provider argument; i.e. "ucx+dc_x", "ofi+verbs", "ofi+tcp")')
        booleanParam(name: 'CI_BUILD_PACKAGES_ONLY',
                     defaultValue: false,
                     description: 'Only build RPM and DEB packages, Skip unit tests.')
        string(name: 'CI_RPM_TEST_VERSION',
               defaultValue: '',
               description: 'Package version to use instead of building. example: 1.3.103-1, 1.2-2')
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
        booleanParam(name: 'CI_RPM_el8_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build RPM packages for EL 8')
        booleanParam(name: 'CI_RPM_el9_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build RPM packages for EL 9')
        booleanParam(name: 'CI_RPM_leap15_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build RPM packages for Leap 15')
        booleanParam(name: 'CI_DEB_Ubuntu20_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build DEB packages for Ubuntu 20')
        booleanParam(name: 'CI_ALLOW_UNSTABLE_TEST',
                     defaultValue: false,
                     description: 'Continue testing if a previous stage is Unstable')
        booleanParam(name: 'CI_UNIT_TEST',
                     defaultValue: true,
                     description: 'Run the Unit Test on EL 8 test stage')
        booleanParam(name: 'CI_UNIT_TEST_MEMCHECK',
                     defaultValue: true,
                     description: 'Run the Unit Test with memcheck on EL 8 test stage')
        booleanParam(name: 'CI_FI_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Fault injection testing on EL 8 test stage')
        booleanParam(name: 'CI_MORE_FUNCTIONAL_PR_TESTS',
                     defaultValue: false,
                     description: 'Enable more distros for functional CI tests')
        booleanParam(name: 'CI_FUNCTIONAL_el8_VALGRIND_TEST',
                     defaultValue: false,
                     description: 'Run the Functional on EL 8 with Valgrind test stage')
        booleanParam(name: 'CI_FUNCTIONAL_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 8 test stage')
        booleanParam(name: 'CI_FUNCTIONAL_el9_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 9 test stage')
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
        booleanParam(name: 'CI_medium_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium MD on SSD test stage')
        booleanParam(name: 'CI_medium_verbs_provider_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider test stage')
        booleanParam(name: 'CI_medium_verbs_provider_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider MD on SSD test stage')
        booleanParam(name: 'CI_medium_ucx_provider_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium UCX Provider test stage')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large test stage')
        booleanParam(name: 'CI_large_md_on_ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large MD on SSD test stage')
        string(name: 'CI_UNIT_VM1_LABEL',
               defaultValue: 'ci_vm1',
               description: 'Label to use for 1 VM node unit and RPM tests')
        string(name: 'CI_UNIT_VM1_NVME_LABEL',
               defaultValue: 'ci_ssd_vm1',
               description: 'Label to use for 1 VM node unit tests that need NVMe')
        string(name: 'FUNCTIONAL_VM_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'CI_NLT_1_LABEL',
               defaultValue: 'ci_nlt_1',
               description: 'Label to use for NLT tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium (MD on SSD) stages')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium Verbs Provider (MD on SSD) stages')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_UCX_PROVIDER_LABEL',
               defaultValue: 'ci_ofed5',
               description: 'Label to use for 5 node Functional Hardware Medium UCX Provider stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large (MD on SSD) stages')
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
        stage('Build') {
            /* Don't use failFast here as whilst it avoids using extra resources
             * and gives faster results for PRs it's also on for master where we
             * do want complete results in the case of partial failure
             */
            //failFast true
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
                stage('Build on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.el.8'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                deps_build: true,
                                                                parallel_build: true) +
                                                " -t ${sanitized_JOB_NAME}-el8 " +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        job_step_update(
                            sconsBuild(parallel_build: true,
                                       stash_files: 'ci/test_files_to_stash.txt',
                                       build_deps: 'no',
                                       stash_opt: true,
                                       scons_args: sconsFaultsArgs() +
                                                  ' PREFIX=/opt/daos TARGET_TYPE=release'))
                    }
                    post {
                        unsuccessful {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-el8-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-el8-gcc',
                                             allowEmptyArchive: true
                        }
                        cleanup {
                            job_status_update()
                        }
                    }
                }
                stage('Build on EL 8 Bullseye') {
                    // when {
                    //     beforeAgent true
                    //     expression { !skipStage() }
                    // }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.el.8'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                deps_build: true,
                                                                parallel_build: true) +
                                                " -t ${sanitized_JOB_NAME}-el8 " +
                                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        job_step_update(
                            sconsBuild(parallel_build: true,
                                       stash_files: 'ci/test_files_to_stash.txt',
                                       build_deps: 'yes',
                                       stash_opt: true,
                                       scons_args: sconsFaultsArgs() +
                                                   ' PREFIX=/opt/daos TARGET_TYPE=release'))
                    }
                    post {
                        unsuccessful {
                            sh label: 'Save failed Bullseye logs',
                               script: '''if [ -f config.log ]; then
                                          mv config.log config.log-el8-covc
                                       fi'''
                            archiveArtifacts artifacts: 'config.log-el8-covc',
                                             allowEmptyArchive: true
                        }
                        cleanup {
                            job_status_update()
                        }
                    }
                }
            }
        }
        stage('Unit Tests') {
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
                stage('Unit Test on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'VM1-label', def_val: params.CI_UNIT_VM1_LABEL)
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     unstash_opt: true,
                                     inst_repos: prRepos(),
                                     inst_rpms: unitPackages()))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_logs/']
                            job_status_update()
                        }
                    }
                }
                stage('Unit Test bdev on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_NVME_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     unstash_opt: true,
                                     inst_repos: prRepos(),
                                     inst_rpms: unitPackages()))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_bdev_logs/']
                            job_status_update()
                        }
                    }
                }
                stage('Unit Test Bullseye on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'VM1-label', def_val: params.CI_UNIT_VM1_LABEL)
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     unstash_opt: true,
                                     ignore_failure: true,
                                     inst_repos: prRepos(),
                                     inst_rpms: unitPackages()))
                    }
                    post {
                        always {
                            // This is only set while dealing with issues
                            // caused by code coverage instrumentation affecting
                            // test results, and while code coverage is being
                            // added.
                            unitTestPost ignore_failure: true,
                                         artifacts: ['covc_test_logs/', 'covc_vm_test/**']
                            job_status_update()
                        }
                    }
                } // stage('Unit test Bullseye on EL 8.8')
                stage('Unit Test with memcheck on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'VM1-label', def_val: params.CI_UNIT_VM1_LABEL)
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     unstash_opt: true,
                                     ignore_failure: true,
                                     inst_repos: prRepos(),
                                     inst_rpms: unitPackages()))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_logs.tar.gz',
                                                     'unit_test_memcheck_logs/**/*.log'],
                                         valgrind_stash: 'el8-gcc-unit-memcheck'
                            job_status_update()
                        }
                    }
                } // stage('Unit Test with memcheck on EL 8.8')
                stage('Unit Test bdev with memcheck on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_NVME_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     unstash_opt: true,
                                     ignore_failure: true,
                                     inst_repos: prRepos(),
                                     inst_rpms: unitPackages()))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_bdev_logs.tar.gz',
                                                     'unit_test_memcheck_bdev_logs/**/*.log'],
                                         valgrind_stash: 'el8-gcc-unit-memcheck-bdev'
                            job_status_update()
                        }
                    }
                } // stage('Unit Test bdev with memcheck on EL 8')
            }
        }
        stage('Test Report') {
            parallel {
                stage('Bullseye Report on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.el.8'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable') +
                                " -t ${sanitized_JOB_NAME}-el8 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        // The coverage_healthy is primarily set here
                        // while the code coverage feature is being implemented.
                        job_step_update(
                            cloverReportPublish(
                                coverage_stashes: ['el8-covc-unit-cov'],
                                coverage_healthy: [methodCoverage: 0,
                                                   conditionalCoverage: 0,
                                                   statementCoverage: 0],
                                ignore_failure: true))
                    }
                    post {
                        cleanup {
                            job_status_update()
                        }
                    }
                } // stage('Bullseye Report on EL 8')
            } // parallel
        } // stage ('Test Report')
    } // stages
    post {
        always {
            valgrindReportPublish valgrind_stashes: ['el8-gcc-unit-memcheck',
                                                     'fault-inject-valgrind']
            job_status_update('final_status')
            jobStatusWrite(job_status_internal)
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
