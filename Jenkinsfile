#!/usr/bin/groovy
/* Copyright (C) 2019-2020 Intel Corporation
 * All rights reserved.
 *
 * This file is part of the DAOS Project. It is subject to the license terms
 * in the LICENSE file found in the top-level directory of this distribution
 * and at https://img.shields.io/badge/License-Apache%202.0-blue.svg.
 * No part of the DAOS Project, including this file, may be copied, modified,
 * propagated, or distributed except according to the terms contained in the
 * LICENSE file.
 */

// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value="pipeline-lib@your_branch") _

def doc_only_change() {
    if (cachedCommitPragma(pragma: 'Doc-only') == 'true') {
        return true
    }

    def rc = sh label: "Determine if doc-only change",
                script: "CHANGE_ID=${env.CHANGE_ID} " +
                        "TARGET_BRANCH=${target_branch} " +
                        'ci/doc_only_change.sh',
                returnStatus: true
    return rc == 1
}

def skip_stage(String stage) {
    return cachedCommitPragma(pragma: 'Skip-' + stage) == 'true'
}

def quickbuild() {
    return cachedCommitPragma(pragma: 'Quick-build') == 'true'
}

def functional_post_always() {
   return sh(label: "Job Cleanup",
             script: 'ci/functional/job_cleanup.sh',
             returnStatus: true)
}

def get_daos_packages(String distro) {

    def pkgs
    if (env.TEST_RPMS == 'true') {
        pkgs = "daos{,-{client,tests,server}}"
    } else {
        pkgs = "daos{,-client}"
    }

    return pkgs + "-" + daos_packages_version(distro)
}

def component_repos() {
    return cachedCommitPragma(pragma: 'PR-repos')
}

def daos_repo() {
    if (cachedCommitPragma(pragma: 'RPM-test-version') == '') {
        return "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
    } else {
        return ""
    }
}

def el7_daos_repos() {
    return el7_component_repos + ' ' + component_repos() + ' ' + daos_repo()
}

commit_pragma_cache = [:]
def cachedCommitPragma(Map config) {

    if (commit_pragma_cache[config['pragma']]) {
        return commit_pragma_cache[config['pragma']]
    }

    commit_pragma_cache[config['pragma']] = commitPragma(config)

    return commit_pragma_cache[config['pragma']]

}

def daos_packages_version(String distro) {
    // commit pragma has highest priority
    // TODO: this should actually be determined from the PR-repos artifacts
    def version = cachedCommitPragma(pragma: 'RPM-test-version')
    if (version != "") {
        return version
    }

    // use the stash after that
    unstash distro + '-rpm-version'
    version = readFile(distro + '-rpm-version').trim()
    if (version != "") {
        return version
    }

    error "Don't know how to determine package version for " + distro
}

def parallel_build() {
    // defaults to false
    // true if Quick-build: true unless Parallel-build: false
    def pb = cachedCommitPragma(pragma: 'Parallel-build')
    if (pb == "true" ||
        (quickbuild() && pb != "false")) {
        return true
    }

    return false
}

target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
def arch = ""
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def qb_inst_rpms = ""
el7_component_repos = ""
def functional_rpms  = "--exclude openmpi openmpi3 hwloc ndctl " +
                       "ior-hpc-cart-4-daos-0 mpich-autoload-cart-4-daos-0 " +
                       "romio-tests-cart-4-daos-0 hdf5-tests-cart-4-daos-0 " +
                       "mpi4py-tests-cart-4-daos-0 testmpio-cart-4-daos-0 fio"

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
        cached_uid = sh label: 'getuid()',
                        script: "id -u",
                        returnStdout: true
    return cached_uid
}

// This sets up the additinal build arguments for setting up a docker
// build agent from a dockerfile.
// The result of this function need to be stored in an environment
// variable.  Calling this function to create a docker build agent
// fails.  The log shows a truncated command string.
def docker_build_args(Map config = [:]) {
    ret_str = " --build-arg NOBUILD=1 --build-arg UID=" + getuid() +
              " --build-arg JENKINS_URL=$env.JENKINS_URL" +
              " --build-arg CACHEBUST=${currentBuild.startTimeInMillis}"

    if (env.REPOSITORY_URL) {
      ret_str += ' --build-arg REPO_URL=' + env.REPOSITORY_URL
    }
    if (env.DAOS_STACK_EL_7_LOCAL_REPO) {
      ret_str += ' --build-arg REPO_EL7=' + env.DAOS_STACK_EL_7_LOCAL_REPO
    }
    if (env.DAOS_STACK_EL_8_LOCAL_REPO) {
      ret_str += ' --build-arg REPO_EL8=' + env.DAOS_STACK_EL_8_LOCAL_REPO
    }
    if (env.DAOS_STACK_LEAP_15_LOCAL_REPO) {
      ret_str += ' --build-arg REPO_LEAP15=' +
                 env.DAOS_STACK_LEAP_15_LOCAL_REPO
    }
    if (env.HTTP_PROXY) {
      ret_str += ' --build-arg HTTP_PROXY="' + env.HTTP_PROXY + '"'
                 ' --build-arg http_proxy="' + env.HTTP_PROXY + '"'
    }
    if (env.HTTPS_PROXY) {
      ret_str += ' --build-arg HTTPS_PROXY="' + env.HTTPS_PROXY + '"'
                 ' --build-arg https_proxy="' + env.HTTPS_PROXY + '"'
    }
    if (config['qb']) {
      ret_str += ' --build-arg QUICKBUILD=true'
    }
    ret_str += ' '
    return ret_str
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *\n' : '' +
             env.BRANCH_NAME == 'weekly-testing' ? 'H 0 * * 6' : '')
    }

    environment {
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        BUILDARGS = docker_build_args()
        BUILDARGS_QB_CHECK = docker_build_args(qb: quickbuild())
        BUILDARGS_QB_TRUE = docker_build_args(qb: true)
        QUICKBUILD_DEPS = sh label: 'Get Quickbuild dependencies',
                             script: 'rpmspec -q --srpm --requires' +
                                     ' utils/rpms/daos.spec 2>/dev/null',
                             returnStdout: true
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'false')
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        ansiColor('xterm')
    }

    stages {
        stage('Cancel Previous Builds') {
            when { changeRequest() }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Pre-build') {
            when {
                beforeAgent true
                allOf {
                    not { branch 'weekly-testing' }
                    not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                }
            }
            parallel {
                stage('checkpatch') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('checkpatch') }
                            expression { ! doc_only_change() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS' +
                                           "-t ${sanitized_JOB_NAME}-centos7 "
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW,
                                   ignored_files: "src/control/vendor/*:src/include/daos/*.pb-c.h:src/common/*.pb-c.[ch]:src/mgmt/*.pb-c.[ch]:src/iosrv/*.pb-c.[ch]:src/security/*.pb-c.[ch]:*.crt:*.pem:*_test.go:src/cart/_structures_from_macros_.h"
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
                      expression {
                          cachedCommitPragma(pragma: 'Skip-python-bandit',
                                             def_val: 'true') != 'true'
                      }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.code_scanning'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
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
                anyOf {
                    // always build branch landings as we depend on lastSuccessfulBuild
                    // always having RPMs in it
                    branch target_branch
                    allOf {
                        expression { ! skip_stage('build') }
                        expression { ! doc_only_change() }
                        expression { cachedCommitPragma(pragma: 'RPM-test-version') == '' }
                    }
                }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            additionalBuildArgs '$BUILDARGS'
                            args  '--group-add mock --cap-add=SYS_ADMIN --privileged=true'
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
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET',
                                              value: 'weekly-testing' }
                            expression { ! skip_stage('build-leap15-rpm') }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            args '--privileged=true'
                            additionalBuildArgs '$BUILDARGS'
                            args  '--group-add mock --cap-add=SYS_ADMIN --privileged=true'
                        }
                    }
                    steps {
                        buildRpm unstable: true
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
                        allOf {
                            expression { ! skip_stage('build-centos7-gcc') }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_CHECK' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS + '"' +
                                ' --build-arg REPOS="' + component_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   parallel_build: parallel_build(),
                                   log_to_file: 'centos7-gcc-build.log',
                                   failure_artifacts: 'config.log-centos7-gcc'
                        stash name: 'centos7-gcc-install',
                              includes: 'install/**'
                        stash name: 'centos7-gcc-build-vars',
                              includes: ".build_vars${arch}.*"
                        stash name: 'centos7-gcc-tests',
                              includes: '''build/*/*/src/cart/test/utest/test_linkage,
                                           build/*/*/src/cart/test/utest/utest_hlc,
                                           build/*/*/src/cart/test/utest/utest_swim,
                                           build/*/*/src/gurt/tests/test_gurt,
                                           build/*/*/src/rdb/raft/src/tests_main,
                                           build/*/*/src/common/tests/btree_direct,
                                           build/*/*/src/common/tests/btree,
                                           build/*/*/src/common/tests/sched,
                                           build/*/*/src/common/tests/drpc_tests,
                                           build/*/*/src/common/tests/acl_api_tests,
                                           build/*/*/src/common/tests/acl_valid_tests,
                                           build/*/*/src/common/tests/acl_util_tests,
                                           build/*/*/src/common/tests/acl_principal_tests,
                                           build/*/*/src/common/tests/acl_real_tests,
                                           build/*/*/src/common/tests/prop_tests,
                                           build/*/*/src/iosrv/tests/drpc_progress_tests,
                                           build/*/*/src/control/src/github.com/daos-stack/daos/src/control/mgmt,
                                           build/*/*/src/client/api/tests/eq_tests,
                                           build/*/*/src/iosrv/tests/drpc_handler_tests,
                                           build/*/*/src/iosrv/tests/drpc_listener_tests,
                                           build/*/*/src/mgmt/tests/srv_drpc_tests,
                                           build/*/*/src/security/tests/cli_security_tests,
                                           build/*/*/src/security/tests/srv_acl_tests,
                                           build/*/*/src/vos/vea/tests/vea_ut,
                                           build/*/*/src/common/tests/umem_test,
                                           build/*/*/src/bio/smd/tests/smd_ut,
                                           utils/sl/build_info/**,
                                           src/common/tests/btree.sh,
                                           src/control/run_go_tests.sh,
                                           src/rdb/raft_tests/raft_tests.py,
                                           src/vos/tests/evt_ctl.sh
                                           src/control/lib/netdetect/netdetect.go'''
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-centos7",
                                         tools: [ gcc4(pattern: 'centos7-gcc-build.log'),
                                                  cppCheck(pattern: 'centos7-gcc-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 debug') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('build-centos7-gcc-debug') }
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_CHECK' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS + '"' +
                                ' --build-arg REPOS="' + component_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   BUILD_TYPE: 'debug',
                                   parallel_build: parallel_build(),
                                   log_to_file: 'centos7-gcc-debug-build.log',
                                   failure_artifacts: 'config.log-centos7-gcc-debug'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-centos7-debug",
                                         tools: [ gcc4(pattern: 'centos7-gcc-debug-build.log'),
                                                  cppCheck(pattern: 'centos7-gcc-debug-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-gcc-debug
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc-debug',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 release') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('build-centos7-gcc-release') }
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_CHECK' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS + '"' +
                                ' --build-arg REPOS="' + component_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   BUILD_TYPE: 'release',
                                   parallel_build: parallel_build(),
                                   log_to_file: 'centos7-gcc-release-build.log',
                                   failure_artifacts: 'config.log-centos7-gcc-release'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-centos7-release",
                                         tools: [ gcc4(pattern: 'centos7-gcc-release-build.log'),
                                                  cppCheck(pattern: 'centos7-gcc-release-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-gcc-release
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc-release',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch target_branch
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_CHECK' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS + '"'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   prebuild: 'rm -rf bandit.xml',
                                   COMPILER: "clang",
                                   parallel_build: parallel_build(),
                                   log_to_file: 'centos7-clang-build.log',
                                   failure_artifacts: 'config.log-centos7-clang'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-centos7-clang",
                                         tools: [ clang(pattern: 'centos7-clang-build.log'),
                                                  cppCheck(pattern: 'centos7-clang-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        allOf {
                            branch target_branch
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.20.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu20.04 " +
                                                '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   prebuild: 'rm -rf bandit.xml',
                                   parallel_build: parallel_build(),
                                   log_to_file: 'ubuntu20.04-gcc-build.log',
                                   failure_artifacts: 'config.log-ubuntu20.04-gcc'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu20",
                                         tools: [ gcc4(pattern: 'ubuntu20.04-gcc-build.log'),
                                                  cppCheck(pattern: 'ubuntu20.04-gcc-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-ubuntu20.04-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu20.04-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Ubuntu 20.04 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! quickbuild() }
                            expression { ! skip_stage('build-ubuntu-clang') }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.20.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu20.04 " +
                                                '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   prebuild: 'rm -rf bandit.xml',
                                   COMPILER: "clang",
                                   parallel_build: parallel_build(),
                                   log_to_file: 'ubuntu20.04-clang-build.log',
                                   failure_artifacts: 'config.log-ubuntu20.04-clag'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu20-clang",
                                         tools: [ clang(pattern: 'ubuntu20.04-clang-build.log'),
                                                  cppCheck(pattern: 'ubuntu20.04-clang-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-ubuntu20.04-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu20.04-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15') {
                    when {
                        beforeAgent true
                        allOf {
                            branch target_branch
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " +
                                                 '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   prebuild: 'rm -rf bandit.xml',
                                   parallel_build: parallel_build(),
                                   log_to_file: 'leap15-gcc-build.log',
                                   failure_artifacts: 'config.log-leap15-gcc'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-leap15",
                                         tools: [ gcc4(pattern: 'leap15-gcc-build.log'),
                                                  cppCheck(pattern: 'leap15-gcc-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch target_branch
                            expression { ! quickbuild() }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " +
                                                 '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   COMPILER: "clang",
                                   parallel_build: parallel_build(),
                                   log_to_file: 'leap15-clang-build.log',
                                   failure_artifacts: 'config.log-leap15-clang'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-clang",
                                         tools: [ clang(pattern: 'leap15-clang-build.log'),
                                                  cppCheck(pattern: 'leap15-clang-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C and TARGET_PREFIX') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET', value: 'weekly-testing' }
                            expression { ! quickbuild() }
                            expression { ! skip_stage('build-leap15-icc') }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " +
                                                '$BUILDARGS'
                            args '-v /opt/intel:/opt/intel'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   prebuild: 'rm -rf src/rdb/raft/CLinkedListQueue',
                                   COMPILER: "icc",
                                   parallel_build: parallel_build(),
                                   log_to_file: 'leap15-icc-build.log',
                                   TARGET_PREFIX: 'install/opt',
                                   failure_artifacts: 'config.log-leap15-icc'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-intelc",
                                         tools: [ intel(pattern: 'leap15-icc-build.log'),
                                                  cppCheck(pattern: 'leap15-icc-build.log') ],
                                         filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*')]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-leap15-intelc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
                                             allowEmptyArchive: true
                        }
                    }
                }
            }
        }
        stage('Unit Test') {
            when {
                beforeAgent true
                allOf {
                    not { environment name: 'NO_CI_TESTING', value: 'true' }
                    // nothing to test if build was skipped
                    expression { ! skip_stage('build') }
                    // or it's a doc-only change
                    expression { ! doc_only_change() }
                    expression { ! skip_stage('test') }
                    expression { cachedCommitPragma(pragma: 'RPM-test-version') == '' }
                }
            }
            parallel {
                stage('run_test.sh') {
                    when {
                      beforeAgent true
                      expression { ! skip_stage('run_test') }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        script {
                            if (quickbuild()) {
                                // TODO: these should be gotten from the Requires: of RPMs
                                qb_inst_rpms = " spdk-tools mercury boost-devel"
                            }
                        }
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 1,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_component_repos + ' ' +
                                                   component_repos(),
                                       inst_rpms: 'gotestsum openmpi3 ' +
                                                  'hwloc-devel argobots ' +
                                                  'fuse3-libs fuse3 ' +
                                                  'boost-devel ' +
                                                  'libisa-l-devel libpmem ' +
                                                  'libpmemobj protobuf-c ' +
                                                  'spdk-devel libfabric-devel '+
                                                  'pmix numactl-devel ' +
                                                  'libipmctl-devel ' +
						    'python36-tabulate' +
                                                  qb_inst_rpms
                        timeout(time:60, unit:'MINUTES') {
                          runTest stashes: [ 'centos7-gcc-tests',
                                           'centos7-gcc-install',
                                           'centos7-gcc-build-vars' ],
                                script: "SSH_KEY_ARGS=${env.SSH_KEY_ARGS} " +
                                        "NODELIST=${env.NODELIST} " +
                                        'ci/unit/test_main.sh',
                                junit_files: 'test_results/*.xml'
                        }
                    }
                    post {
                      always {
                            // https://issues.jenkins-ci.org/browse/JENKINS-58952
                            // label is at the end
                            // sh label: "Collect artifacts and tear down",
                            //   script '''set -ex
                            sh script: 'ci/unit/test_post_always.sh',
                               label: "Collect artifacts and tear down"
                            junit 'test_results/*.xml'
                            archiveArtifacts artifacts: 'run_test.sh/**'
                            archiveArtifacts artifacts: 'vm_test/**'
                            publishValgrind (
                                    failBuildOnInvalidReports: true,
                                    failBuildOnMissingReports: true,
                                    failThresholdDefinitelyLost: '0',
                                    failThresholdInvalidReadWrite: '0',
                                    failThresholdTotal: '0',
                                    pattern: 'dnt.*.memcheck.xml',
                                    publishResultsForAbortedBuilds: false,
                                    publishResultsForFailedBuilds: true,
                                    sourceSubstitutionPaths: '',
                                    unstableThresholdDefinitelyLost: '0',
                                    unstableThresholdInvalidReadWrite: '0',
                                    unstableThresholdTotal: '0'
                            )
                            recordIssues enabledForFailure: true,
                                         failOnError: true,
                                         referenceJobName: 'daos-stack/daos/master',
                                         ignoreFailedBuilds: false,
                                         ignoreQualityGate: true,
                                         /* Set qualitygate to 1 new "NORMAL" priority message
                                           * Supporting messages to help identify causes of
                                           * problems are set to "LOW", and there are a
                                           * number of intermittent issues during server
                                           * shutdown that would normally be NORMAL but in
                                           * order to have stable results are set to LOW.
                                           */
                                         qualityGates: [[threshold: 1, type: 'TOTAL_HIGH', unstable: true],
                                                        [threshold: 1, type: 'TOTAL_ERROR', unstable: true],
                                                        [threshold: 1, type: 'NEW_NORMAL', unstable: true]],
                                         name: "Node local testing",
                                         tool: issues(pattern: 'vm_test/nlt-errors.json',
                                                      name: 'NLT results',
                                                      id: 'VM_test')
                        }
                    }
                }
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                allOf {
                    not { environment name: 'NO_CI_TESTING', value: 'true' }
                    // nothing to test if build was skipped
                    expression { ! skip_stage('build') }
                    // or it's a doc-only change
                    expression { ! doc_only_change() }
                    expression { ! skip_stage('test') }
                }
            }
            parallel {
                stage('Coverity on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skip_stage('coverity-test') }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_TRUE' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS + '"' +
                                ' --build-arg REPOS="' + component_repos() + '"'
                        }
                    }
                    steps {
                        sh "rm -f coverity/daos_coverity.tgz"
                        sconsBuild coverity: "daos-stack/daos",
                                   parallel_build: parallel_build(),
                                   clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-centos7-cov'
                    }
                    post {
                        success {
                            sh """rm -rf _build.external${arch}
                                  mkdir -p coverity
                                  rm -f coverity/*
                                  if [ -e cov-int ]; then
                                      tar czf coverity/daos_coverity.tgz cov-int
                                  fi"""
                            archiveArtifacts artifacts: 'coverity/daos_coverity.tgz',
                                             allowEmptyArchive: true
                        }
                        unsuccessful {
                            sh """mkdir -p coverity
                                  if [ -f config${arch}.log ]; then
                                      mv config${arch}.log coverity/config.log-centos7-cov
                                  fi
                                  if [ -f cov-int/build-log.txt ]; then
                                      mv cov-int/build-log.txt coverity/cov-build-log.txt
                                  fi"""
                            archiveArtifacts artifacts: 'coverity/cov-build-log.txt',
                                             allowEmptyArchive: true
                            archiveArtifacts artifacts: 'coverity/config.log-centos7-cov',
                                             allowEmptyArchive: true
                      }
                    }
                }
                stage('Functional') {
                    when {
                        beforeAgent true
                        expression { ! skip_stage('func-test') }
                    }
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       snapshot: true,
                                       inst_repos: el7_daos_repos(),
                                       inst_rpms: get_daos_packages('centos7') + ' ' +
                                                  functional_rpms
                        runTestFunctional stashes: [ 'centos7-gcc-install',
                                                     'centos7-gcc-build-vars' ],
                                          test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '',
                                          test_tag: 'pr,-hw',
                                          node_count: 9,
                                          ftest_arg: ''
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                    }
                }
                stage('Functional_Hardware_Small') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-small') }
                        }
                    }
                    agent {
                        // 2 node cluster with 1 IB/node + 1 test control node
                        label 'ci_nvme3'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 3,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos(),
                                       inst_rpms: get_daos_packages('centos7') +
                                                   ' ' + functional_rpms
                        runTestFunctional stashes: [ 'centos7-gcc-install',
                                                     'centos7-gcc-build-vars' ],
                                          test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-small',
                                          test_tag: 'pr,hw,small',
                                          node_count: 3,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                    }
                }
                stage('Functional_Hardware_Medium') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-medium') }
                        }
                    }
                    agent {
                        // 4 node cluster with 2 IB/node + 1 test control node
                        label 'ci_nvme5'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 5,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos(),
                                       inst_rpms: get_daos_packages('centos7') + ' ' +
                                                  functional_rpms
                        runTestFunctional stashes: [ 'centos7-gcc-install',
                                                     'centos7-gcc-build-vars' ],
                                          test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-medium',
                                          test_tag: 'pr,hw,medium,ib2',
                                          node_count: 5,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                    }
                }
                stage('Functional_Hardware_Large') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'DAOS_STACK_CI_HARDWARE_SKIP', value: 'true' }
                            expression { ! skip_stage('func-hw-test') }
                            expression { ! skip_stage('func-hw-test-large') }
                        }
                    }
                    agent {
                        // 8+ node cluster with 1 IB/node + 1 test control node
                        label 'ci_nvme9'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       profile: 'daos_ci',
                                       distro: 'el7',
                                       inst_repos: el7_daos_repos(),
                                       inst_rpms: get_daos_packages('centos7') + ' ' +
                                                  functional_rpms
                        runTestFunctional stashes: [ 'centos7-gcc-install',
                                                     'centos7-gcc-build-vars' ],
                                          test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-large',
                                          test_tag: 'pr,hw,large',
                                          node_count: 9,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                    }
                }
                stage('Test CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET',
                                              value: 'weekly-testing' }
                            expression { ! skip_stage('test') }
                            expression { ! skip_stage('test-centos-rpms') }
                        }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        testRpm inst_repos: el7_daos_repos(),
                                daos_pkg_version: daos_packages_version("centos7")
                   }
                } // stage('Test CentOS 7 RPMs')
                stage('Scan CentOS 7 RPMs') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            not { environment name: 'CHANGE_TARGET',
                                              value: 'weekly-testing' }
                            expression { ! skip_stage('scan-centos-rpms') }
                        }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        testRpm inst_repos: el7_daos_repos(),
                                daos_pkg_version: daos_packages_version("centos7"),
                                inst_rpms: 'clamav clamav-devel',
                                test_script: 'ci/rpm/scan_daos.sh',
                                junit_files: 'maldetect.xml'
                    }
                    post {
                        always {
                            junit 'maldetect.xml'
                        }
                    }
                } // stage('Scan CentOS 7 RPMs')
            }
        }
    }
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    }
}
