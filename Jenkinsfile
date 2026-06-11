#!/usr/bin/groovy
/* groovylint-disable-next-line LineLength */
/* groovylint-disable DuplicateMapLiteral, DuplicateNumberLiteral */
/* groovylint-disable DuplicateStringLiteral, NestedBlockDepth */
/* groovylint-disable ParameterName, VariableName */
/* Copyright 2019-2024 Intel Corporation
/* Copyright 2025 Google LLC
 * Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
import org.jenkinsci.plugins.pipeline.modeldefinition.Utils

// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value='pipeline-lib@your_branch') _
@Library(value='pipeline-lib@hendersp/DAOS-18348') _

// The trusted-pipeline-lib daosLatestVersion() method will convert this into a number
/* groovylint-disable-next-line CompileStatic, VariableName */
String next_version() {
    return 'release/2.8'
}

/* groovylint-disable-next-line CompileStatic */
job_status_internal = [:]

// Keys and values updated by the updateRunStage() function using the parameters.
@Field
Map<String, Boolean> runStage = [:]

// Update the runStage map
void updateRunStage() {
    Map reasons = [:]

    // Ordered list of stage names as params.keySet() does not guarantee order
    List<String> stageOrder = [
        'Cancel Previous Builds',
        'Pre-build',
        'Python Bandit check',
        'Build',
        'Build on EL 8',
        'Build on EL 9',
        'Build on Leap 15',
        'Build on EL 9 with Bullseye',
        'Unit Tests',
        'Unit Test',
        'Unit Test bdev',
        'NLT',
        'NLT with Bullseye',
        'Unit Test with memcheck',
        'Unit Test bdev with memcheck',
        'Test',
        'Functional on EL 8.8 with Valgrind',
        'Functional on EL 8',
        'Functional on EL 9',
        'Functional on Leap 15',
        'Functional on SLES 15',
        'Functional on Ubuntu 20.04',
        'Fault injection testing',
        'Test RPMs on EL 9.6',
        'Test RPMs on Leap 15.5',
        'Test Hardware',
        'Functional Hardware Medium',
        'Functional Hardware Medium MD on SSD',
        'Functional Hardware Medium VMD',
        'Functional Hardware Medium Verbs Provider',
        'Functional Hardware Medium Verbs Provider MD on SSD',
        'Functional Hardware Medium UCX Provider',
        'Functional Hardware Large',
        'Functional Hardware Large MD on SSD'
    ]

    // Initialize the run state of each stage using the parameter stage keys
    for (name in stageOrder) {
        value = params.get(name, null)
        if (value instanceof Boolean && !name.startsWith('CI_')) {
            runStage[name] = value
            reasons[name] = "parameter selection or default"
        }
    }

    // Handle doc-only changes: Only run default or selected build stages
    if (docOnlyChange(target_branch)) {
        println("updateRunStage: Detected doc-only change, skipping testing")
        for (stage in runStage.keySet()) {
            if (stage in ['Unit Tests', 'Test', 'Test Hardware']) {
                runStage[stage] = false
                reasons[stage] = "doc-only change"
            }
        }
        displayRunStage(reasons)
        return
    }

    // Handle landing builds
    if (startedByLanding()) {
        println("updateRunStage: Detected landing build, overwriting defaults")
        for (stage in runStage.keySet()) {
            runStage[stage] = false
            if (stage in ['Pre-build', 'Python Bandit check', 'Build', 'Unit Tests', 'Test']
                    || stage.contains('Build on')
                    || stage.contains('Unit Test')
                    || stage.contains('NLT')
                    || stage.contains('Fault injection')
                    || stage.contains('Test RPMs')
                    || (stage.contains('Functional on') && !stage.contains('Ubuntu'))) {
                runStage[stage] = true
            }
            reasons[stage] = "landing build"
        }
        displayRunStage(reasons)
        return
    }

    // Handle user setting CI_RPM_TEST_VERSION or RPM-test-version
    if (rpmTestVersion()) {
        println("updateRunStage: Detected RPM test version, skipping build/RPM test stages")
        for (stage in runStage.keySet()) {
            if (stage.contains('Build')
                    || stage.contains('Unit Tests')
                    || stage.contains('Test RPMs')) {
                runStage[stage] = false
                reasons[stage] = "RPM test version"
            }
        }
        displayRunStage(reasons)
        return
    }

    // Handle user setting CI_FULL_BULLSEYE_REPORT
    if (params.CI_FULL_BULLSEYE_REPORT) {
        println("updateRunStage: Detected CI_FULL_BULLSEYE_REPORT, skipping unrelated stages")
        for (stage in runStage.keySet()) {
            if (stage in ['Build on EL 9 with Bullseye',
                          'Unit Test',
                          'Unit Test bdev',
                          'NLT with Bullseye',
                          'Functional on EL 9']) {
                runStage[stage] = true
                reasons[stage] = "CI_FULL_BULLSEYE_REPORT"
            } else if (stage.contains('Functional Hardware')) {
                runStage[stage] = true
                reasons[stage] = "CI_FULL_BULLSEYE_REPORT"
            } else if (stage in ['Build on EL 8',
                                 'Build on EL 9',
                                 'Build on Leap 15',
                                 'NLT',
                                 'Unit Test with memcheck',
                                 'Unit Test bdev with memcheck',
                                 'Functional on EL 8.8 with Valgrind',
                                 'Functional on EL 8',
                                 'Functional on Leap 15',
                                 'Functional on SLES 15',
                                 'Functional on Ubuntu 20.04',
                                 'Fault injection testing',
                                 'Test RPMs on EL 9.6',
                                 'Test RPMs on Leap 15.5']) {
                runStage[stage] = false
                reasons[stage] = "CI_FULL_BULLSEYE_REPORT"
            }
        }
        displayRunStage(reasons)
        return
    }

    // Handle user setting CI_BUILD_PACKAGES_ONLY
    if (params.CI_BUILD_PACKAGES_ONLY) {
        println("updateRunStage: Detected CI_BUILD_PACKAGES_ONLY, skipping unit test stages")
        for (stage in runStage.keySet()) {
            if (stage.contains('Unit Tests')) {
                runStage[stage] = false
                reasons[stage] = "CI_BUILD_PACKAGES_ONLY"
            } else if (stage.contains('Build')) {
                runStage[stage] = true
                reasons[stage] = "CI_BUILD_PACKAGES_ONLY"
            }
        }
        displayRunStage(reasons)
        return
    }

    // Handle builds started by the user
    if (startedByUser()) {
        println("updateRunStage: Build started by the user, skipping commit pragma checks")
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
    if (stageName in ['Cancel Previous Builds', 'Pre-build']) {
        // Add skip pragma for this stage
        pragmas.add(stagePragma)

    } else if (stageName == 'Python Bandit check') {
        // Add skip pragma for this stage
        pragmas.add(stagePragma)
        // Compatibility with existing commit pragmas
        pragmas.add(stagePragma.replace('-bandit-check', '-bandit'))

    } else if (stageName.contains('Build')) {
        // Add skip pragma for parent stage
        if (stageName != 'Build') {
            pragmas.add('skip-build')
        }
        // Add skip pragma for this stage
        pragmas.add(stagePragma)
        // Compatibility with existing commit pragmas
        if (stagePragma.contains('build-on-')) {
            pragmas.add(stagePragma.replace('build-on-', 'build-'))
        }

    } else if (stageName.contains('Unit Test') || stageName.contains('NLT')) {
        // Add skip pragma for parent stage
        if (stageName != 'Unit Tests') {
            pragmas.add('skip-unit-tests')
        }
        // Add skip pragma for this stage
        pragmas.add(stagePragma)
        // Compatibility with existing commit pragmas
        if (stagePragma.contains('-with-')) {
            pragmas.add(stagePragma.replace('-with-', '-'))
        }

    } else if (stageName == 'Test' || stageName.contains('Functional on')
            || stageName.contains('Fault injection') || stageName.contains('Test RPMs')) {
        // Add skip pragma for parent stage
        if (stageName != 'Test') {
            pragmas.add('skip-test')
        }
        if (stageName.contains('Functional on')) {
            // Add skip pragma alias for all functional tests
            pragmas.add('skip-func-test')
            // Add skip pragma alias for all functional VM tests
            pragmas.add('skip-func-test-vm')
            pragmas.add('skip-func-vm-test')
            // Compatibility with existing commit pragmas
            pragmas.add(stagePragma.replace('functional-on-', 'func-test-'))
        } else if (stageName.contains('Test RPMs on')) {
            // Add skip pragma alias for all RPM tests
            pragmas.add('skip-test-rpms')
        } else if (stageName.contains('Fault injection')) {
            // Compatibility with existing commit pragmas
            pragmas.add('skip-fault-injection-test')
        }
        // Add skip pragma for this stage
        pragmas.add(stagePragma)

    } else if (stageName.contains('Hardware')) {
        if (stageName != 'Test Hardware') {
            pragmas.add('skip-test-hardware')
            pragmas.add('skip-test-hw')
        }
        if (stageName.contains('Functional')) {
            // Add skip pragma alias for all functional tests
            pragmas.add('skip-func-test')
            // Add skip pragma alias for all functional HW tests
            pragmas.add('skip-func-test-hw')
            pragmas.add('skip-func-hw-test')
            // Compatibility with existing commit pragmas
            if (stagePragma.contains('functional-hardware-')) {
                pragmas.add(stagePragma.replace('functional-hardware-', 'func-hw-test-'))
                pragmas.add(stagePragma.replace('functional-hardware-', 'func-hw-'))
            }
        }
        // Add skip pragma for this stage
        pragmas.add(stagePragma)

        // Support shortening hardware to hw
        if (stagePragma.contains('hardware-')) {
            pragmas.add(stagePragma.replace('hardware-', 'hw-'))
        }
    }

    // Compatibility with existing commit pragmas using distro versions
    List<String> distros = ['el', 'leap', 'sles', 'ubuntu']
    List<String> copyPragmas = pragmas.clone()
    for (distro in distros) {
        for (_pragma in copyPragmas) {
            if (_pragma.contains("-${distro}-")) {
                pragmas.add(_pragma.replace("-${distro}-", "-${distro}"))
            }
        }
    }

    return pragmas
}

void get_rpm_relval() {
    env.DAOS_RELVAL = sh(label: 'get git tag',
               script: '''if [ -n "$GIT_CHECKOUT_DIR" ] && [ -d "$GIT_CHECKOUT_DIR" ]; then
                              cd "$GIT_CHECKOUT_DIR"
                          fi
                          if git diff-index --name-only HEAD^ | grep -q TAG; then
                              echo ""
                          else
                              echo ".$(git rev-list HEAD --count).g$(git rev-parse --short=8 HEAD)"
                          fi''',
                returnStdout: true).trim()
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
                    '/usr/bin/daos_server',
                    '/usr/bin/ddb']

    String content = readFile(file: 'utils/rpms/daos.rpmlintrc') + '\n\n' +
                     '# https://daosio.atlassian.net/browse/DAOS-11534\n'

    go_bins.each { bin ->
        content += 'addFilter("W: position-independent-executable-suggested ' + bin + '")\n'
    }

    writeFile(file: 'utils/rpms/daos.rpmlintrc', text: content)
}

void uploadNewRPMs(String target, String stage) {
    buildRpmPost target: target,
                 condition: stage,
                 rpmlint: false,
                 productArtifacts: ['daos', 'deps', 'bullseye']
}

String vm9_label(String distro) {
    return cachedCommitPragma(pragma: distro + '-VM9-label',
                              def_val: cachedCommitPragma(pragma: 'VM9-label',
                                                          def_val: params.FUNCTIONAL_VM_LABEL))
}

void rpm_test_post(String stageName, String node) {
    // Extract first node from comma-delimited list
    String firstNode = node.split(',')[0].trim()
    sh label: 'Fetch and stage artifacts',
       script: 'hostname; ssh -i ci_key jenkins@' + firstNode +
               ' ls -ltar /tmp; mkdir -p "' +  env.STAGE_NAME + '/" && ' +
               'scp -i ci_key jenkins@' + firstNode +
               ':/tmp/{{suite_dmg,daos_{server_helper,{control,agent}}}.log,daos_server.log.*} "' +
               stageName + '/"'
    archiveArtifacts artifacts: env.STAGE_NAME + '/**'
    job_status_update()
}

String sconsArgs() {
    if (!params.CI_SCONS_ARGS) {
        return sconsFaultsArgs()
    }

    println('Compiling DAOS with custom arguments')
    return sconsFaultsArgs() + ' ' + params.CI_SCONS_ARGS
}

/**
 * Update default commit pragmas based on files modified.
 */
Map update_default_commit_pragmas() {
    String default_pragmas_str = sh(script: 'ci/gen_commit_pragmas.py --target origin/' + target_branch,
                                    returnStdout: true).trim()
    println('pragmas from gen_commit_pragmas.py:')
    println(default_pragmas_str)
    if (default_pragmas_str) {
        updatePragmas(default_pragmas_str, false)
    }
}

/**
 * getScriptOutput
 *
 * Run a script and return the trimmed output.
 *
 * @param script    the script to run
 * @param args      optional arguments to pass to the script
 * @return          the trimmed output from the script
 */
String getScriptOutput(String script, String args='') {
    return sh(script: "${script} ${args}", returnStdout: true).trim()
}

/**
 * scriptedBuildStage
 *
 * Get a build stage in scripted syntax.
 *
 * @param kwargs Map containing the following optional arguments (empty strings yield defaults):
 *          name                the build stage name
 *          distro              the shorthand distro name; defaults to 'el8'
 *          rpmDistro           the distro to use for rpm building; defaults to distro
 *          compiler            the compiler to use; defaults to 'gcc'
 *          runStage            Optional additional condition to determine if the stage runs
 *          buildRpms           whether or not to build rpms; defaults to true
 *          release             the DAOS RPM release value to use; defaults to env.DAOS_RELVAL
 *          dockerBuildArgs     optional docker build arguments
 *          sconsBuildArgs      optional scons build arguments
 *          artifacts           optional artifacts name to archive; defaults to
 *                                "config.log-${distro}-${compiler}"
 *          uploadTarget        the distro to use when uploading rpms; defaults to distro
 * @return a scripted stage to run in a pipeline
 */
def scriptedBuildStage(Map kwargs = [:]) {
    String name = kwargs.get('name', 'Unknown Build Stage')
    String distro = kwargs.get('distro', 'el8')
    String rpmDistro = kwargs.get('rpmDistro', distro)
    String compiler = kwargs.get('compiler', 'gcc')
    Boolean runStage = kwargs.get('runStage', true)
    Boolean buildRpms = kwargs.get('buildRpms', true)
    String release = kwargs.get('release', env.DAOS_RELVAL)
    String dockerBuildArgs = kwargs.get('dockerBuildArgs', '')
    Map sconsBuildArgs = kwargs.get('sconsBuildArgs', [:])
    String artifacts = kwargs.get('artifacts', "config.log-${distro}-${compiler}")
    String uploadTarget = kwargs.get('uploadTarget', distro)
    String dockerTag = jobStatusKey("build-${uploadTarget}-${compiler}").toLowerCase()
    String bullseye = 'false'
    if (compiler == 'covc') {
        bullseye = 'true'
    }
    return {
        stage("${name}") {
            if (!runStage) {
                println("[${name}] Marking build stage as skipped")
                Utils.markStageSkippedForConditional("${name}")
                return
            }
            node('docker_runner') {
                println("[${name}] Check out from version control")
                checkoutScm(pruneStaleBranch: true)

                def dockerImage = docker.build(dockerTag, dockerBuildArgs)
                try {
                    dockerImage.inside() {
                        if (buildRpms) {
                            sh label: 'Install RPMs',
                                script: "./ci/rpm/install_deps.sh ${rpmDistro} ${release} ${bullseye}"
                            // Avoid interpolation of sensitive environment variables
                            sh label: 'Build deps',
                                script: "./ci/rpm/build_deps.sh ${bullseye}" + ' ${BULLSEYE_KEY}'
                        }
                        job_step_update(sconsBuild(sconsBuildArgs))
                        if (buildRpms) {
                            sh label: 'Generate RPMs',
                                script: "./ci/rpm/gen_rpms.sh ${rpmDistro} ${release} ${bullseye}"
                            // Success actions
                            uploadNewRPMs(uploadTarget, 'success')
                        }
                    }
                } catch (Exception e) {
                    // Unsuccessful actions
                    sh """if [ -f config.log ]; then
                            mv config.log ${artifacts}
                        fi"""
                    archiveArtifacts artifacts: "${artifacts}", allowEmptyArchive: true
                    throw e
                } finally {
                    // Cleanup actions
                    if (buildRpms) {
                        uploadNewRPMs(uploadTarget, 'cleanup')
                    }
                    jobStatusUpdate(job_status_internal, name)
                }
            }
            println("[${name}] Finished with ${job_status_internal}")
        }
    }
}

/**
 * scriptedSummaryStage
 *
 * Get a summary stage in scripted syntax.
 *
 * @param kwargs Map containing the following optional arguments (empty strings yield defaults):
 *          name                    the summary stage name
 *          distro                  the shorthand distro name; defaults to 'el8'
 *          compiler                the compiler to use; defaults to 'gcc'
 *          runStage                Optional additional condition to determine if the stage runs
 *          dockerBuildArgs         optional docker build arguments
 *          installScript           optional script to install RPMs
 *          runScriptArgs           Map of arguments to pass to runScriptWithStashes()
 *          archiveArtifactsArgs    Map of arguments to pass to archiveArtifacts()
 *          publishHtmlArgs         Map of arguments to pass to publishHTML()
 * @return a scripted stage to run in a pipeline
 */
def scriptedSummaryStage(Map kwargs = [:]) {
    String name = kwargs.get('name', 'Unknown Summary Stage')
    String distro = kwargs.get('distro', 'el8')
    String compiler = kwargs.get('compiler', 'gcc')
    Boolean runStage = kwargs.get('runStage', true)
    String dockerBuildArgs = kwargs.get('dockerBuildArgs', '')
    String installScript = kwargs.get('installScript', '')
    Map runScriptArgs = kwargs.get('runScriptArgs', [:])
    Map archiveArtifactsArgs = kwargs.get('archiveArtifactsArgs', [:])
    Map publishHtmlArgs = kwargs.get('publishHtmlArgs', [:])
    String dockerTag = jobStatusKey("${name}-${distro}-${compiler}").toLowerCase()

    return {
        stage("${name}") {
            if (!runStage) {
                println("[${name}] Marking summary stage as skipped")
                Utils.markStageSkippedForConditional("${name}")
                return
            }
            node('docker_runner') {
                println("[${name}] Check out from version control")
                checkoutScm(pruneStaleBranch: true)

                def dockerImage = docker.build(dockerTag, dockerBuildArgs)
                try {
                    dockerImage.inside() {
                        if (installScript) {
                            sh label: 'Install RPMs',
                                script: "${installScript} ${distro}"
                        }
                        job_step_update(runScriptWithStashes(runScriptArgs))
                    }
                } finally {
                    // Cleanup actions
                    if (publishHtmlArgs) {
                        publishHTML(publishHtmlArgs)
                    }
                    if (archiveArtifactsArgs) {
                        archiveArtifacts(archiveArtifactsArgs)
                    }
                    jobStatusUpdate(job_status_internal, name)
                }
            }
            println("[${name}] Finished with ${job_status_internal}")
        }
    }
}

// Determine if the Build with Bullseye was run and successful
Boolean bullseyeBuilt() {
    Map status = job_status_internal['Build_on_EL_9_with_Bullseye'] ?: [:]
    println("bullseyeBuilt: status=${status}, status.result=${status.result}")
    return status.result == 'SUCCESS'
}

// Get the inst_rpms argument for the unitTest method
String unitTestInstRpms(String distro='el9', Boolean bullseye=false) {
    return getScriptOutput("ci/unit/required_packages.sh ${distro} ${bullseye.toString()}")
}

// Get the compiler argument for the unitTest method
String unitTestCompiler(Boolean bullseye=false) {
    if (bullseye) {
        return 'covc'
    }
    return 'gcc'
}

// Get the packages to install for functional testing
String functionalInstRpms(String otherPackages, Boolean bullseye=false, String rpmDistro=null) {
    String packages = functionalPackages(
        clientVersion: 1,
        nextVersion: next_version(),
        addDaosPkgs: 'tests-internal',
        rpmDistribution: rpmDistro)
    if (bullseye) {
        packages = packages.replace('daos', 'daos-bullseye')
        packages += ' bullseye'
    }
    if (otherPackages) {
        packages += " ${otherPackages}"
    }
    return packages
}

// Boolean skip_pragma_set(String name, String def_val='false') {
//     // Return whether or not the skip pragma is set
//     return cachedCommitPragma("Skip-${name}", def_val).toLowerCase() == 'true'
// }

// Boolean skip_build_stage(String distro='', String compiler='gcc') {
//     // Skip the stage if the CI_<distro>_NOBUILD parameter is set
//     if (distro) {
//         if (startedByUser() && paramsValue("CI_${distro}_NOBUILD", false)) {
//             println("[${env.STAGE_NAME}] Skipping build stage due to CI_${distro}_NOBUILD")
//             return true
//         }
//     }

//     // Skip the stage if any Skip-build[-<distro>-<compiler>] pragmas are true
//     List<String> pragma_names = ['build']
//     if (distro && compiler) {
//         pragma_names << "build-${distro}-${compiler}"
//     }
//     Boolean any_pragma_skip = pragma_names.any { name ->
//         if (skip_pragma_set(name)) {
//             println("[${env.STAGE_NAME}] Skipping build stage due to \"Skip-${name}: true\" pragma")
//             return true
//         }
//     }
//     if (any_pragma_skip) {
//         return true
//     }

//     // Skip the stage if a specific DAOS RPM version is specified
//     if (rpmTestVersion() != '') {
//         println("[${env.STAGE_NAME}] Skipping build stage for due to specific DAOS RPM version")
//         return true
//     }

//     // Otherwise run the build stage
//     return false
// }

pipeline {
    agent { label 'lightweight' }

    environment {
        BULLSEYE_KEY = credentials('bullseye_license_key')
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        SSH_KEY_ARGS = '-ici_key'
        CLUSH_ARGS = "-o$SSH_KEY_ARGS"
        TEST_RPMS = cachedCommitPragma(pragma: 'RPM-test', def_val: 'true')
        COVFN_DISABLED = cachedCommitPragma(pragma: 'Skip-fnbullseye', def_val: 'true')
        REPO_FILE_URL = repoFileUrl(env.REPO_FILE_URL)
        SCONS_FAULTS_ARGS = sconsArgs()
        HTTPS_PROXY = ''
        PYTHON_VERSION = '3.11'
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
                     description: 'Build RPM and DEB packages, Skip unit tests.')
        booleanParam(name: 'CI_ALLOW_UNSTABLE_TEST',
                     defaultValue: false,
                     description: 'Continue testing if a previous stage is Unstable')
        booleanParam(name: 'CI_FULL_BULLSEYE_REPORT',
                     defaultValue: false,
                     description: 'Use this build to generate a full Bullseye code coverage report')
        string(name: 'CI_SCONS_ARGS',
               defaultValue: '',
               description: 'Arguments for scons when building DAOS')
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
        booleanParam(name: 'Cancel Previous Builds',
                     defaultValue: true,
                     description: 'Run the Cancel Previous Builds stage.')
        booleanParam(name: 'Pre-build',
                     defaultValue: true,
                     description: 'Run the pre-build stage.')
        booleanParam(name: 'Python Bandit check',
                     defaultValue: true,
                     description: 'Run the Python Bandit check stage.')
        booleanParam(name: 'Build',
                     defaultValue: true,
                     description: 'Run the build stage.')
        booleanParam(name: 'Build on EL 8',
                     defaultValue: true,
                     description: 'Run the build on EL 8 stage.')
        booleanParam(name: 'Build on EL 9',
                     defaultValue: true,
                     description: 'Run the build on EL 9 stage.')
        booleanParam(name: 'Build on Leap 15',
                     defaultValue: true,
                     description: 'Run the build on Leap 15 stage.')
        booleanParam(name: 'Build on EL 9 with Bullseye',
                     defaultValue: true,
                     description: 'Run the build on EL 9 with Bullseye stage.')
        booleanParam(name: 'Unit Tests',
                     defaultValue: true,
                     description: 'Run the Unit Tests stage.')
        booleanParam(name: 'Unit Test',
                     defaultValue: true,
                     description: 'Run the Unit Test stage.')
        booleanParam(name: 'Unit Test bdev',
                     defaultValue: true,
                     description: 'Run the Unit Test bdev stage.')
        booleanParam(name: 'NLT',
                     defaultValue: true,
                     description: 'Run the NLT stage.')
        booleanParam(name: 'NLT with Bullseye',
                     defaultValue: true,
                     description: 'Run the NLT with Bullseye stage.')
        booleanParam(name: 'Unit Test with memcheck',
                     defaultValue: true,
                     description: 'Run the Unit Test with memcheck stage.')
        booleanParam(name: 'Unit Test bdev with memcheck',
                     defaultValue: true,
                     description: 'Run the Unit Test bdev with memcheck stage.')
        booleanParam(name: 'Test',
                     defaultValue: true,
                     description: 'Run the Test stage.')
        booleanParam(name: 'Functional on EL 8.8 with Valgrind',
                     defaultValue: false,
                     description: 'Run the Functional on EL 8.8 with Valgrind stage.')
        booleanParam(name: 'Functional on EL 8',
                     defaultValue: false,
                     description: 'Run the Functional on EL 8 stage.')
        booleanParam(name: 'Functional on EL 9',
                     defaultValue: true,
                     description: 'Run the Functional on EL 9 stage.')
        booleanParam(name: 'Functional on Leap 15',
                     defaultValue: false,
                     description: 'Run the Functional on Leap 15 stage.')
        booleanParam(name: 'Functional on SLES 15',
                     defaultValue: false,
                     description: 'Run the Functional on SLES 15 stage.')
        booleanParam(name: 'Functional on Ubuntu 20.04',
                     defaultValue: false,
                     description: 'Run the Functional on Ubuntu 20.04 stage.')
        booleanParam(name: 'Fault injection testing',
                     defaultValue: true,
                     description: 'Run the Fault injection testing stage.')
        booleanParam(name: 'Test RPMs on EL 9.6',
                     defaultValue: true,
                     description: 'Run the Test RPMs on EL 9.6 stage.')
        booleanParam(name: 'Test RPMs on Leap 15.5',
                     defaultValue: true,
                     description: 'Run the Test RPMs on Leap 15.5 stage.')
        booleanParam(name: 'Test Hardware',
                     defaultValue: true,
                     description: 'Run the Test Hardware stage.')
        booleanParam(name: 'Functional Hardware Medium',
                     defaultValue: false,
                     description: 'Run the Functional Hardware Medium stage.')
        booleanParam(name: 'Functional Hardware Medium MD on SSD',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium MD on SSD stage.')
        booleanParam(name: 'Functional Hardware Medium VMD',
                     defaultValue: false,
                     description: 'Run the Functional Hardware Medium VMD stage.')
        booleanParam(name: 'Functional Hardware Medium Verbs Provider',
                     defaultValue: false,
                     description: 'Run the Functional Hardware Medium Verbs Provider stage.')
        booleanParam(name: 'Functional Hardware Medium Verbs Provider MD on SSD',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Medium Verbs Provider MD on SSD stage.')
        booleanParam(name: 'Functional Hardware Medium UCX Provider',
                     defaultValue: false,
                     description: 'Run the Functional Hardware Medium UCX Provider stage.')
        booleanParam(name: 'Functional Hardware Large',
                     defaultValue: false,
                     description: 'Run the Functional Hardware Large stage.')
        booleanParam(name: 'Functional Hardware Large MD on SSD',
                     defaultValue: true,
                     description: 'Run the Functional Hardware Large MD on SSD stage.')
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
               defaultValue: 'ci_nlt_vm1',
               description: 'Label to use for NLT tests')
        string(name: 'CI_FI_1_LABEL',
               defaultValue: 'ci_fi_vm1',
               description: 'Label to use for Fault Injection (FI) tests')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_LABEL',
               defaultValue: 'ci_nvme5',
               description: 'Label to use for the Functional Hardware Medium (MD on SSD) stages')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL',
               defaultValue: 'ci_ofed5',
               description: 'Label to use for 5 node Functional Hardware Medium Verbs Provider (MD on SSD) stages')
        string(name: 'FUNCTIONAL_HARDWARE_MEDIUM_VMD_LABEL',
               defaultValue: 'ci_vmd5',
               description: 'Label to use for the Functional Hardware Medium VMD stage')
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
        stage('Prepare Environment Variables') {
            // TODO: Could/should these be moved to the environment block?
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
                        pragmasToEnv()
                        update_default_commit_pragmas()
                        updateRunStage()
                    }
                }
                stage('Get RPM relval') {
                    steps {
                        get_rpm_relval()
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
                expression { runStage['Cancel Previous Builds'] }
            }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Pre-build') {
            when {
                beforeAgent true
                expression { runStage['Pre-build'] }
            }
            parallel {
                stage('Python Bandit check') {
                    when {
                        beforeAgent true
                        expression { runStage['Python Bandit check'] }
                    }
                    agent {
                        dockerfile {
                            filename 'utils/docker/Dockerfile.code_scanning'
                            label 'docker_runner'
                            additionalBuildArgs dockerBuildArgs(add_repos: false) +
                                                ' --build-arg FVERSION=37'
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
                expression { runStage['Build'] }
            }
            steps {
                script {
                    parallel(
                        'Build on EL 8': scriptedBuildStage(
                            name: 'Build on EL 8',
                            distro:'el8',
                            compiler: 'gcc',
                            runStage: runStage['Build on EL 8'],
                            buildRpms: true,
                            release: env.DAOS_RELVAL,
                            dockerBuildArgs: dockerBuildArgs(repo_type: 'stable',
                                                             deps_build: false,
                                                             parallel_build: true) +
                                             ' --build-arg DAOS_PACKAGES_BUILD=no' +
                                             ' --build-arg DAOS_KEEP_SRC=yes' +
                                             ' --build-arg REPOS="' + prRepos('el8') + '"' +
                                             ' --build-arg POINT_RELEASE=.10 ' +
                                             " --build-arg PYTHON_VERSION=${env.PYTHON_VERSION}" +
                                             ' -f utils/docker/Dockerfile.el.8 .',
                            sconsBuildArgs: [
                                parallel_build: true,
                                stash_files: 'ci/test_files_to_stash.txt',
                                build_deps: 'no',
                                stash_opt: true,
                                scons_args: sconsArgs() + ' PREFIX=/opt/daos TARGET_TYPE=release'
                            ],
                            artifacts: "config.log-el8-gcc"
                        ),
                        'Build on EL 9': scriptedBuildStage(
                            name: 'Build on EL 9',
                            distro:'el9',
                            compiler: 'gcc',
                            runStage: runStage['Build on EL 9'],
                            buildRpms: true,
                            release: env.DAOS_RELVAL,
                            dockerBuildArgs: dockerBuildArgs(repo_type: 'stable',
                                                             deps_build: false,
                                                             parallel_build: true) +
                                             ' --build-arg DAOS_PACKAGES_BUILD=no' +
                                             ' --build-arg DAOS_KEEP_SRC=yes' +
                                             ' --build-arg REPOS="' + prRepos('el9') + '"' +
                                             ' --build-arg POINT_RELEASE=.7' +
                                             " --build-arg PYTHON_VERSION=${env.PYTHON_VERSION}" +
                                             ' -f utils/docker/Dockerfile.el.9 .',
                            sconsBuildArgs: [
                                parallel_build: true,
                                stash_files: 'ci/test_files_to_stash.txt',
                                build_deps: 'no',
                                stash_opt: true,
                                scons_args: sconsArgs() + ' PREFIX=/opt/daos TARGET_TYPE=release'
                            ],
                            artifacts: "config.log-el9-gcc"
                        ),
                        'Build on Leap 15': scriptedBuildStage(
                            name: 'Build on Leap 15',
                            distro:'leap15',
                            rpmDistro: 'suse.lp156',
                            compiler: 'gcc',
                            runStage: runStage['Build on Leap 15'],
                            buildRpms: true,
                            release: env.DAOS_RELVAL,
                            dockerBuildArgs: dockerBuildArgs(repo_type: 'stable',
                                                             deps_build: false,
                                                             parallel_build: true) +
                                             ' --build-arg DAOS_PACKAGES_BUILD=no' +
                                             ' --build-arg DAOS_KEEP_SRC=yes' +
                                             ' --build-arg POINT_RELEASE=.6' +
                                             " --build-arg PYTHON_VERSION=${env.PYTHON_VERSION}" +
                                             ' -f utils/docker/Dockerfile.leap.15 .',
                            sconsBuildArgs: [
                                parallel_build: true,
                                build_deps: 'yes',
                                scons_args: sconsArgs() + ' PREFIX=/opt/daos TARGET_TYPE=release'
                            ],
                            artifacts: "config.log-leap156-gcc",
                        ),
                        'Build on EL 9 with Bullseye': scriptedBuildStage(
                            name: 'Build on EL 9 with Bullseye',
                            distro:'el9',
                            compiler: 'covc',
                            runStage: runStage['Build on EL 9 with Bullseye'],
                            buildRpms: true,
                            release: env.DAOS_RELVAL,
                            dockerBuildArgs: dockerBuildArgs(repo_type: 'stable',
                                                             deps_build: false,
                                                             parallel_build: true) +
                                             ' --build-arg DAOS_PACKAGES_BUILD=no' +
                                             ' --build-arg DAOS_KEEP_SRC=yes' +
                                             ' --build-arg REPOS="' + prRepos('el9') + '"' +
                                             ' --build-arg POINT_RELEASE=.7' +
                                             " --build-arg PYTHON_VERSION=${env.PYTHON_VERSION}" +
                                             ' --build-arg COMPILER=covc' +
                                             ' --build-arg CODE_COVERAGE=true' +
                                             ' -f utils/docker/Dockerfile.el.9 .',
                            sconsBuildArgs: [
                                parallel_build: true,
                                stash_files: 'ci/test_files_to_stash.txt',
                                build_deps: 'no',
                                stash_opt: true,
                                scons_args: sconsArgs() + ' PREFIX=/opt/daos TARGET_TYPE=release' +
                                            ' COMPILER=covc'
                            ],
                            artifacts: "config.log-el9-covc",
                            uploadTarget: 'el9-bullseye'
                        )
                    ) // parallel
                } // script
            } // steps
        } // stage('Build')
        stage('Unit Tests') {
            when {
                beforeAgent true
                expression { runStage['Unit Tests'] }
            }
            parallel {
                stage('Unit Test') {
                    when {
                        beforeAgent true
                        expression { runStage['Unit Test'] }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'VM1-label', def_val: params.CI_UNIT_VM1_LABEL)
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 120,
                                     unstash_opt: true,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', bullseyeBuilt()),
                                     image_version: 'el9.7',
                                     compiler: unitTestCompiler(bullseyeBuilt()),
                                     test_script: 'ci/unit/test_main.sh',
                                     always_script: 'ci/unit/test_post_always.sh unit_test_logs',
                                     coverage_stash: 'unit_test_bullseye'))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_logs/'],
                                         compiler: unitTestCompiler(bullseyeBuilt())
                            job_status_update()
                        }
                    }
                }
                stage('Unit Test bdev') {
                    when {
                        beforeAgent true
                        expression { runStage['Unit Test bdev'] }
                    }
                    agent {
                        label params.CI_UNIT_VM1_NVME_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 120,
                                     unstash_opt: true,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', bullseyeBuilt()),
                                     image_version: 'el9.7',
                                     compiler: unitTestCompiler(bullseyeBuilt()),
                                     test_script: 'ci/unit/test_main.sh',
                                     always_script: 'ci/unit/test_post_always.sh unit_test_bdev_logs',
                                     coverage_stash: 'unit_test_bdev_bullseye'))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_bdev_logs/'],
                                         compiler: unitTestCompiler(bullseyeBuilt())
                            job_status_update()
                        }
                    }
                }
                stage('NLT') {
                    when {
                        beforeAgent true
                        expression { runStage['NLT'] }
                    }
                    agent {
                        label params.CI_NLT_1_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 60,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', false),
                                     image_version: 'el9.7',
                                     compiler: 'gcc',
                                     test_script: 'ci/unit/test_nlt.sh' +
                                                  ' --system-ram-reserved 4' +
                                                  ' --max-log-size 1950MiB' +
                                                  ' --dfuse-dir /localhome/jenkins/' +
                                                  ' --log-usage-save nltir.xml' +
                                                  ' --log-usage-export nltr.json' +
                                                  ' --log-base-dir nlt_logs' +
                                                  ' --class-name nlt all',
                                     always_script: 'ci/unit/test_nlt_post.sh nlt_logs',
                                     testResults: 'nlt-junit.xml',
                                     unstash_opt: true,
                                     unstash_tests: false,
                                     with_valgrind: 'memcheck',
                                     valgrind_pattern: '*memcheck.xml',
                                     prov_env_vars: 'VM_CPUS=14'))
                        // recordCoverage(tools: [[parser: 'COBERTURA', pattern:'nltir.xml']],
                        //                 skipPublishingChecks: true,
                        //                 id: 'tlc', name: 'Fault Injection Interim Report')
                        stash(name:'nltr', includes:'nltr.json', allowEmpty: true)
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['nlt_logs/'],
                                         testResults: 'nlt-junit.xml',
                                         referenceJobName: 'daos-stack/daos/release%252F2.8',
                                         valgrind_stash: 'nlt-memcheck',
                                         valgrind_pattern: '*memcheck.xml',
                                         NLT: true
                            recordIssues enabledForFailure: true,
                                         failOnError: false,
                                         ignoreQualityGate: true,
                                         name: 'NLT server leaks',
                                         qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]],
                                         tool: issues(pattern: 'nlt-server-leaks.json',
                                                      name: 'NLT server results',
                                                      id: 'NLT_server'),
                                         scm: 'daos-stack/daos'
                            job_status_update()
                        }
                    }
                }
                stage('NLT with Bullseye') {
                    when {
                        beforeAgent true
                        expression { runStage['NLT with Bullseye'] }
                    }
                    agent {
                        label params.CI_NLT_1_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 150,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', true),
                                     image_version: 'el9.7',
                                     compiler: 'covc',
                                     test_script: 'ci/unit/test_nlt.sh' +
                                                  ' --system-ram-reserved 4' +
                                                  ' --max-log-size 1950MiB' +
                                                  ' --dfuse-dir /localhome/jenkins/' +
                                                  ' --log-usage-save nltir-bullseye.xml' +
                                                  ' --log-usage-export nltr-bullseye.json' +
                                                  ' --log-base-dir nlt_bullseye_logs' +
                                                  ' --memcheck no' +
                                                  ' --class-name nlt all',
                                     always_script: 'ci/unit/test_nlt_post.sh nlt_bullseye_logs',
                                     testResults: 'nlt-junit.xml',
                                     unstash_opt: true,
                                     unstash_tests: false,
                                     prov_env_vars: 'VM_CPUS=14',
                                     ignore_failure: true,
                                     coverage_stash: 'nlt_bullseye'))
                        stash(name:'nltr-bullseye', includes:'nltr-bullseye.json', allowEmpty: true)
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['nlt_bullseye_logs/'],
                                         testResults: 'nlt-junit.xml',
                                         NLT: true,
                                         compiler: 'covc'
                            job_status_update()
                        }
                    }
                }
                stage('Unit Test with memcheck') {
                    when {
                        beforeAgent true
                        expression { runStage['Unit Test with memcheck'] }
                    }
                    agent {
                        label cachedCommitPragma(pragma: 'VM1-label', def_val: params.CI_UNIT_VM1_LABEL)
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 160,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', false),
                                     image_version: 'el9.7',
                                     compiler: 'gcc',
                                     test_script: 'ci/unit/test_main.sh',
                                     always_script: 'ci/unit/test_post_always.sh unit_test_memcheck_logs',
                                     unstash_opt: true,
                                     ignore_failure: true))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_logs.tar.gz',
                                                     'unit_test_memcheck_logs/**/*.log'],
                                         valgrind_stash: 'unit-memcheck'
                            job_status_update()
                        }
                    }
                } // stage('Unit Test with memcheck')
                stage('Unit Test bdev with memcheck') {
                    when {
                        beforeAgent true
                        expression { runStage['Unit Test bdev with memcheck'] }
                    }
                    agent {
                        label params.CI_UNIT_VM1_NVME_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 180,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitTestInstRpms('el9', false),
                                     image_version: 'el9.7',
                                     compiler: 'gcc',
                                     test_script: 'ci/unit/test_main.sh',
                                     always_script: 'ci/unit/test_post_always.sh unit_test_memcheck_bdev_logs',
                                     unstash_opt: true,
                                     ignore_failure: true))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['unit_test_memcheck_bdev_logs.tar.gz',
                                                     'unit_test_memcheck_bdev_logs/**/*.log'],
                                         valgrind_stash: 'unit-bdev-memcheck'
                            job_status_update()
                        }
                    }
                } // stage('Unit Test bdev with memcheck')
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                expression { runStage['Test'] }
            }
            parallel {
                stage('Functional on EL 8.8 with Valgrind') {
                    when {
                        beforeAgent true
                        expression { runStage['Functional on EL 8.8 with Valgrind'] }
                    }
                    agent {
                        label vm9_label('EL8')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version(), 'tests-internal') +
                                           ' mercury-libfabric',
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8.8 with Valgrind')
                stage('Functional on EL 8') {
                    when {
                        beforeAgent true
                        expression { runStage['Functional on EL 8'] }
                    }
                    agent {
                        label vm9_label('EL8')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalInstRpms('mercury-libfabric', false),
                                test_function: 'runTestFunctionalV2',
                                image_version: 'el8.10'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 8')
                stage('Functional on EL 9') {
                    when {
                        beforeAgent true
                        expression { runStage['Functional on EL 9'] }
                    }
                    agent {
                        label vm9_label('EL9')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalInstRpms(
                                    'mercury-libfabric',
                                    paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                                test_function: 'runTestFunctionalV2',
                                image_version: 'el9.7',
                                bullseye: paramsValue('CI_FULL_BULLSEYE_REPORT', false),
                                coverage_stash: 'func_vm_bullseye'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    }
                } // stage('Functional on EL 9')
                stage('Functional on Leap 15') {
                    when {
                        beforeAgent true
                        expression { runStage['Functional on Leap 15'] }
                    }
                    agent {
                        label vm9_label('Leap15')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalInstRpms('mercury-libfabric', false),
                                test_function: 'runTestFunctionalV2',
                                image_version: 'leap15.6'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } // post
                } // stage('Functional on Leap 15')
                stage('Functional on Ubuntu 20.04') {
                    when {
                        beforeAgent true
                        expression { runStage['Functional on Ubuntu 20.04'] }
                    }
                    agent {
                        label vm9_label('Ubuntu')
                    }
                    steps {
                        job_step_update(
                            functionalTest(
                                inst_repos: daosRepos(),
                                inst_rpms: functionalInstRpms('mercury-libfabric', false),
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } // post
                } // stage('Functional on Ubuntu 20.04')
                stage('Fault injection testing') {
                    when {
                        beforeAgent true
                        expression { runStage['Fault injection testing'] }
                    }
                    agent {
                        label params.CI_FI_1_LABEL
                    }
                    steps {
                        job_step_update(
                            unitTest(timeout_time: 240,
                                     inst_repos: daosRepos(),
                                     inst_rpms: unitPackages(target: 'el9') + ' daos-client-tests',
                                     image_version: 'el9.7',
                                     test_script: 'ci/unit/test_nlt.sh --memcheck no' +
                                                  ' --system-ram-reserved 4 --server-debug WARN' +
                                                  ' --log-usage-import nltr.json' +
                                                  ' --log-usage-save nltr.xml' +
                                                  ' --log-base-dir nlt_logs' +
                                                  ' --class-name fault-injection fi',
                                     with_valgrind: '',
                                     always_script: 'ci/unit/test_nlt_post.sh nlt_logs',
                                     testResults: 'nlt-junit.xml',
                                     unstash_opt: true,
                                     unstash_tests: false,
                                     prov_env_vars: 'VM_CPUS=14'))
                    }
                    post {
                        always {
                            unitTestPost artifacts: ['nlt_logs/'],
                                         testResults: 'nlt-junit.xml',
                                         with_valgrind: '',
                                         FI: true
                            discoverGitReferenceBuild referenceJob: 'daos-stack/daos/master',
                                                      scm: 'daos-stack/daos',
                                                      requiredResult: hudson.model.Result.UNSTABLE
                            archiveArtifacts artifacts: 'nlt_logs/fault-injection/',
                                             allowEmptyArchive: true
                            job_status_update()
                        }
                    }
                } // stage('Fault injection testing')
                stage('Test RPMs on EL 9.6') {
                    when {
                        beforeAgent true
                        expression { runStage['Test RPMs on EL 9.6'] }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        job_step_update(
                            testRpm(inst_repos: daosRepos(),
                                    daos_pkg_version: daosPackagesVersion(next_version()),
                                    inst_rpms: 'mercury-libfabric')
                        )
                    }
                    post {
                        always {
                            rpm_test_post(env.STAGE_NAME, env.NODELIST)
                        }
                    }
                } // stage('Test RPMs on EL 9.6')
                stage('Test RPMs on Leap 15.5') {
                    when {
                        beforeAgent true
                        expression { runStage['Test RPMs on Leap 15.5'] }
                    }
                    agent {
                        label params.CI_UNIT_VM1_LABEL
                    }
                    steps {
                        /* neither of these work as FTest strips the first node
                           out of the pool requiring 2 node clusters at minimum
                         * additionally for this use-case, can't override
                           ftest_arg with this :-(
                        script {
                            'Test RPMs on Leap 15.5': getFunctionalTestStage(
                                name: 'Test RPMs on Leap 15.5',
                                pragma_suffix: '',
                                label: params.CI_UNIT_VM1_LABEL,
                                next_version: next_version(),
                                stage_tags: '',
                                default_tags: 'test_daos_management',
                                nvme: 'auto',
                                run_if_pr: true,
                                run_if_landing: true,
                                job_status: job_status_internal
                            )
                        }
                           job_step_update(
                            functionalTest(
                                test_tag: 'test_daos_management',
                                ftest_arg: '--yaml_extension single_host',
                                inst_repos: daosRepos(),
                                inst_rpms: functionalPackages(1, next_version(), 'tests-internal'),
                                test_function: 'runTestFunctionalV2'))
                    }
                    post {
                        always {
                            functionalTestPostV2()
                            job_status_update()
                        }
                    } */
                        job_step_update(
                            testRpm(inst_repos: daosRepos(),
                                    daos_pkg_version: daosPackagesVersion(next_version()),
                                    inst_rpms: 'mercury-libfabric')
                        )
                    }
                    post {
                        always {
                            rpm_test_post(env.STAGE_NAME, env.NODELIST)
                        }
                    }
                } // stage('Test RPMs on Leap 15.5')
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
                        inst_rpms: functionalPackages(1, next_version(), 'tests-internal')))
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
                expression { runStage['Test Hardware'] }
            }
            steps {
                script {
                    parallel(
                        'Functional Hardware Medium': getFunctionalTestStage(
                            name: 'Functional Hardware Medium',
                            runStage: runStage['Functional Hardware Medium'],
                            pragma_suffix: '-hw-medium',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            nvme: 'auto',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Medium MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium MD on SSD',
                            runStage: runStage['Functional Hardware Medium MD on SSD'],
                            pragma_suffix: '-hw-medium-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,medium,-provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            nvme: 'auto_md_on_ssd',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_md_on_ssd_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Medium VMD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium VMD',
                            runStage: runStage['Functional Hardware Medium VMD'],
                            pragma_suffix: '-hw-medium-vmd',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VMD_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw_vmd,medium',
                            /* groovylint-disable-next-line UnnecessaryGetter */
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            nvme: 'auto',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_vmd_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Medium Verbs Provider': getFunctionalTestStage(
                            name: 'Functional Hardware Medium Verbs Provider',
                            runStage: runStage['Functional Hardware Medium Verbs Provider'],
                            pragma_suffix: '-hw-medium-verbs-provider',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            provider: 'ofi+verbs;ofi_rxm',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_verbs_provider_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Medium Verbs Provider MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Medium Verbs Provider MD on SSD',
                            runStage: runStage['Functional Hardware Medium Verbs Provider MD on SSD'],
                            pragma_suffix: '-hw-medium-verbs-provider-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_VERBS_PROVIDER_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto_md_on_ssd',
                            provider: 'ofi+verbs;ofi_rxm',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_verbs_provider_md_on_ssd_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Medium UCX Provider': getFunctionalTestStage(
                            name: 'Functional Hardware Medium UCX Provider',
                            runStage: runStage['Functional Hardware Medium UCX Provider'],
                            pragma_suffix: '-hw-medium-ucx-provider',
                            label: params.FUNCTIONAL_HARDWARE_MEDIUM_UCX_PROVIDER_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,medium,provider',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            provider: cachedCommitPragma('Test-provider-ucx', 'ucx+ud_x'),
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_medium_ucx_provider_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Large': getFunctionalTestStage(
                            name: 'Functional Hardware Large',
                            runStage: runStage['Functional Hardware Large'],
                            pragma_suffix: '-hw-large',
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_large_bullseye',
                            image_version: 'el9.7'
                        ),
                        'Functional Hardware Large MD on SSD': getFunctionalTestStage(
                            name: 'Functional Hardware Large MD on SSD',
                            runStage: runStage['Functional Hardware Large MD on SSD'],
                            pragma_suffix: '-hw-large-md-on-ssd',
                            label: params.FUNCTIONAL_HARDWARE_LARGE_LABEL,
                            inst_rpms: functionalInstRpms(
                                'mercury-libfabric', paramsValue('CI_FULL_BULLSEYE_REPORT', false)),
                            stage_tags: 'hw,large',
                            default_tags: startedByTimer() ? 'pr daily_regression' : 'pr',
                            default_nvme: 'auto_md_on_ssd',
                            job_status: job_status_internal,
                            coverage_stash: 'func_hw_large_md_on_ssd_bullseye',
                            image_version: 'el9.7'
                        ),
                    )
                }
            }
        } // stage('Test Hardware')
        stage('Test Summary') {
            when {
                beforeAgent true
                expression { true }
            }
            steps {
                script {
                    parallel(
                        'Bullseye Report': scriptedSummaryStage(
                            name: 'Bullseye Report',
                            distro: 'el9',
                            compiler: 'covc',
                            runStage: bullseyeBuilt(),
                            nodeLabel: 'docker_runner',
                            dockerBuildArgs: dockerBuildArgs(repo_type: 'stable',
                                                             deps_build: false,
                                                             parallel_build: true) +
                                             ' --build-arg DAOS_PACKAGES_BUILD=no' +
                                             ' --build-arg DAOS_KEEP_SRC=yes' +
                                             ' --build-arg REPOS="' + prRepos('el9') + '"' +
                                             ' --build-arg COMPILER=covc' +
                                             ' --build-arg CODE_COVERAGE=true' +
                                             ' -f utils/docker/Dockerfile.el.9 .',
                            installScript: './ci/summary/install_pkgs.sh el9 true',
                            runScriptArgs: [
                                label: 'Generate Bullseye Report',
                                script: 'ci/summary/bullseye_report.sh',
                                stashes: ['unit_test_bullseye',
                                          'unit_test_bdev_bullseye',
                                          'nlt_bullseye',
                                          'func_vm_bullseye',
                                          'func_hw_medium_bullseye',
                                          'func_hw_medium_md_on_ssd_bullseye',
                                          'func_hw_medium_verbs_provider_bullseye',
                                          'func_hw_medium_verbs_provider_md_on_ssd_bullseye',
                                          'func_hw_medium_ucx_provider_bullseye',
                                          'func_hw_large_bullseye',
                                          'func_hw_large_md_on_ssd_bullseye']
                            ],
                            archiveArtifactsArgs: [
                                artifacts: 'bullseye_code_coverage_report/',
                                allowEmptyArchive: false
                            ],
                            publishHtmlArgs: [
                                target: [
                                    reportDir: 'bullseye_code_coverage_report',
                                    reportFiles: 'index.html',
                                    reportName: 'Bullseye Coverage'
                                ]
                            ]
                        )
                    ) // parallel
                } // script
            } // steps
        } // stage('Test Summary')
    } // stages
    post {
        always {
            valgrindReportPublish valgrind_stashes: ['nlt-memcheck',
                                                     'unit-memcheck']
            job_status_update('final_status')
            jobStatusWrite(job_status_internal)
        }
        unsuccessful {
            notifyBrokenBranch branches: target_branch
        }
    } // post
}
