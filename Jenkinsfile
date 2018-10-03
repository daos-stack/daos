def step_result(name, result) {

    println "step_result(" + name + ", " + result + ")"

    if (env.CHANGE_ID &&
       (result == "ABORTED" ||
        result == "UNSTABLE" ||
        result) == "FAILURE") {
        pullRequest.comment("Test stage " + name +
                            " completed with status " +
                            result +
                            ".  " + env.BUILD_URL +
                            "display/redirect")
        currentBuild.result = result
    } else {
        println "Not posting a comment for status " + result
    }
}

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
                        step_result(env.STAGE_NAME, "FAILURE")
                    } else {
                        if (sh(script: "grep failed results", returnStatus: true) == 0) {
                            println "Job UNSTABLE"
                            step_result(env.STAGE_NAME, "UNSTABLE")
                        }
                    }
                }
            }
        }
    }
}
