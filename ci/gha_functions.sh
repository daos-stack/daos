#!/bin/bash

# TODO: this should produce a JUnit result
error_exit() {
    echo "$1"
    exit 1
}

get_repo_path() {
    # shellcheck disable=SC2153
    local repo_path="$REPO_PATH$GITHUB_RUN_NUMBER/artifact/artifacts/"
    mkdir -p "$repo_path"

    echo "$repo_path"

}

cleanup_provision_request () {
    local reqid="$1"
    local file="/scratch/Get a cluster/$reqid"
    local n=3

    while [ $n -gt 0 ] && [ -e "$file" ]; do
        if ! rm -f "$file"; then
            echo "Failed to remove $file"
            id
            ls -l "$file"
            exit 1
        fi
        # make sure it was removed; we have seen where it was not
        if [ ! -e "$file" ]; then
            return
        fi
        echo "Somehow even after removal $file exists, trying again"
        ((n--)) || true
    done
    if [ -e "$file" ]; then
        echo "Complete failure to remove $file, exiting error"
        exit 1
    fi
}

cancel_provision() {
    trap 'rm -f $cookiejar' EXIT
    cookiejar="$(mktemp)"
    # shellcheck disable=SC2153
    crumb="$(curl --cookie-jar "$cookiejar" \
             "${JENKINS_URL}crumbIssuer/api/xml?xpath=concat(//crumbRequestField,\":\",//crumb)")"
    # shellcheck disable=SC2001
    url="$(echo "$QUEUE_URL" | sed -e 's|item/\(.*\)/|cancelItem?id=\1|')"
    trap 'set -x; rm -f $cookiejar $headers_file' EXIT
    headers_file="$(mktemp)"
    if ! curl -D "$headers_file" -v -f -X POST --cookie "$cookiejar" -H "$crumb" "$url"; then
        echo "Failed to cancel cluster provision request."
        cat "$headers_file"
        return 1
    fi
}

get_test_tags() {
    local stage_tags="$1"

    local test_tags=()
    local tags
    # Test-tag: has higher priority
    if [ -n "$CP_TEST_TAG" ]; then
        tags="$CP_TEST_TAG"
    else
        tags="pr"
        if [ -n "$CP_FEATURES" ]; then
            for feature in $CP_FEATURES; do
                tags+=" daily_regression,$feature full_regression,$feature"
            done
        fi
    fi
    for tag in $tags; do
        test_tags+=("$tag,$stage_tags")
    done

    echo "${test_tags[@]}"
}


get_commit_pragmas() {
    sed -ne '/^[^ ]*: */s/\([^:]*\): *\(.*\)/\1 \2/p' | while read -r a b; do
        echo -n "${a//-/_}" | tr '[:lower:]' '[:upper:]'
        # escape special characters in the value
        echo "=$b" | sed -e 's/\([<> ]\)/\\\1/g'
    done
}

wait_nodes_ready() {

    local wait_seconds=600

    while [ $SECONDS -lt $wait_seconds ]; do
        if clush -B -S -l root -w "$NODESTRING" "set -eux;
          while [ \$SECONDS -lt \$(($wait_seconds-\$SECONDS)) ]; do
              if [ -d /var/chef/reports ]; then
                  exit 0;
              fi;
              sleep 10;
          done;
          exit 1"; then
              exit 0;
          fi;
    done;
    exit 1
}

escape_single_quotes() {
    sed -e "s/'/'\"'\"'/g"
}

# This is run under the unit test framework at https://github.com/pgrange/bash_unit/
test_test_tag_and_features() {
    # Simple Test-tag: test
    assert_equals "$(CP_TEST_TAG="always_passes always_fails" get_test_tags "-hw")" "always_passes,-hw always_fails,-hw"
    # Simple Features: test (no Test-tag:)
    assert_equals "$(CP_FEATURES="always_passes" get_test_tags "-hw")" \
                  "pr,-hw daily_regression,always_passes,-hw full_regression,always_passes,-hw"
    assert_equals "$(CP_FEATURES="foo bar" get_test_tags "-hw")" \
                  "pr,-hw daily_regression,foo,-hw full_regression,foo,-hw daily_regression,bar,-hw full_regression,bar,-hw"
    # Features: and Test-tag:
    assert_equals "$(CP_TEST_TAG="always_passes always_fails"
                     CP_FEATURES="foo bar" get_test_tags "-hw")" "always_passes,-hw always_fails,-hw"
}

test_get_commit_pragmas() {
    local msg='Escape spaces also
    
Skip-func-test-leap15: false
Skip-PR-comments: true
Test-tag: always_passes always_fails
EL8-VM9-label: all_vm9
EL9-VM9-label: all_vm9
Leap15-VM9-label: all_vm9
HW-medium-label: new_icx5
HW-large-label: new_icx9

Required-githooks: true

Signed-off-by: Brian J. Murrell <brian.murrell@intel.com>
'
    assert_equals "$(echo "$msg" | get_commit_pragmas)" 'SKIP_FUNC_TEST_LEAP15=false
SKIP_PR_COMMENTS=true
TEST_TAG=always_passes\ always_fails
EL8_VM9_LABEL=all_vm9
EL9_VM9_LABEL=all_vm9
LEAP15_VM9_LABEL=all_vm9
HW_MEDIUM_LABEL=new_icx5
HW_LARGE_LABEL=new_icx9
REQUIRED_GITHOOKS=true
SIGNED_OFF_BY=Brian\ J.\ Murrell\ \<brian.murrell@intel.com\>'

}