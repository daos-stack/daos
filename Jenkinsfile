#!/usr/bin/groovy
/* Copyright (C) 2019-2021 Intel Corporation
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

boolean release_candidate() {
    return !sh(label: "Determine if building (a PR of) an RC",
              script: "git diff-index --name-only HEAD^ | grep -q TAG && " +
                      "grep -i '[0-9]rc[0-9]' TAG",
              returnStatus: true)
}

def scons_faults_args() {
    // The default build will have BUILD_TYPE=dev; fault injection enabled
    if ((cachedCommitPragma(pragma: 'faults-enabled', def_val: 'true') == 'true') && !release_candidate()) {
        return "BUILD_TYPE=dev"
    } else {
        return "BUILD_TYPE=release"
    }
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
    return cachedCommitPragma(pragma: 'Quick-build') == 'true' ||
           quick_functional()
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
    if (distro.startsWith('ubuntu20')) {
        return pkgs + "=" + daos_packages_version(distro)
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
    } else if (distro.startsWith('ubuntu20')) {
        repos = cachedCommitPragma(pragma: 'PR-repos-ubuntu20', cache: commit_pragma_cache)
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
                           'spdk-devel libfabric-devel ' +
                           'pmix numactl-devel ' +
                           'libipmctl-devel ' +
                           'python36-tabulate numactl ' +
                           'libyaml-devel ' +
                           'valgrind-devel patchelf'
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
        String dist = ""
        if (version.indexOf('-') > -1) {
            // only tack on the %{dist} if the release was specified
            if (distro == "centos7") {
                dist = ".el7"
            } else if (distro == "leap15") {
                dist = ".suse.lp152"
            }
        }
        return version + dist
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
    String daos_pkgs = get_daos_packages(distro)
    String pkgs = " openmpi3 hwloc ndctl fio " +
                  "patchutils ior-hpc-daos-1 " +
                  "romio-tests-daos-1 " +
                  "testmpio " +
                  "mpi4py-tests " +
                  "hdf5-mpich-tests " +
                  "hdf5-openmpi3-tests " +
                  "hdf5-vol-daos-mpich2-tests-daos-1 " +
                  "hdf5-vol-daos-openmpi3-tests-daos-1 " +
                  "MACSio-mpich " +
                  "MACSio-openmpi3 " +
                  "mpifileutils-mpich-daos-1 "
    if (distro == "leap15") {
        return daos_pkgs + pkgs
    } else if (distro == "centos7") {
        // need to exclude openmpi until we remove it from the repo
        return  "--exclude openmpi " + daos_pkgs + pkgs
    } else if (distro.startsWith('ubuntu20')) {
        return daos_pkgs + " openmpi-bin ndctl fio"
    } else {
        error 'functional_packages not implemented for ' + stage_info['target']
    }
}

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

// Default priority is 3, lower is better.
// The parameter for a job is set using the script/Jenkinsfile from the
// previous build of that job, so the first build of any PR will always
// run at default because Jenkins sees it as a new job, but subsequent
// ones will use the value from here.
// The advantage therefore is not to change the priority of PRs, but to
// change the master branch itself to run at lower priority, resulting
// in faster time-to-result for PRs.

String get_priority() {
    if (env.BRANCH_NAME == 'master') {
        string p = '2'
    } else {
        string p = ''
    }
    echo "Build priority set to " + p == '' ? 'default' : p
    return p
}

String rpm_test_version() {
    return cachedCommitPragma(pragma: 'RPM-test-version')
}

boolean skip_prebuild() {
    return target_branch == 'weekly-testing'
}

boolean skip_checkpatch() {
    return skip_stage('checkpatch') ||
           doc_only_change() ||
           quick_functional()
}

boolean skip_build() {
    // always build branch landings as we depend on lastSuccessfulBuild
    // always having RPMs in it
    return (env.BRANCH_NAME != target_branch) &&
           skip_stage('build') ||
           doc_only_change() ||
           rpm_test_version() != ''
}

boolean quick_functional() {
    return cachedCommitPragma(pragma: 'Quick-Functional') == 'true'
}

boolean skip_build_rpm(String distro) {
    return target_branch == 'weekly-testing' ||
           skip_stage('build-' + distro + '-rpm') ||
           distro == 'ubuntu20' && quick_functional()
}

boolean skip_build_on_centos7_gcc() {
    return skip_stage('build-centos7-gcc') ||
           quick_functional()
}

boolean skip_ftest(String distro) {
    return distro == 'ubuntu20' ||
           skip_stage('func-test') ||
           skip_stage('func-test-vm') ||
           ! tests_in_stage('vm') ||
           skip_stage('func-test-' + distro)
}

boolean skip_test_rpms_centos7() {
    return target_branch == 'weekly-testing' ||
           skip_stage('test') ||
           skip_stage('test-centos-rpms') ||
           quick_functional()
}

boolean skip_scan_rpms_centos7() {
    return target_branch == 'weekly-testing' ||
           skip_stage('scan-centos-rpms') ||
           quick_functional()
}

boolean tests_in_stage(String size) {
    String tags = cachedCommitPragma(pragma: 'Test-tag', def_val: 'pr')
    def newtags = []
    if (size == "vm") {
        for (String tag in tags.split(" ")) {
            newtags.add(tag + ",-hw")
        }
        tags += ",-hw"
    } else {
        if (size == "medium") {
            size += ",ib2"
        }
        for (String tag in tags.split(" ")) {
            newtags.add(tag + ",hw," + size)
        }
    }
    tags = newtags.join(" ")
    return sh(label: "Get test list for ${size}",
              script: """cd src/tests/ftest
                         ./launch.py --list ${tags}""",
              returnStatus: true) == 0
}

boolean skip_ftest_hw(String size) {
    return env.DAOS_STACK_CI_HARDWARE_SKIP == 'true' ||
           skip_stage('func-test') ||
           skip_stage('func-hw-test') ||
           skip_stage('func-hw-test-' + size) ||
           ! tests_in_stage(size) ||
           (env.BRANCH_NAME == 'master' && ! startedByTimer())
}

boolean skip_bandit_check() {
    return cachedCommitPragma(pragma: 'Skip-python-bandit',
                              def_val: 'true') == 'true' ||
           quick_functional()
}

boolean skip_build_on_centos7_bullseye() {
    return  env.NO_CI_TESTING == 'true' ||
            skip_stage('bullseye', true) ||
            quick_functional()
}

boolean skip_build_on_centos7_gcc_debug() {
    return skip_stage('build-centos7-gcc-debug') ||
           quickbuild()
}

boolean skip_build_on_centos7_gcc_release() {
    return skip_stage('build-centos7-gcc-release') ||
           quickbuild()
}

boolean skip_build_on_landing_branch() {
    return env.BRANCH_NAME != target_branch ||
           quickbuild()
}

boolean skip_build_on_ubuntu_clang() {
    return target_branch == 'weekly-testing' ||
           skip_stage('build-ubuntu-clang') ||
           quickbuild()

}

boolean skip_build_on_leap15_gcc() {
    return skip_stage('build-leap15-gcc') ||
            quickbuild()
}

boolean skip_build_on_leap15_icc() {
    return target_branch == 'weekly-testing' ||
           skip_stage('build-leap15-icc') ||
           quickbuild()
}

boolean skip_unit_testing_stage() {
    return  env.NO_CI_TESTING == 'true' ||
            (skip_stage('build') &&
             rpm_test_version() == '') ||
            doc_only_change() ||
            skip_build_on_centos7_gcc() ||
            skip_stage('unit-tests')
}

boolean skip_coverity() {
    return skip_stage('coverity-test') ||
           quick_functional()
}

boolean skip_testing_stage() {
    return  env.NO_CI_TESTING == 'true' ||
            (skip_stage('build') &&
             rpm_test_version() == '') ||
            doc_only_change() ||
            skip_stage('test')
}

boolean skip_unit_test() {
    return skip_stage('unit-test') ||
           skip_stage('run_test')
}

boolean skip_bullseye_report() {
    return env.BULLSEYE == null ||
           skip_stage('bullseye', true)
}

String quick_build_deps(String distro) {
    String rpmspec_args = ""
    if (distro == "leap15") {
        rpmspec_args = "--define dist\\ .suse.lp152 " +
                       "--undefine rhel " +
                       "--define suse_version\\ 1502"
    } else if (distro == "centos7") {
        rpmspec_args = "--undefine suse_version " +
                       "--define rhel\\ 7"
    } else {
        error("Unknown distro: ${distro} in quick_build_deps()")
    }
    return sh(label: 'Get Quickbuild dependencies',
              script: "rpmspec -q " +
                      "--srpm " +
                      rpmspec_args + ' ' +
                      "--requires utils/rpms/daos.spec " +
                      "2>/dev/null",
              returnStdout: true)
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? 'TZ=America/Toronto\n0 0,12 * * *\n' : '' +
             env.BRANCH_NAME == 'weekly-testing' ? 'H 0 * * 6' : '')
    }

    environment {
        BULLSEYE = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
        SCONS_FAULTS_ARGS = scons_faults_args()
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        ansiColor('xterm')
        buildDiscarder(logRotator(artifactDaysToKeepStr: '100'))
    }

    parameters {
        string(name: 'BuildPriority',
               defaultValue: get_priority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
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
                expression { ! skip_prebuild() }
            }
            parallel {
                stage('checkpatch') {
                    when {
                        beforeAgent true
                        expression { ! skip_checkpatch() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() +
                                           " -t ${sanitized_JOB_NAME}-centos7 "
                        }
                    }
                    steps {
                        checkPatch user: GITHUB_USER_USR,
                                   password: GITHUB_USER_PSW,
                                   ignored_files: "src/control/vendor/*:" +
                                                  "src/include/daos/*.pb-c.h:" +
                                                  "src/common/*.pb-c.[ch]:" +
                                                  "src/mgmt/*.pb-c.[ch]:" +
                                                  "src/iosrv/*.pb-c.[ch]:" +
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
                      expression { ! skip_bandit_check() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.code_scanning'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
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
                expression { ! skip_build() }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
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
                        expression { ! skip_build_rpm('leap15') }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.mockbuild'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs()
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
                stage('Build DEB on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_rpm('ubuntu20') }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.20.04'
                            dir 'utils/rpms/packaging'
                            label 'docker_runner'
                            args '--privileged=true'
                            additionalBuildArgs dockerBuildArgs()
                            args  '--cap-add=SYS_ADMIN --privileged=true'
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
                        expression { ! skip_build_on_centos7_gcc() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('centos7') + '"' +
                                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
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
                        expression { ! skip_build_on_centos7_bullseye() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                quick_build_deps('centos7') + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-covc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-covc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 debug') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_centos7_gcc_debug() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('centos7') + '"' +
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-gcc-debug
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc-debug',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 release') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_centos7_gcc_release() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('centos7') + '"' +
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-gcc-release
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-gcc-release',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_landing_branch() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('centos7') + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-centos7-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-centos7-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_landing_branch() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.20.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() +
                                                " -t ${sanitized_JOB_NAME}-ubuntu20.04"
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu20.04-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu20.04-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Ubuntu 20.04 with Clang') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_ubuntu_clang() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.20.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() +
                                                " -t ${sanitized_JOB_NAME}-ubuntu20.04"
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu20.04-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-ubuntu20.04-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_leap15_gcc() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                                " -t ${sanitized_JOB_NAME}-leap15 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('leap15') + '"' +
                                                ' --build-arg REPOS="' + pr_repos() + '"'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   stash_files: 'ci/test_files_to_stash.txt',
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-leap15-gcc
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-gcc',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_landing_branch() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() +
                                                " -t ${sanitized_JOB_NAME}-leap15"
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
                        unsuccessful {
                            sh """if [ -f config.log ]; then
                                      mv config.log config.log-leap15-clang
                                  fi"""
                            archiveArtifacts artifacts: 'config.log-leap15-clang',
                                             allowEmptyArchive: true
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C and TARGET_PREFIX') {
                    when {
                        beforeAgent true
                        expression { ! skip_build_on_leap15_icc() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() +
                                                " -t ${sanitized_JOB_NAME}-leap15"
                            args '-v /opt/intel:/opt/intel'
                        }
                    }
                    steps {
                        sconsBuild parallel_build: parallel_build(),
                                   scons_args: scons_faults_args()
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
                            sh "rm -rf _build.external"
                        }
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
                expression { ! skip_unit_testing_stage() }
            }
            parallel {
                stage('Unit Test') {
                    when {
                      beforeAgent true
                      expression { ! skip_unit_test() }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        unitTest timeout_time: 30,
                                 inst_repos: pr_repos(),
                                 inst_rpms: unit_packages()
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
                      expression { ! skip_stage('nlt') }
                    }
                    agent {
                        label 'ci_nlt_1'
                    }
                    steps {
                        unitTest timeout_time: 20,
                                 inst_repos: pr_repos(),
                                 test_script: 'ci/unit/test_nlt.sh',
                                 inst_rpms: unit_packages()
                    }
                    post {
                      always {
                            unitTestPost artifacts: ['nlt_logs/*'],
                                         testResults: 'None',
                                         always_script: 'ci/unit/test_nlt_post.sh',
                                         valgrind_stash: 'centos7-gcc-nlt-memcheck'
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
                        unitTest timeout_time: 30,
                                 ignore_failure: true,
                                 inst_repos: pr_repos(),
                                 inst_rpms: unit_packages()
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
                expression { ! skip_testing_stage() }
            }
            parallel {
                stage('Coverity on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skip_coverity() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: true) +
                                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                                ' --build-arg QUICKBUILD_DEPS="' +
                                                quick_build_deps('centos7') + '"' +
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
                } // stage('Coverity on CentOS 7')
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skip_ftest('el7') }
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
                } // stage('Functional on CentOS 7')
                stage('Functional on Leap 15') {
                    when {
                        beforeAgent true
                        expression { ! skip_ftest('leap15') }
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
                stage('Functional on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { ! skip_ftest('ubuntu20') }
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
                } // stage('Functional on Ubuntu 20.04')
                stage('Functional_Hardware_Small') {
                    when {
                        beforeAgent true
                        expression { ! skip_ftest_hw('small') }
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
                        expression { ! skip_ftest_hw('medium') }
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
                        expression { ! skip_ftest_hw('large') }
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
                        expression { ! skip_test_rpms_centos7() }
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
                        expression { ! skip_scan_rpms_centos7() }
                    }
                    agent {
                        label 'ci_vm1'
                    }
                    steps {
                        scanRpms inst_repos: daos_repos(),
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
                      expression { ! skip_bullseye_report() }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(qb: quickbuild()) +
                                " -t ${sanitized_JOB_NAME}-centos7 " +
                                ' --build-arg BULLSEYE=' + env.BULLSEYE +
                                ' --build-arg QUICKBUILD_DEPS="' +
                                quick_build_deps('centos7') + '"' +
                                ' --build-arg REPOS="' + pr_repos() + '"'
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
