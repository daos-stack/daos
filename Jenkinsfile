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

// For master, this is just some wildly high number
String base_branch = 'master'
String next_version = base_branch

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

void fixup_rpmlintrc() {
    if (env.SCONS_FAULTS_ARGS != 'BUILD_TYPE=dev') {
        return
    }

    List go_bins = ['/usr/bin/dmg',
                    '/usr/bin/daos',
                    '/usr/bin/daos_agent',
                    '/usr/bin/hello_drpc',
                    '/usr/bin/daos_firmware',
                    '/usr/bin/daos_admin',
                    '/usr/bin/daos_server',
                    '/usr/bin/ddb']

    String content = readFile(file: 'utils/rpms/daos.rpmlintrc') + '\n\n' +
                     '# https://daosio.atlassian.net/browse/DAOS-11534\n'

    go_bins.each { bin ->
        content += 'addFilter("W: position-independent-executable-suggested ' + bin + '")\n'
    }

    writeFile(file: 'utils/rpms/daos.rpmlintrc', text: content)
}

String vm9_label(String distro) {
    return cachedCommitPragma(pragma: distro + '-VM9-label',
                              def_val: cachedCommitPragma(pragma: 'VM9-label',
                                                          def_val: params.FUNCTIONAL_VM_LABEL))
}

void rpm_test_post(String stage_name, String node) {
    sh label: 'Fetch and stage artifacts',
       script: 'hostname; ssh -i ci_key jenkins@' + node + ' ls -ltar /tmp; mkdir -p "' +  env.STAGE_NAME + '/" && ' +
               'scp -i ci_key jenkins@' + node + ':/tmp/{{suite_dmg,daos_{server_helper,{control,agent}}}.log,daos_server.log.*} "' +
               env.STAGE_NAME + '/"'
    archiveArtifacts artifacts: env.STAGE_NAME + '/**'
    job_status_update()
}

pipeline {
    agent { label 'lightweight' }

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
        string(name: 'BuildType',
               defaultValue: '',
               description: 'Type of build.  Passed to scons as BUILD_TYPE.  (I.e. dev, release, debug, etc.).  ' +
                            'Defaults to release on an RC or dev otherwise.')
        booleanParam(name: 'CI_BUILD_PACKAGES_ONLY',
                     defaultValue: false,
                     description: 'Only build RPM and DEB packages, Skip unit tests.')
        string(name: 'BaseBranch',
               defaultValue: base_branch,
               description: 'The base branch to run daily-testing against (i.e. master, or a PR\'s branch)')
        // TODO: add parameter support for per-distro CI_PR_REPOS
        string(name: 'CI_PR_REPOS',
               defaultValue: '',
               description: 'Additional repository used for locating packages for the build and ' +
                            'test nodes, in the project@PR-number[:build] format.')
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
        string(name: 'CI_UNIT_VM1_LABEL',
               defaultValue: 'ci_vm1',
               description: 'Label to use for 1 VM node unit and RPM tests')
        string(name: 'CI_UNIT_VM1_NVME_LABEL',
               defaultValue: 'ci_ssd_vm1',
               description: 'Label to use for 1 VM node unit tests that need NVMe')
        string(name: 'CI_NLT_1_LABEL',
               defaultValue: 'ci_nlt_1',
               description: 'Label to use for NLT tests')
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
        stage('Check PR') {
            when { changeRequest() }
            parallel {
                stage('Used Required Git Hooks') {
                    steps {
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS',
                                   message: 'PR did not get committed with required git hooks.  ' +
                                            'Please see utils/githooks/README.md.') {
                            sh 'if ! ' + cachedCommitPragma('Required-githooks', 'false') + '''; then
                                   echo 'PR did not get committed with required git hooks.  ' +
                                        'Please see utils/githooks/README.md.'
                                   exit 1
                                fi'''
                        }
                    }
                    post {
                        unsuccessful {
                            echo 'PR did not get committed with required git hooks.  ' +
                                 'Please see utils/githooks/README.md.'
                        }
                    }
                } // stage('Used Required Git Hooks')
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
    } // stages
    post {
        always {
            valgrindReportPublish valgrind_stashes: ['el8-gcc-nlt-memcheck',
                                                     'el8-gcc-unit-memcheck',
                                                     'fault-inject-valgrind']
            job_status_update('final_status')
            jobStatusWrite(job_status_internal)
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
