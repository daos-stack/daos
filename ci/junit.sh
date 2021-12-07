#!/bin/bash

set -eux

junit_result() {
    local name="$1"
    local msg="$2"
    local stacktrace="${3:-$(stacktrace "Called from")}"

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

    clush -o '-i ci_key' -l root -w "$nodes" --rcopy /tmp/artifacts --dest "$STAGE_NAME"/framework/

}
