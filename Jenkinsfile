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
    (!env.BRANCH_NAME.startsWith('weekly-testing') &&
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
                    '/usr/bin/daos_server']

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
        booleanParam(name: 'CI_medium-md-on-ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium MD on SSD test stage')
        booleanParam(name: 'CI_medium-verbs-provider_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider test stage')
        booleanParam(name: 'CI_medium-verbs-provider-md-on-ssd_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider MD on SSD test stage')
        booleanParam(name: 'CI_medium-ucx-provider_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium UCX Provider test stage')
        booleanParam(name: 'CI_large_TEST',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large test stage')
        booleanParam(name: 'CI_large-md-on-ssd_TEST',
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
        stage('Get Commit Message') {
            steps {
                script {
                    env.COMMIT_MESSAGE = sh(script: 'git show -s --format=%B',
                                            returnStdout: true).trim()
                    Map pragmas = [:]
                    // can't use eachLine() here: https://issues.jenkins.io/browse/JENKINS-46988/
                    env.COMMIT_MESSAGE.split('\n').each { line ->
                        String key, value
                        try {
                            (key, value) = line.split(':', 2)
                            if (key.contains(' ')) {
                                return
                            }
                            pragmas[key.toLowerCase()] = value
                        /* groovylint-disable-next-line CatchArrayIndexOutOfBoundsException */
                        } catch (ArrayIndexOutOfBoundsException ignored) {
                            // ignore and move on to the next line
                        }
                    }
                    env.pragmas = pragmas
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
        stage('Pre-build') {
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
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
                        job_step_update(pythonBanditCheck())
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
                stage('Build RPM on EL 8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() + '--build-arg FVERSION=38'
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        job_step_update(buildRpm())
                    }
                    post {
                        success {
                            fixup_rpmlintrc()
                            buildRpmPost condition: 'success', rpmlint: true
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
                stage('Build RPM on EL 9') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() + '--build-arg FVERSION=38'
                            args '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        job_step_update(buildRpm())
                    }
                    post {
                        success {
                            fixup_rpmlintrc()
                            buildRpmPost condition: 'success', rpmlint: true
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
                stage('Build RPM on Leap 15.5') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        dockerfile {
                            filename 'packaging/Dockerfile.mockbuild'
                            dir 'utils/rpms'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs() + '--build-arg FVERSION=38'
                            args  '--cap-add=SYS_ADMIN'
                        }
                    }
                    steps {
                        job_step_update(buildRpm())
                    }
                    post {
                        success {
                            fixup_rpmlintrc()
                            buildRpmPost condition: 'success', rpmlint: true
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
                        job_step_update(buildRpm())
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
                stage('Build on Leap 15.5 with Intel-C and TARGET_PREFIX') {
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
                        job_step_update(
                            sconsBuild(parallel_build: true,
                                       scons_args: sconsFaultsArgs() +
                                                   ' PREFIX=/opt/daos TARGET_TYPE=release',
                                       build_deps: 'no'))
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
                stage('NLT on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_NLT_1_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     inst_repos: prRepos(),
                                     test_script: 'ci/unit/test_nlt.sh',
                                     unstash_opt: true,
                                     unstash_tests: false,
                                     inst_rpms: unitPackages()))
                        // recordCoverage(tools: [[parser: 'COBERTURA', pattern:'nltir.xml']],
                        //                 skipPublishingChecks: true,
                        //                 id: 'tlc', name: 'Fault Injection Interim Report')
                        stash(name:'nltr', includes:'nltr.json', allowEmpty: true)
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['nlt_logs/'],
                                         testResults: 'nlt-junit.xml',
                                         always_script: 'ci/unit/test_nlt_post.sh',
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
        stage('Test') {
            when {
                beforeAgent true
                expression { !skipStage() }
            }
            parallel {
                stage('Functional on EL 8.8 with Valgrind') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label params.CI_FUNCTIONAL_VM9_LABEL
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8.8 with Valgrind')
                stage('Functional on EL 8.8') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label vm9_label('EL8')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                    inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                                    test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8.8')
                stage('Functional on EL 9') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label vm9_label('EL9')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                    inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                                    test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 9')
                stage('Functional on Leap 15.5') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label vm9_label('Leap15')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } // post
                } // stage('Functional on Leap 15.5')
                stage('Functional on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { !skipStage() }
                    }
                    agent {
                        label vm9_label('Ubuntu')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } // post
                } // stage('Functional on Ubuntu 20.04')
                stage('Fault injection testing on EL 8.8') {
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
                        job_step_update(
                            sconsBuild(parallel_build: true,
                                       scons_args: 'PREFIX=/opt/daos TARGET_TYPE=release BUILD_TYPE=debug',
                                       build_deps: 'no'))
                        job_step_update(nlt_test())
                        // recordCoverage(tools: [[parser: 'COBERTURA', pattern:'nltr.xml']],
                        //                skipPublishingChecks: true,
                        //                id: 'fir', name: 'Fault Injection Report')
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
                            stash name: 'fault-inject-valgrind',
                                  includes: '*.memcheck.xml',
                                  allowEmpty: true
                            archiveArtifacts artifacts: 'nlt_logs/el8.fault-injection/'
                            job_status_update()
                        }
                    }
                } // stage('Fault inection testing on EL 8.8')
                // stage('Test RPMs on EL 8.6') {
                //     when {
                //         beforeAgent true
                //         expression { ! skipStage() }
                //     }
                //     agent {
                //         label params.CI_UNIT_VM1_LABEL
                //     }
                //     steps {
                //         job_step_update(
                //             testRpm(inst_repos: daosRepos(),
                //                     daos_pkg_version: daosPackagesVersion(next_version))
                //         )
                //     }
                //     post {
                //         always {
                //             rpm_test_post(env.STAGE_NAME, env.NODELIST)
                //         }
                //     }
                // } // stage('Test CentOS 7 RPMs')
                // stage('Test RPMs on Leap 15.4') {
                //     when {
                //         beforeAgent true
                //         expression { ! skipStage() }
                //     }
                //     agent {
                //         label params.CI_UNIT_VM1_LABEL
                //     }
                //     steps {
                //         /* neither of these work as FTest strips the first node
                //            out of the pool requiring 2 node clusters at minimum
                //          * additionally for this use-case, can't override
                //            ftest_arg with this :-(
                //         script {
                //             'Test RPMs on Leap 15.4': getFunctionalTestStage(
                //                 name: 'Test RPMs on Leap 15.4',
                //                 pragma_suffix: '',
                //                 label: params.CI_UNIT_VM1_LABEL,
                //                 next_version: next_version,
                //                 stage_tags: '',
                //                 default_tags: 'test_daos_management',
                //                 nvme: 'auto',
                //                 run_if_pr: true,
                //                 run_if_landing: true,
                //                 job_status: job_status_internal
                //             )
                //         }
                //            job_step_update(
                //             functionalTest(
                //                 test_tag: 'test_daos_management',
                //                 ftest_arg: '--yaml_extension single_host',
                //                 inst_repos: daosRepos(),
                //                 inst_rpms: functionalPackages(1, next_version, 'tests-internal'),
                //                 test_function: 'runTestFunctionalV2'))
                //     }
                //     post {
                //         always {
                //             functionalTestPostV2()
                //             job_status_update()
                //         }
                //     } */
                //         job_step_update(
                //             testRpm(inst_repos: daosRepos(),
                //                     daos_pkg_version: daosPackagesVersion(next_version))
                //         )
                //     }
                //     post {
                //         always {
                //             rpm_test_post(env.STAGE_NAME, env.NODELIST)
                //         }
                //     }
                // } // stage('Test Leap 15 RPMs')
            } // parallel
        } // stage('Test')
        stage('Test Storage Prep on EL 8.8') {
            when {
                beforeAgent true
                expression { params.CI_STORAGE_PREP_LABEL != '' }
            }
            agent {
                label params.CI_STORAGE_PREP_LABEL
            }
            steps {
                job_step_update(
                    storagePrepTest(
                        inst_repos: daosRepos(),
                        inst_rpms: functionalPackages(1, next_version, 'tests-internal')))
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
            steps {
                script {
                    parallel(
                        'Functional Hardware Medium': getFunctionalTestStage(
                            name: 'Functional Hardware Medium',
                            pragma_suffix: '-hw-medium',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium MD on SSD',
                            pragma_suffix: '-hw-medium-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ?
                                'pr,md_on_ssd daily_regression,md_on_ssd' : 'pr,md_on_ssd',
                            nvme: 'auto_md_on_ssd',
                            run_if_pr: false,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium Verbs Provider': getFunctionalTestStage(
                            name: 'Functional Hardware Medium Verbs Provider',
                            pragma_suffix: '-hw-medium-verbs-provider',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            provider: 'ofi+verbs;ofi_rxm',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium Verbs Provider MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium Verbs Provider MD on SSD',
                            pragma_suffix: '-hw-medium-verbs-provider-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ?
                                'pr,md_on_ssd daily_regression,md_on_ssd' : 'pr,md_on_ssd',
                            default_nvme: 'auto_md_on_ssd',
                            provider: 'ofi+verbs;ofi_rxm',
                            run_if_pr: false,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium UCX Provider': getFunctionalTestStage(
                            name: 'Functional Hardware Medium UCX Provider',
                            pragma_suffix: '-hw-medium-ucx-provider',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_UCX_PROVIDER_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            provider: 'ucx+dc_x',
                            run_if_pr: false,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large': getFunctionalTestStage(
                            name: 'Functional Hardware Large',
                            pragma_suffix: '-hw-large',
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            run_if_pr: true,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large MD on SSD',
                            pragma_suffix: '-hw-large-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            next_version: next_version,
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto_md_on_ssd',
                            run_if_pr: false,
                            run_if_landing: false,
                            job_status: job_status_internal
                        ),
                    )
                }
            }
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
                                coverage_stashes: ['el8-covc-unit-cov',
                                                   'func-vm-cov',
                                                   'func-hw-medium-cov',
                                                   'func-hw-large-cov'],
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
