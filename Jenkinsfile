def step_result(name, context, result) {

    println "step_result(" + name + ", " + result + ")"

    currentBuild.result = result

    if (env.CHANGE_ID) {
       if (result == "ABORTED" ||
           result == "UNSTABLE" ||
           result == "FAILURE") {
            pullRequest.comment("Test stage " + name +
                                " completed with status " +
                                result +
                                ".  " + env.BUILD_URL +
                                "display/redirect")
        }

        switch(result) {
            case "UNSTABLE":
                result = "FAILURE"
                break
            case "FAILURE":
                result = "ERROR"
                break
        }
        /* java.lang.IllegalArgumentException: The supplied credentials are invalid to login
         * probably due to changing the account from me to daos-jenkins
         * might need to just recreate the org with the daos-jenkins account
        githubNotify description: name, context: context + "/" + name, status: result
        */
    }
}

def test(script) {
    rc = sh(script: script, returnStatus: true)
    if (rc != 0) {
        println "Calling stepResult"
        stepResult name: env.STAGE_NAME, context: "test", result: "FAILURE"
    } else if (rc == 0) {
        if (sh(script: "grep failed results", returnStatus: true) == 0) {
            println "Calling stepResult"
            stepResult name: env.STAGE_NAME, context: "test", result: "UNSTABLE"
        } else {
            println "Calling stepResult"
            stepResult name: env.STAGE_NAME, context: "test", result: "SUCCESS"
        }
    }
}

pipeline {
    agent none

    stages {
        stage('Test') {
            parallel {
                stage('Test UNSTABLE') {
                    agent any
                    steps {
                        test('''echo failed > results
                                               exit 0''')
                    }
                }
                stage('Test FAILURE') {
                    agent any
                    steps {
                        test('''echo failed > results
                               exit 1''')
                    }
                }
            }
        }
    }
}
