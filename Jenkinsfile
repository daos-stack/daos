#!/usr/bin/groovy
/* Copyright 2019-2022 Intel Corporation
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

// For master, this is just some wildly high number
next_version = "2.3.0"

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

// The docker agent setup and the provisionNodes step need to know the
// UID that the build agent is running under.
cached_uid = 0
def getuid() {
    if (cached_uid == 0)
        cached_uid = sh(label: 'getuid()',
                        script: "id -u",
                        returnStdout: true).trim()
    return cached_uid
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'release/2.2' ? 'TZ=America/Toronto\n0 12 * * *\n' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
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
               defaultValue: getPriority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
        string(name: 'TestTag',
               defaultValue: "",
               description: 'Test-tag to use for this run (i.e. pr, daily_regression, full_regression, etc.)')
        string(name: 'BuildType',
               defaultValue: "",
               description: 'Type of build.  Passed to scons as BUILD_TYPE.  (I.e. dev, release, debug, etc.).  Defaults to release on an RC or dev otherwise.')
        string(name: 'TestRepeat',
               defaultValue: "",
               description: 'Test-repeat to use for this run.  Specifies the ' +
                            'number of times to repeat each functional test. ' +
                            'CAUTION: only use in combination with a reduced ' +
                            'number of tests specified with the TestTag ' +
                            'parameter.')
        booleanParam(name: 'CI_BUILD_PACKAGES_ONLY',
                     defaultValue: false,
                     description: 'Only build RPM and DEB packages, Skip unit tests.')
        string(name: 'CI_RPM_TEST_VERSION',
               defaultValue: '',
               description: 'Package version to use instead of building. example: 1.3.103-1, 1.2-2')
        string(name: 'CI_HARDWARE_DISTRO',
               defaultValue: '',
               description: 'Distribution to use for CI Hardware Tests')
        string(name: 'CI_CENTOS7_TARGET',
               defaultValue: '',
               description: 'Image to used for Centos 7 CI tests.  I.e. el7, el7.9, etc.')
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
                     description: 'Run the Unit CI tests')
        booleanParam(name: 'CI_UNIT_TEST_MEMCHECK',
                     defaultValue: true,
                     description: 'Run the Unit Memcheck CI tests')
        booleanParam(name: 'CI_FUNCTIONAL_el7_TEST',
                     defaultValue: true,
                     description: 'Run the functional CentOS 7 CI tests')
        booleanParam(name: 'CI_MORE_FUNCTIONAL_PR_TESTS',
                     defaultValue: false,
                     description: 'Enable more distros for functional CI tests')
        booleanParam(name: 'CI_FUNCTIONAL_el8_TEST',
                     defaultValue: true,
                     description: 'Run the functional EL 8 CI tests' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_FUNCTIONAL_leap15_TEST',
                     defaultValue: true,
                     description: 'Run the functional OpenSUSE Leap 15 CI tests' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_FUNCTIONAL_ubuntu20_TEST',
                     defaultValue: false,
                     description: 'Run the functional Ubuntu 20 CI tests' +
                                  '  Requires CI_MORE_FUNCTIONAL_PR_TESTS')
        booleanParam(name: 'CI_RPMS_el7_TEST',
                     defaultValue: true,
                     description: 'Run the CentOS 7 RPM CI tests')
        booleanParam(name: 'CI_SCAN_RPMS_el7_TEST',
                     defaultValue: true,
                     description: 'Run the Malware Scan for CentOS 7 RPM CI tests')
        booleanParam(name: 'CI_small_TEST',
                     defaultValue: true,
                     description: 'Run the Small Cluster CI tests')
        booleanParam(name: 'CI_medium_TEST',
                     defaultValue: true,
                     description: 'Run the Medium Cluster CI tests')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Large Cluster CI tests')
        string(name: 'CI_UNIT_VM1_LABEL',
               defaultValue: 'ci_vm1',
               description: 'Label to use for 1 VM node unit and RPM tests')
        string(name: 'CI_FUNCTIONAL_VM9_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'CI_NLT_1_LABEL',
               defaultValue: 'ci_nlt_1',
               description: "Label to use for NLT tests")
        string(name: 'CI_NVME_3_LABEL',
               defaultValue: 'ci_nvme3',
               description: 'Label to use for 3 node NVMe tests')
        string(name: 'CI_NVME_5_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node NVMe tests')
        string(name: 'CI_NVME_9_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node NVMe tests')
        string(name: 'CI_STORAGE_PREP_LABEL',
               defaultValue: '',
               description: 'Label for cluster to do a DAOS Storage Preparation')
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
        stage('Pre-build') {
            when {
                beforeAgent true
                expression { ! skipStage() }
            }
            parallel {
                stage('checkpatch') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
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
                                   ignored_files: "src/control/vendor/*:" +
                                                  "src/include/daos/*.pb-c.h:" +
                                                  "src/common/*.pb-c.[ch]:" +
                                                  "src/mgmt/*.pb-c.[ch]:" +
                                                  "src/engine/*.pb-c.[ch]:" +
                                                  "src/security/*.pb-c.[ch]:" +
                                                  "src/client/java/daos-java/src/main/java/io/daos/dfs/uns/*:" +
                                                  "src/client/java/daos-java/src/main/java/io/daos/obj/attr/*:" +
                                                  "src/client/java/daos-java/src/main/native/include/daos_jni_common.h:" +
                                                  "src/client/java/daos-java/src/main/native/*.pb-c.[ch]:" +
                                                  "src/client/java/daos-java/src/main/native/include/*.pb-c.[ch]:" +
                                                  "*.crt:" +
                                                  "*.pem:" +
                                                  "*_test.go:" +
                                                  "src/cart/_structures_from_macros_.h:" +
                                                  "src/tests/ftest/*.patch:" +
                                                  "src/tests/ftest/large_stdout.txt"
                    }
                    post {
                        always {
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
                      expression { ! skipStage() }
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
                expression { ! skipStage() }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args  '--cap-add=SYS_ADMIN'
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
                        }
                    }
                }
                stage('Build RPM on EL 8.5') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args  '--cap-add=SYS_ADMIN'
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
                        }
                    }
                }
                stage('Build RPM on Leap 15') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args  '--cap-add=SYS_ADMIN'
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
                        }
                    }
                }
                stage('Build DEB on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.ubuntu.20.04'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
                            args  '--cap-add=SYS_ADMIN'
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
                        }
                    }
                }
                stage('Build on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.centos.7'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                qb: quickBuild()) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quickBuildDeps('centos7') + '"' +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallelBuild(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   scons_exe: 'scons-3',
                                   scons_args: sconsFaultsArgs()
                    }
                    post {
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 Bullseye') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.centos.7'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                qb: quickBuild()) +
                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                quickBuildDeps('centos7') + '"' +
                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallelBuild(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   scons_exe: 'scons-3',
                                   scons_args: sconsFaultsArgs()
                    }
                    post {
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-covc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-covc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C and TARGET_PREFIX') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.leap.15'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                parallel_build: true,
                                                                deps_build: true) +
                                                " -t ${sanitized_JOB_NAME}-leap15" +
                                                " --build-arg COMPILER=icc"
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallelBuild(),
                                   scons_args: sconsFaultsArgs() + " PREFIX=/opt/daos TARGET_TYPE=release",
                                   build_deps: "no"
                    }
                    post {
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-leap15-intelc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
                                             allowEmptyArchive: true
                        }
                    }
                }
            }
        }
        stage('Unit Tests') {
            when {
                beforeAgent true
                expression { ! skipStage() }
            }
            parallel {
                stage('Unit Test') {
                    when {
                      beforeAgent true
                      expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 inst_repos: prRepos(),
                                 inst_rpms: unitPackages()
                    }
                    post {
                      always {
                            unitTestPost artifacts: ['unit_test_logs/*']
                        }
                    }
                }
                stage('NLT') {
                    when {
                      beforeAgent true
                      expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_NLT_1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 inst_repos: prRepos(),
                                 test_script: 'ci/unit/test_nlt.sh',
                                 inst_rpms: unitPackages()
                    }
                    post {
                      always {
                            unitTestPost artifacts: ['nlt_logs/*'],
                                         testResults: 'nlt-junit.xml',
                                         always_script: 'ci/unit/test_nlt_post.sh',
                                         valgrind_stash: 'centos7-gcc-nlt-memcheck'
                            recordIssues enabledForFailure: true,
                                         failOnError: false,
                                         ignoreFailedBuilds: true,
                                         ignoreQualityGate: true,
                                         name: "NLT server leaks",
                                         qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]],
                                         tool: issues(pattern: 'nlt-server-leaks.json',
                                           name: 'NLT server results',
                                           id: 'NLT_server')
                        }
                    }
                }
                stage('Unit Test Bullseye') {
                    when {
                      beforeAgent true
                      expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 60,
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
                        }
                    }
                } // stage('Unit test Bullseye')
                stage('Unit Test with memcheck') {
                    when {
                      beforeAgent true
                      expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        unitTest timeout_time: 35,
                                 ignore_failure: true,
                                 inst_repos: prRepos(),
                                 inst_rpms: unitPackages()
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_logs.tar.gz',
                                                     'unit_test_memcheck_logs/*.log'],
                                         valgrind_stash: 'centos7-gcc-unit-memcheck'
                        }
                    }
                } // stage('Unit Test with memcheck')
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                expression { ! skipStage() }
            }
            parallel {
                stage('Coverity on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.centos.7'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                qb: true) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quickBuildDeps('centos7', true) + '"' +
                                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        sconsBuild coverity: "daos-stack/daos",
                                   parallel_build: parallelBuild(),
                                   scons_exe: 'scons-3'
                    }
                    post {
                        success {
                            coverityPost condition: 'success'
                        }
                        unsuccessful {
                            coverityPost condition: 'unsuccessful'
                        }
                    }
                } // stage('Coverity on CentOS 7')
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on CentOS 7')
                stage('Functional on EL 8 with Valgrind') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional on EL 8 with Valgrind')
                stage('Functional on EL 8') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    } // post
                } // stage('Functional on Ubuntu 20.04')
                stage('Test CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        testRpm inst_repos: daosRepos(),
                                daos_pkg_version: daosPackagesVersion(next_version)
                   }
                } // stage('Test CentOS 7 RPMs')
                stage('Test EL 8.5 RPMs') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        testRpm inst_repos: daosRepos(),
                                target: 'el8.5',
                                daos_pkg_version: daosPackagesVersion("el8", next_version)
                   }
                } // stage('Test EL 8.5 RPMs')
                stage('Scan CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        scanRpms inst_repos: daosRepos(),
                                 daos_pkg_version: daosPackagesVersion(next_version)
                    }
                    post {
                        always {
                            junit 'maldetect.xml'
                        }
                    }
                } // stage('Scan CentOS 7 RPMs')
                stage('Scan EL 8 RPMs') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        scanRpms inst_repos: daosRepos(),
                                 daos_pkg_version: daosPackagesVersion(next_version)
                    }
                    post {
                        always {
                            junit 'maldetect.xml'
                        }
                    }
                } // stage('Scan EL 8 RPMs')
                stage('Scan Leap 15 RPMs') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        scanRpms inst_repos: daosRepos(),
                                 daos_pkg_version: daosPackagesVersion(next_version)
                    }
                    post {
                        always {
                            junit 'maldetect.xml'
                        }
                    }
                } // stage('Scan Leap 15 RPMs')
                stage('Fault injection testing') {
                    when {
                        beforeAgent true
                        expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.centos.7'
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
                                   scons_args: "PREFIX=/opt/daos TARGET_TYPE=release BUILD_TYPE=debug",
                                   build_deps: "no"
                        sh (script:"""./utils/docker_nlt.sh --class-name centos7.fault-injection fi""",
                            label: 'Fault injection testing using NLT')
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
                            archiveArtifacts artifacts: 'nlt_logs/centos7.fault-injection/'
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
                                inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi")
            }
        } // stage('Test Storage Prep')
        stage('Test Hardware') {
            when {
                beforeAgent true
                expression { ! skipStage() }
            }
            parallel {
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
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
                        functionalTest inst_repos: daosRepos(),
                                       inst_rpms: functionalPackages(1, next_version, "{client,server}-tests-openmpi"),
                                       test_function: 'runTestFunctionalV2'
                    }
                    post {
                        always {
                            functionalTestPostV2()
                        }
                    }
                } // stage('Functional_Hardware_Large')
            } // parallel
        } // stage('Test Hardware')
        stage ('Test Report') {
            parallel {
                stage('Bullseye Report') {
                    when {
                      beforeAgent true
                      expression { ! skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.centos.7'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(repo_type: 'stable',
                                                                qb: quickBuild()) +
                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                quickBuildDeps('centos7') + '"' +
                                ' --build-arg REPOS="' + prRepos() + '"'
                        }
                    }
                    steps {
                        // The coverage_healthy is primarily set here
                        // while the code coverage feature is being implemented.
                        cloverReportPublish coverage_stashes: ['centos7-covc-unit-cov'],
                                            coverage_healthy: [methodCoverage: 0,
                                                               conditionalCoverage: 0,
                                                               statementCoverage: 0],
                                            ignore_failure: true
                    }
                } // stage('Bullseye Report')
            } // parallel
        } // stage ('Test Report')
    } // stages
    post {
        always {
            valgrindReportPublish valgrind_stashes: ['centos7-gcc-nlt-memcheck',
                                                     'centos7-gcc-unit-memcheck']
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
