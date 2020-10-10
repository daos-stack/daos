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

boolean doc_only_change() {
    if (cachedCommitPragma(pragma: 'Doc-only') == 'true') {
        return true
    }
    if (cachedCommitPragma(pragma: 'Doc-only') == 'false') {
        return false
    }

    def rc = sh label: "Determine if doc-only change",
                script: "CHANGE_ID=${env.CHANGE_ID} " +
                        "TARGET_BRANCH=${target_branch} " +
                        'ci/doc_only_change.sh',
                returnStatus: true
    return rc == 1
}

def skip_stage(String stage, boolean def_val = false) {
    String value = 'false'
    if (def_val) {
        value = 'true'
    }
    return cachedCommitPragma(pragma: 'Skip-' + stage,
                              def_val: value) == 'true'
}

boolean quickbuild() {
    return cachedCommitPragma(pragma: 'Quick-build') == 'true'
}

def functional_post_always() {
   return sh(label: "Job Cleanup",
             script: 'ci/functional/job_cleanup.sh',
             returnStatus: true)
}

String get_daos_packages() {
    Map stage_info = parseStageInfo()
    return get_daos_packages(stage_info['target'])
}

String get_daos_packages(String distro) {

    String pkgs
    if (env.TEST_RPMS == 'true') {
        pkgs = "daos{,-{client,tests,server}}"
    } else {
        pkgs = "daos{,-client}"
    }

    return pkgs + "-" + daos_packages_version(distro)
}

String pr_repos() {
    Map stage_info = parseStageInfo()
    return pr_repos(stage_info['target'])
}

String pr_repos(String distro) {
    String repos = ""
    if (distro == 'centos7') {
        repos = cachedCommitPragma(pragma: 'PR-repos-el7')
    } else if (distro == 'leap15') {
        repos = cachedCommitPragma(pragma: 'PR-repos-leap15')
    } else {
       error 'pr_repos not implemented for ' + distro
    }
    return repos + ' ' + cachedCommitPragma(pragma: 'PR-repos')
}

String daos_repo() {
    if (cachedCommitPragma(pragma: 'RPM-test-version') == '') {
        return "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
    } else {
        return ""
    }
}

String hw_distro_target() {
    if (env.STAGE_NAME.contains('Hardware')) {
        if (env.STAGE_NAME.contains('Small')) {
            return hw_distro('small')
        }
        if (env.STAGE_NAME.contains('Medium')) {
            return hw_distro('medium')
        }
        if (env.STAGE_NAME.contains('Large')) {
            return hw_distro('large')
        }
    }
    Map stage_info = parseStageInfo()
    return stage_info['target']
}

String daos_repos() {
    String target = hw_distro_target()
    return daos_repos(target)
}

String daos_repos(String distro) {
    return pr_repos(distro) + ' ' + daos_repo()
}

String unit_packages() {
    Map stage_info = parseStageInfo()
    boolean need_qb = quickbuild()
    if (env.STAGE_NAME.contains('Bullseye')) {
        need_qb = true
    }
    if (stage_info['target'] == 'centos7') {
        String packages =  'gotestsum openmpi3 ' +
                           'hwloc-devel argobots ' +
                           'fuse3-libs fuse3 ' +
                           'boost-devel ' +
                           'libisa-l-devel libpmem ' +
                           'libpmemobj protobuf-c ' +
                           'spdk-devel libfabric-devel '+
                           'pmix numactl-devel ' +
                           'libipmctl-devel ' +
                           'python36-tabulate '
        if (need_qb) {
            // TODO: these should be gotten from the Requires: of RPM
            packages += " spdk-tools mercury-2.0.0~rc1" +
                        " boost-devel libisa-l_crypto libfabric-debuginfo"
        }
        return packages
    } else {
        error 'unit packages not implemented for ' + stage_info['target']
    }
}

commit_pragma_cache = [:]
def cachedCommitPragma(Map config) {

    if (commit_pragma_cache[config['pragma']]) {
        return commit_pragma_cache[config['pragma']]
    }

    commit_pragma_cache[config['pragma']] = commitPragma(config)

    return commit_pragma_cache[config['pragma']]

}

String daos_packages_version() {
    stage_info = parseStageInfo()
    return daos_packages_version(stage_info['target'])
}

String daos_packages_version(String distro) {
    // commit pragma has highest priority
    // TODO: this should actually be determined from the PR-repos artifacts
    String version = cachedCommitPragma(pragma: 'RPM-test-version')
    if (version != "") {
        String dist
        if (distro == "centos7") {
            dist = "el7"
        } else if (distro == "leap15") {
            dist = "suse.lp152"
        }
        return version + "." + dist
    }

    // use the stash after that
    unstash distro + '-rpm-version'
    version = readFile(distro + '-rpm-version').trim()
    if (version != "") {
        return version
    }

    error "Don't know how to determine package version for " + distro
}

boolean parallel_build() {
    // defaults to false
    // true if Quick-build: true unless Parallel-build: false
    def pb = cachedCommitPragma(pragma: 'Parallel-build')
    if (pb == "true" ||
        (quickbuild() && pb != "false")) {
        return true
    }

    return false
}

String hw_distro(String size) {
    // Possible values:
    //'leap15
    //'centos7
    return cachedCommitPragma(pragma: 'Func-hw-test-' + size + '-distro',
                              def_val: cachedCommitPragma(pragma: 'Func-hw-test-distro',
                                                          def_val: 'centos7'))
}

String functional_packages() {
    String target = hw_distro_target()
    return functional_packages(target)
}

String functional_packages(String distro) {
    String pkgs = get_daos_packages(distro)
    pkgs += " openmpi3 hwloc ndctl fio " +
            "ior-hpc-daos-0 " +
            "romio-tests-cart-4-daos-0 " +
            "testmpio-cart-4-daos-0 " + 
            "mpi4py-tests-cart-4-daos-0 " +
            "hdf5-mpich2-tests-daos-0 " +
            "hdf5-openmpi3-tests-daos-0 " +
            "hdf5-vol-daos-mpich2-tests-daos-0 " +
            "hdf5-vol-daos-openmpi3-tests-daos-0 " +
            "MACSio-mpich2-daos-0 " +
            "MACSio-openmpi3-daos-0"
    if (quickbuild()) {
        pkgs += " spdk_tools"
    }
    if (distro == "leap15") {
        return pkgs
    } else if (distro == "centos7") {
        // need to exclude openmpi until we remove it from the repo
        return  "--exclude openmpi " + pkgs
    } else {
        error 'functional_packages not implemented for ' + stage_info['target']
    }
}

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
def arch = ""
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
      ret_str += ' --build-arg REPO_LOCAL_LEAP15=' +
                 env.DAOS_STACK_LEAP_15_LOCAL_REPO
    }
    if (env.DAOS_STACK_LEAP_15_GROUP_REPO) {
      ret_str += ' --build-arg REPO_GROUP_LEAP15=' +
                 env.DAOS_STACK_LEAP_15_GROUP_REPO
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
        cron(env.BRANCH_NAME == 'weekly-testing' ? 'H 0 * * 6' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        BUILDARGS = docker_build_args()
        BUILDARGS_QB_CHECK = docker_build_args(qb: quickbuild())
        BUILDARGS_QB_TRUE = docker_build_args(qb: true)
        QUICKBUILD_DEPS_EL7 = sh label:'Get Quickbuild dependencies',
                                 script: "rpmspec -q --define dist\\ .el7 " +
                                         "--undefine suse_version " +
                                         "--define rhel\\ 7 --srpm " +
                                         "--requires utils/rpms/daos.spec " +
                                         "2>/dev/null",
                                 returnStdout: true
        QUICKBUILD_DEPS_LEAP15 = sh label:'Get Quickbuild dependencies',
                                    script: "rpmspec -q --define dist\\ .suse.lp151 " +
                                            "--undefine rhel " +
                                            "--define suse_version\\ 1501 --srpm " +
                                            "--requires utils/rpms/daos.spec " +
                                            "2>/dev/null",
                                    returnStdout: true
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
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
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-centos7",
                                         tools: [ gcc4(pattern: 'centos7-gcc-build.log'),
                                                  cppCheck(pattern: 'centos7-gcc-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                stage('Build on CentOS 7 Bullseye') {
                    when {
                        beforeAgent true
                        allOf {
                            not { environment name: 'NO_CI_TESTING',
                                  value: 'true' }
                            expression { ! skip_stage('bullseye', true) }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_TRUE' +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-covc-centos7",
                                         tools: [ gcc4(pattern: 'centos7-covc-build.log'),
                                                  cppCheck(pattern: 'centos7-covc-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                        }
                        unsuccessful {
                            sh """if [ -f config${arch}.log ]; then
                                      mv config${arch}.log config.log-centos7-covc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-covc',
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
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-centos7-debug",
                                         tools: [ gcc4(pattern: 'centos7-gcc-debug-build.log'),
                                                  cppCheck(pattern: 'centos7-gcc-debug-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                   excludeFile('_build\\.external\\/.*') ]
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
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build()
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
                                  env.QUICKBUILD_DEPS_EL7 + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-centos7-clang",
                                         tools: [ clang(pattern: 'centos7-clang-build.log'),
                                                  cppCheck(pattern: 'centos7-clang-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu20",
                                         tools: [ gcc4(pattern: 'ubuntu20.04-gcc-build.log'),
                                                  cppCheck(pattern: 'ubuntu20.04-gcc-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-ubuntu20-clang",
                                         tools: [ clang(pattern: 'ubuntu20.04-clang-build.log'),
                                                  cppCheck(pattern: 'ubuntu20.04-clang-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " +
                                '$BUILDARGS_QB_CHECK' +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS_LEAP15 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt'
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-gcc-leap15",
                                         tools: [ gcc4(pattern: 'leap15-gcc-build.log'),
                                                  cppCheck(pattern: 'leap15-gcc-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-clang",
                                         tools: [ clang(pattern: 'leap15-clang-build.log'),
                                                  cppCheck(pattern: 'leap15-clang-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
                        sconsBuild parallel_build: parallel_build()
                    }
                    post {
                        always {
                            recordIssues enabledForFailure: true,
                                         aggregatingResults: true,
                                         id: "analysis-leap15-intelc",
                                         tools: [ intel(pattern: 'leap15-icc-build.log'),
                                                  cppCheck(pattern: 'leap15-icc-build.log') ],
                                         filters: [ excludeFile('.*\\/_build\\.external\\/.*'),
                                                    excludeFile('_build\\.external\\/.*') ]
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
        stage('Unit Tests') {
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
                stage('Unit Test') {
                    when {
                      beforeAgent true
                      allOf {
                          expression { ! skip_stage('unit-test')}
                          expression { ! skip_stage('run_test') }
                      }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 inst_repos: pr_repos(),
                                 inst_rpms: unit_packages()
                    }
                    post {
                      always {
                            unitTestPost artifacts: ['unit_test_logs/*',
                                                     'unit_vm_test/**'],
                                         valgrind_stash: 'centos7-gcc-unit-valg'
                        }
                    }
                }
                stage('Unit Test Bullseye') {
                    when {
                      beforeAgent true
                      expression { ! skip_stage('bullseye', true) }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 ignore_failure: true,
                                 inst_repos: pr_repos(),
                                 inst_rpms: unit_packages()
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
                      expression { ! skip_stage('unit-test-memcheck') }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        unitTest timeout_time: 60,
                                 ignore_failure: true,
                                 inst_repos: pr_repos(),
                                 inst_rpms: unit_packages()
                    }
                    post {
                        always {
                            // This is only set while dealing with issues
                            // caused by code coverage instrumentation affecting
                            // test results, and while code coverage is being
                            // added.
                            unitTestPost ignore_failure: true,
                                         artifacts: ['unit_test_memcheck_logs.tar.gz',
                                                     'unit_memcheck_vm_test/**'],
                                         valgrind_stash: 'centos7-gcc-unit-memcheck'
                        }
                    }
                } // stage('Unit Test with memcheck')
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
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild coverity: "daos-stack/daos",
                                   parallel_build: parallel_build()
                    }
                    post {
                        success {
                            coverityPost condition: 'success'
                        }
                        unsuccessful {
                            coverityPost condition: 'unsuccessful'
                        }
                    }
                }
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('func-test') }
                            expression { ! skip_stage('func-test-vm') }
                            expression { ! skip_stage('func-test-el7')}
                        }
                    }
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        functionalTest inst_repos: daos_repos(),
                                       inst_rpms: functional_packages()
                    }
                    post {
                        always {
                            functionalTestPost()
                        }
                    }
                }
                stage('Functional on Leap 15') {
                    when {
                        beforeAgent true
                        allOf {
                            expression { ! skip_stage('func-test') }
                            expression { ! skip_stage('func-test-vm') }
                            expression { ! skip_stage('func-test-leap15') }
                        }
                    }
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        functionalTest inst_repos: daos_repos(),
                                       inst_rpms: functional_packages()
                    }
                    post {
                        always {
                            functionalTestPost()
                        }
                    } // post
                } // stage('Functional on Leap 15')
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
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages()
                    }
                    post {
                        always {
                            functionalTestPost()
                        }
                    }
                } // stage('Functional_Hardware_Small')
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
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages()
                   }
                    post {
                        always {
                            functionalTestPost()
                        }
                    }
                } // stage('Functional_Hardware_Medium')
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
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages()
                    }
                    post {
                        always {
                            functionalTestPost()
                        }
                    }
                } // stage('Functional_Hardware_Large')
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
                        testRpm inst_repos: daos_repos(),
                                daos_pkg_version: daos_packages_version()
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
                        testRpm inst_repos: daos_repos(),
                                daos_pkg_version: daos_packages_version(),
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
            } // parallel
        } // stage('Test')
        stage ('Test Report') {
            parallel {
                stage('Bullseye Report') {
                    when {
                      beforeAgent true
                      allOf {
                        expression { ! env.BULLSEYE != null }
                        expression { ! skip_stage('bullseye', true) }
                      }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                '$BUILDARGS_QB_TRUE' +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                  env.QUICKBUILD_DEPS_EL7 + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        // The coverage_healthy is primarily set here
                        // while the code coverage feature is being implemented.
                        cloverReportPublish(
                                   coverage_stashes: ['centos7-covc-unit-cov'],
                                   coverage_healthy: [methodCoverage: 0,
                                                      conditionalCoverage: 0,
                                                      statementCoverage: 0],
                                   ignore_failure: true)
                    }
                } // stage('Bullseye Report')
            } // parallel
        } // stage ('Test Report')
    } // stages
    post {
        always {
            valgrindReportPublish valgrind_stashes: ['centos7-gcc-unit-valg',
                                                     'centos7-gcc-unit-memcheck']
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
