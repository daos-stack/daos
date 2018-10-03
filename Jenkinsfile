pipeline {
    agent none

    stages {
        stage('Test') {
            agent any
            steps {
                script {
                    rc = sh(script: '''echo failed > results
                                       exit 1''',
                            returnStatus: true)
                    if (rc != 0) {
                        println "Job FAILURE"
                    }
                    rc = sh(script: "grep failed results", returnStatus: true)
                    if (rc != 0) {
                        println "Job UNSTABLE"
                    }
                }
            }
        }
    }
}
