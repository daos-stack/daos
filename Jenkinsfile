#!/usr/bin/env groovy
// Copyright (c) 2018 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

@Library(value="pipeline-lib") _

pipeline {
  agent any

  parameters {
    choice(name: 'PDOCKERFILE',
           choices: ['Dockerfile.centos.7', 'Dockerfile.leap.15',
                     'Dockerfile.ubuntu.1804'],
           description: 'Dockerfile to use.')
    string(name: 'PBRANCH_NAME', defaultValue: '',
           description: 'PR name')
    string(name: 'PCHANGE_AUTHOR', defaultValue: '',
           description: 'PR Author ID')
    string(name: 'PCHANGE_AUTHOR_DISPLAY_NAME', defaultValue: '',
           description: 'PR Author Name')
    string(name: 'PCHANGE_BRANCH', defaultValue: '',
           description: 'PR Change Branch')
    string(name: 'PCHANGE_ID', defaultValue: '',
           description: 'PR Number')
    string(name: 'PCHANGE_TARGET', defaultValue: '',
           description: 'PR branch')
    string(name: 'PCHANGE_TITLE', defaultValue: '',
           description: 'PR Author ID')
    string(name: 'PCHANGE_URL', defaultValue: '',
           description: 'PR URL')
    string(name: 'PGIT_BRANCH', defaultValue: '',
           description: 'Git branch same as CHANGE_BRANCH')
    string(name: 'PGIT_COMMIT', defaultValue: '',
           description: 'Git commit hash')
    string(name: 'PGIT_PREVIOUS_COMMIT', defaultValue: '',
           description: 'GIT previous commit')
    string(name: 'PGIT_PREVIOUS_SUCCESSFUL_COMMIT', defaultValue: '',
           description: 'GIT previous successful commit')
  }

  environment {
    GITHUB_CREDS = 'aa4ae90b-b992-4fb6-b33b-236a53a26f77'
    BAHTTPS_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTP_PROXY="' + env.HTTP_PROXY + '" --build-arg http_proxy="' + env.HTTP_PROXY + '"' : ''}"
    BAHTTP_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTPS_PROXY="' + env.HTTPS_PROXY + '" --build-arg https_proxy="' + env.HTTPS_PROXY + '"' : ''}"
    UID=sh(script: "id -u", returnStdout: true)
    BUILDARGS = "--build-arg NOBUILD=1 --build-arg UID=$env.UID $env.BAHTTP_PROXY $env.BAHTTPS_PROXY"
    MY_DOCKERFILE = "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
    BRANCH_NAME = "${env.BRANCH_NAME ? env.BRANCH_NAME : params.PBRANCH_NAME}"
    CHANGE_AUTHOR = "${env.CHANGE_AUTHOR ? env.CHANGE_AUTHOR : params.PCHANGE_AUTHOR}"
    CHANGE_AUTHOR_DISPLAY_NAME = "${env.CHANGE_AUTHOR_DISPLAY_NAME ? env.CHANGE_AUTHOR_DISPLAY_NAME : params.PCHANGE_AUTHOR_DISPLAY_NAME}"
    CHANGE_BRANCH = "${env.CHANGE_BRANCH ? env.CHANGE_BRANCH : params.PCHANGE_BRANCH}"
    CHANGE_ID = "${env.CHANGE_ID ? env.CHANGE_ID : params.PCHANGE_ID}"
    CHANGE_TARGET = "${env.CHANGE_TARGET ? env.CHANGE_TARGET : params.PCHANGE_TARGET}"
    CHANGE_TITLE = "${env.CHANGE_TITLE ? env.CHANGE_TITLE : params.PCHANGE_TITLE}"
    CHANGE_URL = "${env.CHANGE_URL ? env.CHANGE_URL : params.PCHANGE_URL}"
    GIT_BRANCH = "${env.GIT_BRANCH ? env.GIT_BRANCH : params.PGIT_BRANCH}"
    GIT_COMMIT = "${env.GIT_COMMIT ? env.GIT_COMMIT : params.PGIT_COMMIT}"
    GIT_PREVIOUS_COMMIT = "${env.GIT_PREVIOUS_COMMIT ? env.GIT_PREVIOUS_COMMIT : params.PGIT_PREVIOUS_COMMIT}"
    GIT_PREVIOUS_SUCCESSFUL_COMMIT = "${env.GIT_PREVIOUS_SUCCESSFUL_COMMIT ? env.GIT_PREVIOUS_SUCCESSFUL_COMMIT : params.PGIT_PREVIOUS_SUCCESSFUL_COMMIT}"

    FUSE_COMMIT = '3e2fcf3a630e575bc420df254525834504dc01b7'
    HWLOC_COMMIT = 'refs/tags/hwloc-1.11.5'
    MERCURY_COMMIT = '674e7f2bd17b5d8b85606cd152dd1bc189899b0e'
    OFI_COMMIT = '8c33f9d63d536cc3781017dd25b7bb480ac96cb5'
    OMPI_COMMIT = '373098d8ae0053af85cc1d49a58dbe933a31a50a'
    OPENPA_COMMIT = '8e1e74feb22d2e733f34a96e6c7834fed3073c52'
    PMIX_COMMIT = '8c2ffe7e6837a2fb76b882e4c5765032b2b84fa9'
  }

  options {
    // preserve stashes so that jobs can be started at the test stage
    preserveStashes(buildCount: 5)
    checkoutToSubdirectory('scons_local')
  }

  stages {
    stage('Phase 1') {
    parallel {
      stage ('environment report') {
        agent any
        steps {
          sh "export; pip freeze || true"
        }
      }
      stage ('code_lint_check') {

        // Just use CentOS for lint checks.
        agent {
           dockerfile {
             filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
             dir 'scons_local/docker'
             label 'docker_runner'
             additionalBuildArgs '$BUILDARGS'
           }
        } // agent
        steps {
          sh "pip freeze || true"
          checkPatch review_creds: "${env.GITHUB_CREDS}"
        } // steps
      } // stage ('code_lint_check')
      stage ('fuse build') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          // Older scons_local_review only used master branch.
          // Newer one looked up the last known good build of master branch.
          // Pipeline currently does not have access that info.

          sh 'rm -rf testbin/ && mkdir -p testbin'
          sconsBuild target: 'fuse',
                     directory: 'scons_local',
                     scm: [url: 'https://github.com/libfuse/libfuse.git',
                           branch: "${env.FUSE_COMMIT}",
                           cleanAfterCheckout: true],
                     no_install: true,  // No separate install step
                     TARGET_PREFIX: '/testbin',
                     target_work: 'testbin'

          echo "fuse build succeeded"
        } // steps
      } // stage ('fuse build')
      stage ('openpa prebuild') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          sh 'rm -rf testbin/ && mkdir -p testbin'
          sconsBuild target: 'openpa',
                     directory: 'scons_local',
                     scm: [url: 'http://git.mcs.anl.gov/radix/openpa.git',
                           branch: "${env.OPENPA_COMMIT}",
                           cleanAfterCheckout: true],
                     no_install: true,  // No separate install step
                     TARGET_PREFIX: '/testbin',
                     target_work: 'testbin'

          echo "openpa build succeeded"
          stash name: "${env.MY_DOCKERFILE}-openpa",
                includes: 'testbin/openpa/**'
        } // steps
      } // stage ('openpa prebuild')
      stage ('hwloc prebuild') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          sh 'rm -rf testbin/ && mkdir -p testbin'
          sconsBuild target: 'hwloc',
                     directory: 'scons_local',
                     scm: [url: 'https://github.com/open-mpi/hwloc.git',
                           branch: "${env.HWLOC_COMMIT}",
                           cleanAfterCheckout: true],
                     no_install: true,  // No separate install step
                     TARGET_PREFIX: '/testbin',
                     target_work: 'testbin',
                     prebuild: 'pushd ${WORKSPACE}/hwloc;' +
                               ' ./autogen.sh; popd'
          echo "hwloc build succeeded"
          stash name: "${env.MY_DOCKERFILE}-hwloc",
                includes: 'testbin/hwloc/**'
        } // steps
      } // stage ('hwloc prebuild')
    } // parallel
    } // stage ('phase 1')
    stage('Phase 2') {
    parallel {
      stage ('CaRT build') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          sconsBuild target: 'cart',
                     scons_local_replace: true,
                     scm: [url: 'https://github.com/daos-stack/cart.git',
                           cleanAfterCheckout: true,
                           withSubmodules: true]
          echo "CaRT build succeeded"
        } // steps
      } // stage ('CaRT build')
      stage ('daos depends') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          // skip cart as we are already buiding that.
          sconsBuild target: 'daos',
                     REQUIRES: 'pmdk,argobots,isal,protobufc',
                     directory: 'scons_local',
                     scons_local_replace: true,
                     scm: [url: 'https://github.com/daos-stack/daos.git',
                           cleanAfterCheckout: true,
                           withSubmodules: true]
          echo "daos depends build succeeded"
        } // steps
      } // stage ('daos depends')
      stage ('basic checks') {
        agent {
          dockerfile {
            filename "${params.PDOCKERFILE ? params.PDOCKERFILE : 'Dockerfile.centos.7'}"
            dir 'scons_local/docker'
            label 'docker_runner'
            additionalBuildArgs '$BUILDARGS'
          }
        } // agent
        steps {
          //checkout scm
          // Older scons_local_review only used master branch.
          // Newer one looked up the last known good build of master branch.
          // Pipeline currently does not have access that info.
          checkoutScm url: 'http://git.mcs.anl.gov/radix/openpa.git',
                      branch: "${env.OPENPA_COMMIT}",
                      checkoutDir: 'openpa',
                      cleanAfterCheckout: true
          checkoutScm url: 'https://github.com/mercury-hpc/mercury.git',
                      branch: "${env.MERCURY_COMMIT}",
                      checkoutDir: 'mercury',
                      cleanAfterCheckout: true,
                      withSubmodules: true
          checkoutScm url: 'https://github.com/ofiwg/libfabric.git',
                      branch: "${env.OFI_COMMIT}",
                      checkoutDir: 'ofi',
                      cleanAfterCheckout: true
          checkoutScm url: 'https://github.com/pmix/master.git',
                      branch: "${env.PMIX_COMMIT}",
                      checkoutDir: 'pmix',
                      cleanAfterCheckout: true
          checkoutScm url: 'https://github.com/open-mpi/ompi.git',
                      branch: "${env.OMPI_COMMIT}",
                      checkoutDir: 'ompi',
                      cleanAfterCheckout: true
          sh('''find /testbin -maxdepth 1 -type l -print 0 | xargs -r0 rm
                rm -rf testbin
                for new_dir in openpa hwloc; do
                  mkdir -p testbin/${new_dir}
                  ln -s ${PWD}/testbin/${new_dir} /testbin/${new_dir}
                done''')
          runTest stashes: ["${env.MY_DOCKERFILE}-hwloc",
                             "${env.MY_DOCKERFILE}-openpa"],
            script: '''#!/bin/bash
                       set -e
                       export WORKSPACE=""
                       export prebuilt1="PREBUILT_PREFIX=/testbin/hwloc"
                              prebuilt1+=":/testbin/openpa"
                       export prebuilt2="HWLOC_PREBUILT=/testbin/hwloc"
                              prebuilt2+=" OPENPA_PREBUILT=/testbin/openpa"
                       export SRC_PREFIX="${PWD}"
                             SRC_PREFIX+=":${PWD}/scons_local/test/prereq"
                       pushd scons_local
                         ./test_scons_local.sh
                       popd'''
        } // steps
      } // stage ('basic checks')
    } // parallel
    } // stage ('phase 2')
  } // stages
} // pipeline