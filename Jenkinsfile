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
@Library(value="pipeline-lib@jemalmbe/daos-14449") _

/* groovylint-disable-next-line CompileStatic */
job_status_internal = [:]

void job_status_write() {
    if (!env.DAOS_STACK_JOB_STATUS_DIR) {
        return
    }
    String jobName = env.JOB_NAME.replace('/', '_')
    jobName += '_' + env.BUILD_NUMBER
    String fileName = env.DAOS_STACK_JOB_STATUS_DIR + '/' + jobName

    String job_status_text = writeYaml data: job_status_internal,
                                       returnText: true

    // Need to use shell script for creating files that are not
    // in the workspace.
    sh label: "Write jenkins_job_status ${fileName}",
       script: "echo \"${job_status_text}\" >> ${fileName}"
}

// groovylint-disable-next-line MethodParameterTypeRequired
void job_status_update(String name=env.STAGE_NAME,
                       // groovylint-disable-next-line NoDef
                       def value=currentBuild.currentResult) {
    String key = name.replace(' ', '_')
    key = key.replaceAll('[ .]', '_')
    job_status_internal[key] = value
}

// groovylint-disable-next-line MethodParameterTypeRequired, NoDef
void job_step_update(def value) {
    // Wrapper around a pipeline step to obtain a status.
    job_status_update(env.STAGE_NAME, value)
}

// For master, this is just some wildly high number
/* groovylint-disable-next-line CompileStatic */
String next_version = '2.3'

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith('weekly-testing') &&
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

pipeline {
    agent { label 'lightweight' }

    // triggers {
    //     cron(env.BRANCH_NAME == 'release/2.2' ? 'TZ=America/Toronto\n0 12 * * *\n' : '')
    // }

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
        string(name: 'CI_CENTOS7_TARGET',
               defaultValue: '',
               description: 'Image to used for CentOS 7 CI tests.  I.e. el7, el7.9, etc.')
        string(name: 'CI_EL8_TARGET',
               defaultValue: '',
               description: 'Image to used for EL 8 CI tests.  I.e. el8, el8.3, etc.')
        string(name: 'CI_LEAP15_TARGET',
               defaultValue: '',
               description: 'Image to use for OpenSUSE Leap CI tests.  I.e. leap15, leap15.2, etc.')
        string(name: 'CI_UBUNTU20.04_TARGET',
               defaultValue: '',
               description: 'Image to used for Ubuntu 20 CI tests.  I.e. ubuntu20.04, etc.')
        booleanParam(name: 'CI_RPM_el7_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build RPM packages for CentOS 7')
        booleanParam(name: 'CI_RPM_el8_NOBUILD',
                     defaultValue: false,
                     description: 'Do not build RPM packages for EL 8')
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
                     description: 'Run the Unit Test stage')
        booleanParam(name: 'CI_UNIT_TEST_MEMCHECK',
                     defaultValue: true,
                     description: 'Run the Unit Test with memcheck stage')
        booleanParam(name: 'CI_FI_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Fault injection testing stage')
        booleanParam(name: 'CI_FUNCTIONAL_el7_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on CentOS 7 stage')
        booleanParam(name: 'CI_MORE_FUNCTIONAL_PR_TESTS',
                     defaultValue: false,
                     description: 'Enable more distros for functional CI tests')
        booleanParam(name: 'CI_FUNCTIONAL_el8_VALGRIND_TEST',
                     defaultValue: false,
                     description: 'Run the Functional on EL 8 with Valgrind stage')
        booleanParam(name: 'CI_FUNCTIONAL_el8_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on EL 8 stage')
        booleanParam(name: 'CI_FUNCTIONAL_leap15_TEST',
                     defaultValue: true,
                     description: 'Run the Functional on Leap 15 stage' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_FUNCTIONAL_ubuntu20_TEST',
                     defaultValue: false,
                     description: 'Run the Functional on Ubuntu 20.04 stage' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_RPMS_el7_TEST',
                     defaultValue: true,
                     description: 'Run the Test CentOS 7 RPMs stage')
        booleanParam(name: 'CI_SCAN_RPMS_el7_TEST',
                     defaultValue: true,
                     description: 'Run the Scan CentOS 7 RPMs stage')
        booleanParam(name: 'CI_medium_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium test stage')
        booleanParam(name: 'CI_medium-verbs-provider_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider test stage')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large stage')
        string(name: 'CI_UNIT_VM1_LABEL',
               defaultValue: 'ci_vm1',
               description: 'Label to use for 1 VM node unit and RPM tests')
        string(name: 'FUNCTIONAL_VM_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'CI_NLT_1_LABEL',
               defaultValue: 'ci_nlt_1',
               description: 'Label to use for NLT tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium Verbs Provider stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large tests')
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
                pragmasToEnv()
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
        stage('Pre-build') {
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
                stage('checkpatch') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.checkpatch'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(add_repos: false)
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW,
                                   ignored_files: 'src/control/vendor/*:' +
                                                  'src/include/daos/*.pb-c.h:' +
                                                  'src/common/*.pb-c.[ch]:' +
                                                  'src/mgmt/*.pb-c.[ch]:' +
                                                  'src/engine/*.pb-c.[ch]:' +
                                                  'src/security/*.pb-c.[ch]:' +
                                                  'src/client/java/daos-java/src/main/java/io/daos/dfs/uns/*:' +
                                                  'src/client/java/daos-java/src/main/java/io/daos/obj/attr/*:' +
                                                  /* groovylint-disable-next-line LineLength */
                                                  'src/client/java/daos-java/src/main/native/include/daos_jni_common.h:' +
                                                  'src/client/java/daos-java/src/main/native/*.pb-c.[ch]:' +
                                                  'src/client/java/daos-java/src/main/native/include/*.pb-c.[ch]:' +
                                                  '*.crt:' +
                                                  '*.pem:' +
                                                  '*_test.go:' +
                                                  'src/cart/_structures_from_macros_.h:' +
                                                  'src/tests/ftest/*.patch:' +
                                                  'src/tests/ftest/large_stdout.txt'
                    }
                    post {
                        always {
                            job_status_update()
                            archiveArtifacts artifacts: 'pylint.log', allowEmptyArchive: true
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        /* temporarily moved into stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'pre-build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                } // stage('checkpatch')
                stage('Python Bandit check') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.code_scanning'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(add_repos: false)
                        }
                    }
                    steps {
                        pythonBanditCheck()
                    }
                    post {
                        always {
                            // Bandit will have empty results if it does not
                            // find any issues.
                            junit testResults: 'bandit.xml',
                                  allowEmptyResults: true
                            job_status_update()
                        }
                    }
                } // stage('Python Bandit check')
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
                stage('Build RPM on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        buildRpm()
                    }
                    post {
                        success {
                            buildRpmPost condition: 'success'
                        }
                        unstable {
                            buildRpmPost condition: 'unstable'
                        }
                        failure {
                            buildRpmPost condition: 'failure'
                        }
                        unsuccessful {
                            buildRpmPost condition: 'unsuccessful'
                        }
                        cleanup {
                            buildRpmPost condition: 'cleanup'
                            job_status_update()
                        }
                    }
                }
                stage('Build RPM on EL 8.6') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        buildRpm()
                    }
                    post {
                        success {
                            buildRpmPost condition: 'success'
                        }
                        unstable {
                            buildRpmPost condition: 'unstable'
                        }
                        failure {
                            buildRpmPost condition: 'failure'
                        }
                        unsuccessful {
                            buildRpmPost condition: 'unsuccessful'
                        }
                        cleanup {
                            buildRpmPost condition: 'cleanup'
                            job_status_update()
                        }
                    }
                }
                stage('Build RPM on Leap 15.4') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() + '--build-arg FVERSION=37'
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        buildRpm()
                    }
                    post {
                        success {
                            buildRpmPost condition: 'success'
                        }
                        unstable {
                            buildRpmPost condition: 'unstable'
                        }
                        failure {
                            buildRpmPost condition: 'failure'
                        }
                        unsuccessful {
                            buildRpmPost condition: 'unsuccessful'
                        }
                        cleanup {
                            buildRpmPost condition: 'cleanup'
                            job_status_update()
                        }
                    }
                }
                stage('Build DEB on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.ubuntu.20.04'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        buildRpm()
                    }
                    post {
                        success {
                            buildRpmPost condition: 'success'
                        }
                        unstable {
                            buildRpmPost condition: 'unstable'
                        }
                        failure {
                            buildRpmPost condition: 'failure'
                        }
                        unsuccessful {
                            buildRpmPost condition: 'unsuccessful'
                        }
                        cleanup {
                            buildRpmPost condition: 'cleanup'
                            job_status_update()
                        }
                    }
                }
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
                                                                parallel_build: true,
                                                                qb: quickBuild()) +
                                                " -t ${sanitized_JOB_NAME}-el8 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quickBuildDeps('el8') + '"' +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: true,
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   build_deps: 'no',
                                   stash_opt: true,
                                   scons_exe: 'scons-3',
                                   scons_args: sconsFaultsArgs() +
                                               ' PREFIX=/opt/daos TARGET_TYPE=release'
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
                                                                parallel_build: true,
                                                                qb: true) +
                                                " -t ${sanitized_JOB_NAME}-el8 " +
                                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quickBuildDeps('el8', true) + '"' +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallelBuild(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   build_deps: 'no',
                                   stash_opt: true,
                                   scons_exe: 'scons-3',
                                   scons_args: sconsFaultsArgs() +
                                               ' PREFIX=/opt/daos TARGET_TYPE=release'
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
                stage('Build on Leap 15.4 with Intel-C and TARGET_PREFIX') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.leap.15'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                parallel_build: true,
                                                                deps_build: true) +
                                                " -t ${sanitized_JOB_NAME}-leap15" +
                                                ' --build-arg COMPILER=icc'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallelBuild(),
                                   scons_args: sconsFaultsArgs() + ' PREFIX=/opt/daos TARGET_TYPE=release',
                                   build_deps: 'no'
                    }
                    post {
                        unsuccessful {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-intelc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
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
                stage('Unit Test on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 unstash_opt: true,
                                 inst_repos: prRepos(),
                                 inst_rpms: unitPackages()
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_logs/*']
                            job_status_update()
                        }
                    }
                }
                stage('NLT on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_NLT_1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 unstash_opt: true,
                                 inst_repos: prRepos(),
                                 test_script: 'ci/unit/test_nlt.sh',
                                 inst_rpms: unitPackages()
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['nlt_logs/*'],
                                         testResults: 'nlt-junit.xml',
                                         always_script: 'ci/unit/test_nlt_post.sh',
                                         referenceJobName: 'daos-stack/daos/release%252F2.2',
                                         valgrind_stash: 'el8-gcc-nlt-memcheck'
                            recordIssues enabledForFailure: true,
                                         failOnError: false,
                                         ignoreFailedBuilds: true,
                                         ignoreQualityGate: true,
                                         name: 'NLT server leaks',
                                         qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]],
                                         tool: issues(pattern: 'nlt-server-leaks.json',
                                           name: 'NLT server results',
                                           id: 'NLT_server')
                            job_status_update()
                        }
                    }
                }
                stage('Unit Test Bullseye on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 unstash_opt: true,
                                 ignore_failure: true,
                                 inst_repos: prRepos(),
                                 inst_rpms: unitPackages()
                    }
                    post {
                        always {
                            // This is only set while dealing with issues
                            // caused by code coverage instrumentation affecting
                            // test results, and while code coverage is being
                            // added.
                            unitTestPost ignore_failure: true,
                                         artifacts: ['covc_test_logs/*',
                                                     'covc_vm_test/**']
                            job_status_update()
                        }
                    }
                } // stage('Unit test Bullseye on EL 8')
                stage('Unit Test with memcheck on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 unstash_opt: true,
                                 ignore_failure: true,
                                 inst_repos: prRepos(),
                                 inst_rpms: unitPackages()
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_logs.tar.gz',
                                                     'unit_test_memcheck_logs/*.log'],
                                         valgrind_stash: 'el8-gcc-unit-memcheck'
                            job_status_update()
                        }
                    }
                } // stage('Unit Test with memcheck on EL 8')
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
                        label params.FUNCTIONAL_VM_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on CentOS 7')
                stage('Functional on EL 8 with Valgrind') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.FUNCTIONAL_VM_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8 with Valgrind')
                stage('Functional on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'EL8-VM9-label', def_val: params.FUNCTIONAL_VM_LABEL)
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8')
                stage('Functional on Leap 15.4') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'Leap15-VM9-label', def_val: params.FUNCTIONAL_VM_LABEL)
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } // post
                } // stage('Functional on Ubuntu 20.04')
                stage('Test CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        testRpm inst_repos: daosRepos(),
                                daos_pkg_version: daosPackagesVersion(next_version)
                   }
                    post {
                        always {
                            job_status_update()
                        }
                    }
                } // stage('Test CentOS 7 RPMs')
                stage('Test EL 8.6 RPMs') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        testRpm inst_repos: daosRepos(),
                                target: 'el8.6',
                                daos_pkg_version: daosPackagesVersion('el8', next_version)
                   }
                    post {
                        always {
                            job_status_update()
                        }
                    }
                } // stage('Test EL 8.6 RPMs')
                stage('Fault injection testing on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.el.8'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                parallel_build: true,
                                                                deps_build: true)
                            args '--tmpfs /mnt/daos_0'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: true,
                                   scons_exe: 'scons-3',
                                   scons_args: 'PREFIX=/opt/daos TARGET_TYPE=release BUILD_TYPE=debug',
                                   build_deps: 'no'
                        sh label: 'Fault injection testing using NLT',
                           script: './utils/docker_nlt.sh --class-name el8.fault-injection fi'
                    }
                    post {
                        always {
                            discoverGitReferenceBuild referenceJob: 'daos-stack/daos/master',
                                                      scm: 'daos-stack/daos'
                            recordIssues enabledForFailure: true,
                                         failOnError: false,
                                         ignoreFailedBuilds: true,
                                         ignoreQualityGate: true,
                                         qualityGates: [[threshold: 1, type: 'TOTAL_ERROR'],
                                                        [threshold: 1, type: 'TOTAL_HIGH'],
                                                        [threshold: 1, type: 'NEW_NORMAL', unstable: true],
                                                        [threshold: 1, type: 'NEW_LOW', unstable: true]],
                                         tools: [issues(pattern: 'nlt-errors.json',
                                                        name: 'Fault injection issues',
                                                        id: 'Fault_Injection'),
                                                 issues(pattern: 'nlt-client-leaks.json',
                                                        name: 'Fault injection leaks',
                                                        id: 'NLT_client')]
                            junit testResults: 'nlt-junit.xml'
                            archiveArtifacts artifacts: 'nlt_logs/el8.fault-injection/'
                            job_status_update()
                        }
                    }
                } // stage('Fault inection testing')
            } // parallel
        } // stage('Test')
        stage('Test Storage Prep') {
            when {
                beforeAgent true
                expression { params.CI_STORAGE_PREP_LABEL != '' }
            }
            agent {
                label params.CI_STORAGE_PREP_LABEL
            }
            steps {
                storagePrepTest inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi')
            }
            post {
                cleanup {
                    job_status_update()
                }
            }
        } // stage('Test Storage Prep')
        stage('Test Hardware') {
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional_Hardware_Medium')
                stage('Functional Hardware Medium Verbs Provider') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, 'client-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional_Hardware_Medium Verbs Provider')
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, '{client,server}-tests-openmpi'),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional_Hardware_Large')
            } // parallel
        } // stage('Test Hardware')
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
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                qb: quickBuild()) +
                                " -t ${sanitized_JOB_NAME}-el8 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                quickBuildDeps('el8') + '"' +
                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        // The coverage_healthy is primarily set here
                        // while the code coverage feature is being implemented.
                        cloverReportPublish coverage_stashes: ['el8-covc-unit-cov',
                                                               'func-vm-cov',
                                                               'func-hw-medium-cov',
                                                               'func-hw-large-cov'],
                                            coverage_healthy: [methodCoverage: 0,
                                                               conditionalCoverage: 0,
                                                               statementCoverage: 0],
                                            ignore_failure: true
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
            job_status_update('final_status')
            job_status_write()
            valgrindReportPublish valgrind_stashes: ['el8-gcc-nlt-memcheck',
                                                     'el8-gcc-unit-memcheck']
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
