#!/bin/bash

set -eux

junit_result() {
    local name="$1"
    local msg="$2"
    local stacktrace="${3:-$(stacktrace "Called from" 1)}"

    echo -e "$msg"

    cat <<EOF > results.xml
  <testcase classname="$HOSTNAME" name="$name" time="0">
    <failure message="$msg" type="TestFail"><![CDATA[$stacktrace]]></failure>
  </testcase>
EOF
}

report_junit() {
    local name="$1"
    local results="$2"
    local nodes="$3"

    clush -o '-i ci_key' -l root -w "$nodes" --rcopy "$results"

    local results_files
    results_files=("$results".*)

    if [ ${#results_files[@]} -eq 0 ]; then
        echo "No results found to report as JUnit results"
        ls -l
        return
    fi

    mkdir -p "$STAGE_NAME"/framework/

    cat <<EOF > "$STAGE_NAME"/framework/framework_results.xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuite errors="${#results_files[@]}" failures="0" name="$name" skipped="0"
           tests="${#results_files[@]}" time="0" timestamp="$(date +%FT%T)">
$(cat "${results_files[@]}")
</testsuite>
EOF

    clush -o '-i ci_key' -l root -w "$nodes" --rcopy /var/tmp/artifacts \
                                             --dest "$STAGE_NAME"/framework/

}

# create this dir so that the remote copy doesn't fail if nothing actually populates it
mkdir -p /var/tmp/artifacts

# functions should inherit the ERR trap
set -E

# set an error trap to create a junit result for any unhandled error
trap 'junit_result "UnhandledError" "Unhandled error in ${STAGE_NAME} Post Restore Script step"' ERR
