#!/usr/bin/groovy
/* Copyright (C) 2019-2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value="pipeline-lib@your_branch") _

def skip_stage(String stage) {
    return cachedCommitPragma(pragma: 'Skip-' + stage).contains('true')
}

def functional_post_always() {
   return sh(label: "Job Cleanup",
             script: '''rm -rf install/lib/daos/TESTING/ftest/avocado/job-results/job-*/html/
                        # Remove the latest avocado symlink directory to avoid inclusion in the
                        # jenkins build artifacts
                        unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
                        rm -rf "Functional/"
                        mkdir "Functional/"
                        # compress those potentially huge DAOS logs
                        if daos_logs=$(find install/lib/daos/TESTING/ftest/avocado/job-results/job-*/daos_logs/* -maxdepth 0 -type f -size +1M); then
                            lbzip2 $daos_logs
                        fi
                        arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                        arts="$arts$(ls -d install/lib/daos/TESTING/ftest/avocado/job-results/job-* 2>/dev/null)" && arts="$arts"$'\n'
                        if [ -n "$arts" ]; then
                            mv $(echo $arts | tr '\n' ' ') "Functional/"
                        fi''',
             returnStatus: true)
}

def get_daos_packages(String distro) {

    def pkgs
    if (env.TEST_RPMS == 'true') {
        pkgs = 'spdk-tools\\ \\<\\ 20 spdk\\ \\<\\ 20 daos{,-{client,tests,server}}'
    } else {
        pkgs = 'daos{,-client}'
    }

    return pkgs + "-" + daos_packages_version(distro)
}

def component_repos() {
    return cachedCommitPragma(pragma: 'PR-repos')
}

/* TODO: A Class with a static attribute is probably the correct way to do
 *       this sort of thing
 *       Same for the pragma cache
 *       Something like (but this doesn't work due to CPS):
 * class rpm_version {
 *     private static String v = ""
 *
 *    rpm_version() {
 *       if (v == "" && v != "locked") {
 *            // no cached value and nobody's getting it
 *            println("setting v")
 *            v = "locked"
 *            v = sh(label: "Get RPM packages version",
 *                   script: '''repoquery --repofrompath=daos,https://repo.dc.hpdd.intel.com/repository/daos-stack-el-7-x86_64-stable-local/ \
 *                                        --repoid daos -q --qf %{version}-%{release} daos''',
 *                   returnStdout: true).trim()
 *        }
 *    }
 *    String getV() {
 *        // somebody else is getting it, wait for them
 *        while (v == "" || v == "locked") {
 *            sleep(1)
 *        }
 *        return v
 *    }
 *}
 */
rpm_version_cache = ""
def rpm_version() {
    // for weekly-test:
    if (target_branch == "weekly-testing-1.x") {
        if (rpm_version_cache != "" && rpm_version_cache != "locked") {
            return rpm_version_cache
        }
        if (rpm_version_cache == "") {
            // no cached value and nobody's getting it
            rpm_version_cache = "locked"
            rpm_version_cache = sh(label: "Get RPM packages version",
                                   script: '''repoquery --repofrompath=daos,https://repo.dc.hpdd.intel.com/repository/daos-stack-el-7-x86_64-stable-local/ \
                                                        --repoid daos -q --qf %{version}-%{release} --whatprovides "daos < 1.1" | sort | tail -1''',
                                   returnStdout: true).trim()
        } else {
            // somebody else is getting it, wait for them
            while (rpm_version_cache == "locked") {
                sleep(1)
            }
        }
        return rpm_version_cache
    /* what's the query to get the higest 1.0.x package?
    } else if (target_branch == "weekly-testing-1.x") {
        return "release/0.9"
    */
    } else {
        return cachedCommitPragma(pragma: 'RPM-test-version')
    }
}

def daos_repo() {
    if (rpm_version() == '') {
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
    def version = rpm_version()
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

target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
def test_tag = "full_regression"
def arch = ""

el7_component_repos = ""
def functional_rpms  = "--exclude openmpi openmpi3 hwloc ndctl " +
                       "ior-hpc-cart-4-daos-0 mpich-autoload-cart-4-daos-0 " +
                       "romio-tests-cart-4-daos-0 hdf5-tests-cart-4-daos-0 " +
                       "mpi4py-tests-cart-4-daos-0 testmpio-cart-4-daos-0 " +
                       "fio"

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (!env.BRANCH_NAME.startsWith("weekly-testing") &&
     !env.BRANCH_NAME.startsWith("release/") &&
     env.BRANCH_NAME != "master")) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *\n' : '' +
             env.BRANCH_NAME.startsWith('weekly-testing') ? 'H 0 * * 6' : '')
    }

    environment {
        UID = sh script: "id -u", returnStdout: true
        SSH_KEY_ARGS = "-ici_key"
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = "true"
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
    }

    stages {
        stage('Cancel Previous Builds') {
            when { changeRequest() }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                allOf {
                    not { environment name: 'NO_CI_TESTING', value: 'true' }
                }
            }
            parallel {
                stage('Functional') {
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
                        runTestFunctional test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '',
                                          test_tag: test_tag + ',-hw',
                                          node_count: 9,
                                          ftest_arg: ''
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
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
                                       inst_rpms: get_daos_packages('centos7') + ' ' +
                                                  functional_rpms
                        runTestFunctional test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-small',
                                          test_tag: test_tag + ',hw,small',
                                          node_count: 3,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
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
                        runTestFunctional test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-medium',
                                          test_tag: test_tag + ',hw,medium,ib2',
                                          node_count: 5,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
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
                        runTestFunctional test_rpms: env.TEST_RPMS,
                                          pragma_suffix: '-hw-large',
                                          test_tag: test_tag + ',hw,large',
                                          node_count: 9,
                                          ftest_arg: '"auto:Optane"'
                    }
                    post {
                        always {
                            functional_post_always()
                            archiveArtifacts artifacts: 'Functional/**'
                            junit 'Functional/*/results.xml, install/lib/daos/TESTING/ftest/*_results.xml'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    } // post
                } // stage('Functional_Hardware_Large')
            } // parallel
        } // stage('Test')
    } //stages
    post {
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    }
} // pipeline
