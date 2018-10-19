pipeline {
    agent any

    environment {
        MY_BUILD_ARG = "${env.TEST_GLOBAL_PROPERTY ? '--build-arg TEST_GLOBAL_PROPERTY=' + env.TEST_GLOBAL_PROPERTY : ''}"
    }

    stages {
        stage('Test') {
            agent {
                dockerfile {
                    filename 'Dockerfile.centos:7'
                    dir 'utils/docker'
                    label 'docker_runner'
                    additionalBuildArgs  '--build-arg NOBUILD=1 --build-arg UID=$(id -u) $MY_BUILD_ARG'
                }
            }
            steps {
                sh 'hostname; pwd; env'
                println "${env.MY_BUILD_ARG}"
            }
        }
    }
}
