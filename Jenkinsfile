#!/usr/bin/groovy
/* groovylint-disable-next-line LineLength */
/* groovylint-disable DuplicateMapLiteral, DuplicateNumberLiteral */
/* groovylint-disable DuplicateStringLiteral, NestedBlockDepth, VariableName */
/* Copyright 2019-2023 Intel Corporation
/* Copyright 2026 Hewlett Packard Enterprise Development LP
 * All rights reserved.
 *
 * This file is part of the DAOS Project. It is subject to the license terms
 * in the LICENSE file found in the top-level directory of this distribution
 * and at https://img.shields.io/badge/License-BSD--2--Clause--Patent-blue.svg.
 * No part of the DAOS Project, including this file, may be copied, modified,
 * propagated, or distributed except according to the terms contained in the
 * LICENSE file.
 */

import groovy.transform.Field

// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
@Library(value='pipeline-lib@hendersp/DAOS-18348-2') _

// Name of branch to be tested
test_branch = 'release/2.6'

/* groovylint-disable-next-line CompileStatic */
job_status_internal = [:]

// Keys and values updated by the updateRunStage() function using the parameters.
@Field
Map<String, Boolean> runStage = [:]

String bashName(String name) {
    return name.replaceAll('[^a-zA-Z0-9]', '_')
}

// Update the runStage map
void updateRunStage() {
    Map reasons = [:]

    // Ordered list of stage names as params.keySet() does not guarantee order
    List<String> stageOrder = [
        'Cancel Previous Builds',
        'Test',
        'Functional on EL 8',
        'Functional on EL 9',
        'Functional on Leap 15',
        'Functional on Ubuntu 20.04',
        'Functional Hardware Medium',
        'Functional Hardware Medium MD on SSD',
        'Functional Hardware Medium VMD',
        'Functional Hardware Large',
        'Functional Hardware Large MD on SSD',
        'Functional Hardware Medium TCP',
        'Functional Hardware Medium TCP Provider',
        'Functional Hardware Large TCP',
        'Functional Hardware Medium UCX',
        'Functional Hardware Large UCX',
    ]

    // Initialize the run state of each stage using the parameter stage keys
    for (name in stageOrder) {
        value = params.get(bashName(name), null)
        if (value instanceof Boolean && !name.startsWith('CI_')) {
            runStage[name] = value
            reasons[name] = "parameter selection or default"
        }
    }

    // Debug
    String buildCause = currentBuild.getBuildCauses().toString()
    println("updateRunStage: Build cause: ${buildCause}")
    println("updateRunStage: Started by user: ${startedByUser()}")

    // Handle landing builds
    if (startedByLanding()) {
        println("updateRunStage: Detected landing build, overwriting defaults")
        for (stage in runStage.keySet()) {
            if (stage in ['Test', 'Functional on EL 9']) {
                runStage[stage] = true
            } else {
                runStage[stage] = false
            }
            reasons[stage] = "landing build"
        }
        displayRunStage(reasons)
        return
    }

    // Handle user setting CI_IGNORE_SKIP_COMMIT_PRAGMAS
    if (params.CI_IGNORE_SKIP_COMMIT_PRAGMAS) {
        println(
            "updateRunStage: Detected CI_IGNORE_SKIP_COMMIT_PRAGMAS, ignoring skip commit pragmas")
        displayRunStage(reasons)
        return
    }

    // Update stage running based on commit pragmas
    println("updateRunStage: Converting env.pragmas string back into a Map: ${env.pragmas}")
    Map<String, String> commitPragmas = envToPragmas()
    println("updateRunStage: Checking skip commit pragmas from commit message:")
    commitPragmas.each { key, value ->
        println("  ${key}: ${value}")
    }
    for (stage in runStage.keySet()) {
        List<String> skipPragmas = getStageNameSkipPragmas(stage)
        for (pragma in skipPragmas) {
            // commitPragmas will already contain lower case keys from pragmasToMap()
            println("updateRunStage: ${stage} checking for a ${pragma} commit pragma")
            if (commitPragmas.get(pragma, '').toLowerCase() == 'true') {
                runStage[stage] = false
                reasons[stage] = "commit pragma ${pragma}: true"
                break
            } else if (commitPragmas.get(pragma, '').toLowerCase() == 'false') {
                runStage[stage] = true
                reasons[stage] = "commit pragma ${pragma}: false"
                break
            }
        }
    }

    displayRunStage(reasons)
}

// Log which stages will be run and why based on the current state of the runStage map
void displayRunStage(Map reasons = [:]) {
    println("Stage run conditions:")
    for (stage in runStage.keySet()) {
        String reason = reasons.get(stage, 'default')
        if (runStage[stage]) {
            echo("Running:   ${stage} (reason: ${reason})")
        } else {
            echo("Skipping:  ${stage} (reason: ${reason})")
        }
    }
}

// Get a list of skip commit pragmas to check for a given stage name
List<String> getStageNameSkipPragmas(String stageName) {
    String stagePragma = "skip-${stageName.replaceAll(' ', '-').toLowerCase()}"
    List<String> pragmas = []

    // Build up a priority list of pragmas to check based on the stage name.
    if (stageName in ['Cancel Previous Builds']) {
        // Add skip pragma for this stage
        pragmas.add(stagePragma)

    } else if (stageName == 'Test' || stageName.contains('Functional')) {
        // Add skip pragma for parent stage
        if (stageName != 'Test') {
            pragmas.add('skip-test')
        }
        if (stageName.contains('Functional on')) {
            // Add skip pragma alias for all functional tests
            pragmas.add('skip-functional')
            pragmas.add('skip-functional-test')
            // Add skip pragma alias for all functional VM tests
            pragmas.add('skip-functional-test-vm')
            pragmas.add('skip-functional-vm-test')
            // Compatibility with existing commit pragmas
            pragmas.add(stagePragma.replace('functional-on-', 'functional-test-'))
        }
        if (stageName.contains('Functional Hardware')) {
            // Add skip pragma alias for all functional tests
            pragmas.add('skip-functional')
            pragmas.add('skip-functional-test')
            // Add skip pragma alias for all functional HW tests
            pragmas.add('skip-functional-test-hardware')
            pragmas.add('skip-functional-hardware-test')
            // Compatibility with existing commit pragmas
            pragmas.add(stagePragma.replace('functional-hardware-', 'functional-hardware-test-'))
        }
        // Add skip pragma for this stage
        pragmas.add(stagePragma)
    }

    // Compatibility with existing commit pragmas using distro versions
    List<String> distros = ['el', 'leap', 'sles', 'ubuntu']
    List<String> copyPragmas = pragmas.clone()
    for (distro in distros) {
        for (_pragma in copyPragmas) {
            if (_pragma.contains("-${distro}-")) {
                Integer _index = pragmas.indexOf(_pragma)
                pragmas.add(_index + 1, _pragma.replace("-${distro}-", "-${distro}"))
            }
        }
    }

    // Compatibility with existing commit pragmas using shortened func or hw
    copyPragmas = pragmas.clone()
    for (_pragma in copyPragmas) {
         if (_pragma.contains('-functional') || _pragma.contains('-hardware')) {
            Integer _index = pragmas.indexOf(_pragma)
            String _compat_pragma = _pragma.replace('-functional', '-func')
            _compat_pragma = _compat_pragma.replace('-hardware', '-hw')
            pragmas.add(_index + 1, _compat_pragma)
        }
    }

    return pragmas
}

// Initialize the runStage map with the current state of the build parameters and any commit
// pragmas related to skipping/running stages. Should only be called once per build.
def setupRunStage() {
    pragmasToEnv()
    updateRunStage()
}

// Determine if a given stage should be run based on the current state of the runStage map.
// Ensure the runStage map is initialized before checking the stage state - required to support
// the Jenkins Restart from Stage option.
Boolean shouldStageRun(String name) {
    if (!runStage) {
        setupRunStage()
    }
    return runStage[name]
}

// groovylint-disable-next-line MethodParameterTypeRequired, NoDef
void job_status_update(String name=env.STAGE_NAME, def value=currentBuild.currentResult) {
    jobStatusUpdate(job_status_internal, name, value)
}

// groovylint-disable-next-line MethodParameterTypeRequired, NoDef
void job_step_update(def value=currentBuild.currentResult) {
    // job_status_update(env.STAGE_NAME, value)
    jobStatusUpdate(job_status_internal, env.STAGE_NAME, value)
}

// Don't define this as a type or it loses it's global scope
target_branch = env.CHANGE_TARGET ? env.CHANGE_TARGET : env.BRANCH_NAME
String sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    !(env.BRANCH_NAME =~ branchTypeRE('testing') ||
      env.BRANCH_NAME =~ branchTypeRE('release') ||
      env.BRANCH_NAME =~ branchTypeRE('downstream') ||
      env.BRANCH_NAME == 'master')) {
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
    return cachedCommitPragma(
        pragma: distro + '-VM9-label',
        def_val: cachedCommitPragma(pragma: 'VM9-label', def_val: params.FUNCTIONAL_VM_LABEL))
}

// Get the default tags to use for a stage based on the current build type
String defaultTags(String timedTags, String prTags = 'always_passes') {
    /* groovylint-disable-next-line UnnecessaryGetter */
    if (isPr()) {
        return prTags
    }
    return timedTags
}

pipeline {
    agent { label 'lightweight' }

    // Timed builds have been disabled
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
        // The TestNvme and TestRepeat parameter definitions are purposely excluded. The functional
        // test stage launch.py --nvme argument is hard-coded in each stage definition to avoid the
        // stages from duplicating testing.
        string(name: 'TestProvider',
               defaultValue: '',
               description: 'Test-provider to use for the non-Provider Functional Hardware test ' +
                            'stages.  Specifies the default provider to use the daos_server ' +
                            'config file when running functional tests (the launch.py ' +
                            '--provider argument; i.e. "ucx+dc_x", "ofi+verbs", "ofi+tcp")')
        string(name: 'TestProviderTCP',
               defaultValue: 'ofi+tcp',
               description: 'Provider to use for the Functional Hardware Medium/Large TCP stages ' +
                            'of this run (i.e. ofi+tcp)')
        string(name: 'TestProviderUCX',
               defaultValue: 'ucx+dc_x',
               description: 'Provider to use for the Functional Hardware Medium/Large UCX stages ' +
                            'of this run (i.e. ucx+ud_x, ucx+dc_x)')
        string(name: 'CI_RPM_TEST_VERSION',
               defaultValue: '',
               description: 'Package version to use instead of latest. example: 1.3.103-1, 1.2-2')
        string(name: 'BaseBranch',
               defaultValue: test_branch,
               description: 'The base branch to run daily-testing against (i.e. master, or a PR\'s branch)')
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
        booleanParam(name: 'CI_IGNORE_SKIP_COMMIT_PRAGMAS',
                     defaultValue: false,
                     description: 'Ignore any commit pragmas used to skip/run stages and rely ' +
                                  'solely on the build parameter settings')
        booleanParam(name: bashName('Cancel Previous Builds'),
                     defaultValue: true,
                     description: 'Run the Cancel Previous Builds stage.')
        booleanParam(name: bashName('Test'),
                     defaultValue: true,
                     description: 'Run the Test stage.')
        booleanParam(name: bashName('Functional on EL 8'),
                     defaultValue: true,
                     description: 'Run the Functional on EL 8 stage.')
        booleanParam(name: bashName('Functional on EL 9'),
                     defaultValue: true,
                     description: 'Run the Functional on EL 9 stage.')
        booleanParam(name: bashName('Functional on Leap 15'),
                     defaultValue: true,
                     description: 'Run the Functional on Leap 15 stage.')
        booleanParam(name: bashName('Functional on Ubuntu 20.04'),
                     defaultValue: false,
                     description: 'Run the Functional on Ubuntu 20.04 stage.')
        booleanParam(name: bashName('Functional Hardware Medium'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium stage.')
        booleanParam(name: bashName('Functional Hardware Medium MD on SSD'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium MD on SSD stage.')
        booleanParam(name: bashName('Functional Hardware Medium VMD'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium VMD stage.')
        booleanParam(name: bashName('Functional Hardware Large'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large stage.')
        booleanParam(name: bashName('Functional Hardware Large MD on SSD'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large MD on SSD stage.')
        booleanParam(name: bashName('Functional Hardware Medium TCP'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium TCP stage.')
        booleanParam(name: bashName('Functional Hardware Medium TCP Provider'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium TCP Provider stage.')
        booleanParam(name: bashName('Functional Hardware Large TCP'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large TCP stage.')
        booleanParam(name: bashName('Functional Hardware Medium UCX'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium UCX stage.')
        booleanParam(name: bashName('Functional Hardware Large UCX'),
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large UCX stage.')
        string(name: 'FUNCTIONAL_VM_LABEL',
               defaultValue: 'ci_vm9',
               description: 'Label to use for 9 VM functional tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VMD_LABEL',
               defaultValue: 'ci_vmd5',
               description: 'Label to use for the Functional Hardware Medium VMD stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large tests')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_MD_ON_SSD_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for the Functional Hardware Large MD on SSD stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_TCP_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium TCP stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_TCP_PROVIDER_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for 5 node Functional Hardware Medium TCP Provider stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_TCP_LABEL',
               defaultValue: 'ci_nvme9',
               description: 'Label to use for 9 node Functional Hardware Large TCP stage')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_UCX_LABEL',
               defaultValue: 'ci_ofed5',
               description: 'Label to use for 5 node Functional Hardware Medium UCX stage')
        string(name: 'FUNCTIONAL_HARDWARE_LARGE_UCX_LABEL',
               defaultValue: 'ci_ofed9',
               description: 'Label to use for 9 node Functional Hardware Large UCX stage')
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
        stage('Prepare') {
            parallel {
                stage('Set Description') {
                    steps {
                        script {
                            if (params.CI_BUILD_DESCRIPTION) {
                                buildDescription params.CI_BUILD_DESCRIPTION
                            }
                        }
                    }
                }
                stage('Setup Stages') {
                    steps {
                        setupRunStage()
                    }
                }
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
            }
        }
        stage('Cancel Previous Builds') {
            when {
                beforeAgent true
                expression { shouldStageRun('Cancel Previous Builds') }
            }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                expression { shouldStageRun('Test') }
            }
            steps {
                script {
                    // Note: The functional test steps define 'nvme' instead of 'default_nvme' to
                    // force the launch.py --nvme argument.  This means the 'Test-nvme' commit
                    // pragmas will be ignored. This is to avoid multiple parallel test stages
                    // from duplicating testing.
                    parallel(
                        'Functional on EL 8': getFunctionalTestStage(
                            name: 'Functional on EL 8',
                            runStage: shouldStageRun('Functional on EL 8'),
                            pragma_suffix: '-vm',
                            distro: 'el8',
                            image_version: 'el8.10',
                            base_branch: params.BaseBranch,
                            label: vm9_label('EL8'),
                            next_version: params.BaseBranch,
                            stage_tags: 'vm',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional on EL 9': getFunctionalTestStage(
                            name: 'Functional on EL 9',
                            runStage: shouldStageRun('Functional on EL 9'),
                            pragma_suffix: '-vm',
                            distro: 'el9',
                            image_version: 'el9.7',
                            base_branch: params.BaseBranch,
                            label: vm9_label('EL9'),
                            next_version: params.BaseBranch,
                            stage_tags: 'vm',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional on Leap 15': getFunctionalTestStage(
                            name: 'Functional on Leap 15',
                            runStage: shouldStageRun('Functional on Leap 15'),
                            pragma_suffix: '-vm',
                            distro: 'leap15',
                            image_version: 'leap15.6',
                            rpm_distro: '.suse.lp155',
                            base_branch: params.BaseBranch,
                            label: vm9_label('Leap15'),
                            next_version: params.BaseBranch,
                            stage_tags: 'vm',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional on Ubuntu 20.04': getFunctionalTestStage(
                            name: 'Functional on Ubuntu 20.04',
                            runStage: shouldStageRun('Functional on Ubuntu 20.04'),
                            pragma_suffix: '-vm',
                            distro: 'ubuntu20',
                            base_branch: params.BaseBranch,
                            label: vm9_label('Ubuntu'),
                            next_version: params.BaseBranch,
                            stage_tags: 'vm',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium': getFunctionalTestStage(
                            name: 'Functional Hardware Medium',
                            runStage: shouldStageRun('Functional Hardware Medium'),
                            pragma_suffix: '-hw-medium',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium MD on SSD',
                            runStage: shouldStageRun('Functional Hardware Medium MD on SSD'),
                            pragma_suffix: '-hw-medium-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto_md_on_ssd',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium VMD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium VMD',
                            runStage: shouldStageRun('Functional Hardware Medium VMD'),
                            pragma_suffix: '-hw-medium-vmd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VMD_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw_vmd,medium',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large': getFunctionalTestStage(
                            name: 'Functional Hardware Large',
                            runStage: shouldStageRun('Functional Hardware Large'),
                            pragma_suffix: '-hw-large',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,large',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large MD on SSD',
                            runStage: shouldStageRun('Functional Hardware Large MD on SSD'),
                            pragma_suffix: '-hw-large-md-on-ssd',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_MD_ON_SSD_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,large',
                            default_tags: defaultTags('full_regression'),
                            nvme: 'auto_md_on_ssd',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium TCP': getFunctionalTestStage(
                            name: 'Functional Hardware Medium TCP',
                            runStage: shouldStageRun('Functional Hardware Medium TCP'),
                            pragma_suffix: '-hw-medium-tcp',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_TCP_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: defaultTags('pr daily_regression'),
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-tcp', params.TestProviderTCP),
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium TCP Provider': getFunctionalTestStage(
                            name: 'Functional Hardware Medium TCP Provider',
                            runStage: shouldStageRun('Functional Hardware Medium TCP Provider'),
                            pragma_suffix: '-hw-medium-tcp-provider',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_TCP_PROVIDER_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,medium,provider',
                            default_tags: defaultTags('pr daily_regression'),
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-tcp', params.TestProviderTCP),
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large TCP': getFunctionalTestStage(
                            name: 'Functional Hardware Large TCP',
                            runStage: shouldStageRun('Functional Hardware Large TCP'),
                            pragma_suffix: '-hw-large-tcp',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_TCP_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,large',
                            default_tags: defaultTags('pr daily_regression'),
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-tcp', params.TestProviderTCP),
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Medium UCX': getFunctionalTestStage(
                            name: 'Functional Hardware Medium UCX',
                            runStage: shouldStageRun('Functional Hardware Medium UCX'),
                            pragma_suffix: '-hw-medium-ucx',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_UCX_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,medium,-provider',
                            default_tags: defaultTags('pr daily_regression'),
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-ucx', params.TestProviderUCX),
                            other_packages: 'mercury-ucx',
                            job_status: job_status_internal
                        ),
                        'Functional Hardware Large UCX': getFunctionalTestStage(
                            name: 'Functional Hardware Large UCX',
                            runStage: shouldStageRun('Functional Hardware Large UCX'),
                            pragma_suffix: '-hw-large-ucx',
                            base_branch: params.BaseBranch,
                            label: params.FUNCTIONAL_HARDWARE_LARGE_UCX_LABEL,
                            next_version: params.BaseBranch,
                            stage_tags: 'hw,large',
                            default_tags: defaultTags('pr daily_regression'),
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-ucx', params.TestProviderUCX),
                            other_packages: 'mercury-ucx',
                            job_status: job_status_internal
                        ),
                    )
                }
            }
        } // stage('Test')
    } //stages
    post {
        always {
            job_status_update('final_status')
            jobStatusWrite(job_status_internal)
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
