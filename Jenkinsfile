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
@Library(value="pipeline-lib@bmurrell/remove-subsequent-test_tag-override") _

boolean doc_only_change() {
    if (cachedCommitPragma(pragma: 'Doc-only') == 'true') {
        return true
    }

    /* automatic Doc-only discover not really valid for the weekly-testing
     * branch, and ends up requiring ci/doc_only_change.sh which is not available
     * when needed since the change to master is not done yet when this is
     * evaluated.  Additionally, maintaining ci/doc_only_change.sh on this branch
     * is just not worthwhile.
     */
    return false
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
    return cachedCommitPragma(pragma: 'Quick-build') == 'true'
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
    if (target_branch == "weekly-testing" || rpm_test_version() == '') {
        return ""
    }
    return "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
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

commit_pragma_cache = [:]
def cachedCommitPragma(Map config) {

    if (commit_pragma_cache[config['pragma']]) {
        return commit_pragma_cache[config['pragma']]
    }

    commit_pragma_cache[config['pragma']] = commitPragma(config)

    return commit_pragma_cache[config['pragma']]

}

String rpm_dist(String distro) {
    if (distro == "centos7") {
        return ".el7"
    } else if (distro == "leap15") {
        return ".suse.lp152"
    } else {
        error("Don't know what the RPM %{dist} is for ${distro}")
    }
}

rpm_version_cache = ""
String daos_packages_version(String distro) {
    // for weekly-test:
    if (target_branch == "weekly-testing") {
        if (rpm_version_cache != "" && rpm_version_cache != "locked") {
            return rpm_version_cache + rpm_dist(distro)
        }
        if (rpm_version_cache == "") {
            // no cached value and nobody's getting it
            rpm_version_cache = "locked"
            rpm_version_cache = sh(label: "Get RPM packages version",
                                   script: '''repoquery --repofrompath=daos,https://repo.dc.hpdd.intel.com/repository/daos-stack-el-7-x86_64-stable-local/ \
                                                        --repoid daos -q --qf %{version}-%{release} daos''',
                                   returnStdout: true).trim()[0..-5]
        } else {
            // somebody else is getting it, wait for them
            while (rpm_version_cache == "locked") {
                sleep(1)
            }
        }
        return rpm_version_cache + rpm_dist(distro)
    /* what's the query to get the higest 1.0.x package?
    } else if (target_branch == "weekly-testing-1.x") {
        return "release/0.9"
    */
    } else {
        // commit pragma has highest priority
        // TODO: this should actually be determined from the PR-repos artifacts
        String version = rpm_test_version()
        if (version != "") {
            return version + rpm_dist(distro)
        }

        // use the stash after that
        unstash distro + '-rpm-version'
        version = readFile(distro + '-rpm-version').trim()
        if (version != "") {
            return version
        }

        error "Don't know how to determine package version for " + distro
    }
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
                  "patchutils ior-hpc-daos-0 " +
                  "romio-tests-cart-4-daos-0 " +
                  "testmpio-cart-4-daos-0 " +
                  "mpi4py-tests-cart-4-daos-0 " +
                  "hdf5-mpich2-tests-daos-0 " +
                  "hdf5-openmpi3-tests-daos-0 " +
                  "hdf5-vol-daos-mpich2-tests-daos-0 " +
                  "hdf5-vol-daos-openmpi3-tests-daos-0 " +
                  "MACSio-mpich2-daos-0 " +
                  "MACSio-openmpi3-daos-0 " +
                  "mpifileutils-mpich-daos-0 "
    if (distro == "leap15") {
        if (quickbuild()) {
            pkgs += " spdk-tools"
        }
        return daos_pkgs + pkgs
    } else if (distro == "centos7") {
        if (quickbuild()) {
            pkgs += " spdk_tools"
        }
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
String test_tag = "full_regression"

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith("weekly-testing") &&
     !env.BRANCH_NAME.startsWith("release/") &&
     env.BRANCH_NAME != "master")) {
   currentBuild.result = 'SUCCESS'
   return
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

boolean skip_ftest(String distro) {
    return distro == 'ubuntu20' ||
           skip_stage('func-test') ||
           skip_stage('func-test-vm') ||
           skip_stage('func-test-' + distro)
}

boolean skip_ftest_hw(String size) {
    return env.DAOS_STACK_CI_HARDWARE_SKIP == 'true' ||
           skip_stage('func-test') ||
           skip_stage('func-hw-test') ||
           skip_stage('func-hw-test-' + size) ||
           (env.BRANCH_NAME == 'master' && ! startedByTimer())
}

boolean skip_testing_stage() {
    return  env.NO_CI_TESTING == 'true' ||
            (skip_stage('build') &&
             rpm_test_version() == '') ||
            doc_only_change() ||
            skip_stage('test')
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? 'TZ=America/Toronto\n0 0,12 * * *\n' : '' +
             env.BRANCH_NAME.startsWith('weekly-testing') ? 'H 0 * * 6' : '')
    }

    environment {
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = "true"
        SCONS_FAULTS_ARGS = scons_faults_args()
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        ansiColor('xterm')
    }

    parameters {
        string(name: 'BuildPriority',
               defaultValue: get_priority(),
               description: 'Priority of this build.  DO NOT USE WITHOUT PERMISSION.')
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
                expression { ! skip_testing_stage() }
            }
            parallel {
                stage('Functional on CentOS 7') {
                    when {
                        beforeAgent true
                        expression { ! skip_ftest('el7') }
                    }
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
                        functionalTest inst_repos: daos_repos(),
                                       inst_rpms: functional_packages(),
                                       test_tag: test_tag
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
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
                        functionalTest inst_repos: daos_repos(),
                                       inst_rpms: functional_packages(),
                                       test_tag: test_tag
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
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
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
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages(),
                                       test_tag: test_tag
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
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages(),
                                       test_tag: test_tag
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
                        // Need to get back onto master for ci/
                        checkoutScm url: 'https://github.com/daos-stack/daos.git',
                                    branch: "master",
                                    withSubmodules: true
                        functionalTest target: hw_distro_target(),
                                       inst_repos: daos_repos(),
                                       inst_rpms: functional_packages(),
                                       test_tag: test_tag
                    }
                    post {
                        always {
                            functionalTestPost()
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
