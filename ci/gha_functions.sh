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
    # shellcheck disable=SC2001,SC2153
    url="$(echo "$QUEUE_URL" | sed -e 's|item/\(.*\)/|cancelItem?id=\1|')"

    if ! VERBOSE=true jenkins_curl -X POST "$url"; then
        # it seems hokey but we need to cancel the RETURN trap that jenkins_curl set
        trap '' RETURN
        echo "Failed to cancel cluster provision request."
        return 1
    fi
}

get_test_tags() {
    local stage_tags="$1"

    local test_tags=()
    local tags
    # Test-tag: has higher priority
    if [ -n "${CP_TEST_TAG:-}" ]; then
        tags="$CP_TEST_TAG"
    else
        tags="pr"
        if [ -n "${CP_FEATURES:-}" ]; then
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
    sed -Ene 's/^([-[:alnum:]]+): *([-\._ [:alnum:]]+)$/\1 \2/p' | while read -r a b; do
        echo -n "${a//-/_}" | tr '[:lower:]' '[:upper:]'
        # escape special characters in the value
        echo "=$b" | sed -e 's/\([<> ]\)/\\\1/g'
    done
}

wait_nodes_ready() {

    local wait_seconds=600

    while [ $SECONDS -lt $wait_seconds ]; do
        # shellcheck disable=SC2153
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

# Persist these across calls
cookiejar=""
crumb=""

jenkins_curl() {
    local args=("$@")

    local q=()
    if ${QUIET:-false}; then
        q+=(-s)
    fi
    local v=()
    if ${VERBOSE:-true}; then
        v+=(-v)
    fi
    local headers_file
    # shellcheck disable=SC2154
    trap 'if [ -z "${__bash_unit_current_test__:-}" ]; then set -x; fi; rm -f $headers_file' RETURN
    trap 'if [ -z "${__bash_unit_current_test__:-}" ]; then set -x; fi; rm -f $cookiejar' EXIT
    if [ -z "$cookiejar" ]; then
        cookiejar="$(mktemp)"
        # shellcheck disable=SC2153
        crumb="$(curl -f "${q[@]}" "${v[@]}" --cookie-jar "$cookiejar" \
                 "${JENKINS_URL}crumbIssuer/api/xml?xpath=concat(//crumbRequestField,\":\",//crumb)")"
    fi
    headers_file="$(mktemp)"
    if ! curl -f -D "$headers_file" --cookie "$cookiejar" -H "$crumb" "${q[@]}" "${v[@]}" "${args[@]}"; then
        echo "curl failed" >&2
        cat "$headers_file" >&2
        return 1
    fi
    if [ -e /dev/fd/3 ]; then
        cat "$headers_file" >&3
    fi
}

provision_cluster() {
    set -euxo pipefail
    local stage_name="$1"
    local runid=$2
    local runner=$3
    local reqid=${4:-$(reqidgen)}

    local wait_seconds=600

    echo "CLUSTER_REQUEST_reqid=$reqid" >> "$GITHUB_ENV"
    local url="${JENKINS_URL}job/Get%20a%20cluster/buildWithParameters?token=mytoken&LABEL=$LABEL&REQID=$reqid&BuildPriority=${PRIORITY:-3}"
    if ! queue_url=$(VERBOSE=true jenkins_curl -X POST "$url" 3>&1 >/dev/null |
                     sed -ne 's/\r//' -e '/Location:/s/.*: //p') || [ -z "$queue_url" ]; then
        echo "Failed to request a cluster."
        return 1
    fi
    echo QUEUE_URL="$queue_url" >> "$GITHUB_ENV"
    # disable xtrace here as this could loop for a long time
    set +x
    while [ ! -f /scratch/Get\ a\ cluster/"$reqid" ]; do
        if [ $((SECONDS % 60)) -eq 0 ]; then
            { local cancelled why
              read -r cancelled; read -r why; } < \
                <(VERBOSE=true QUIET=true jenkins_curl "${queue_url}api/json/" |
                  jq -r .cancelled,.why)
            if [ "$cancelled" == "true" ]; then
                echo "Cluster request cancelled from Jenkins"
                exit 1
            fi
            echo "$why"
        fi
        sleep 1
    done
    set -x
    local nodestring
    nodestring=$(cat /scratch/Get\ a\ cluster/"$reqid")
    if [ "$nodestring" = "cancelled" ]; then
        echo "Cluster request cancelled from Jenkins"
        return 1
    fi
    { echo "NODESTRING=$nodestring"
      echo "NODELIST=$nodestring"; } >> "$GITHUB_ENV"
    echo "NODE_COUNT=$(echo "$nodestring" | tr ',' ' ' | wc -w)" >> "$GITHUB_ENV"
    if [[ $nodestring = *vm* ]]; then
        ssh -oPasswordAuthentication=false -v root@"${nodestring%%vm*}" \
            "POOL=${CP_PROVISIONING_POOL:-}
             NODESTRING=$nodestring
             NODELIST=$nodestring
             DISTRO=$DISTRO_WITH_VERSION
             $(cat ci/provisioning/provision_vm_cluster.sh)"
    else
        local START=$SECONDS
        while [ $((SECONDS-START)) -lt $wait_seconds ]; do
            if clush -B -S -l root -w "$nodestring" '[ -d /var/chef/reports ]'; then
                # shellcheck disable=SC2016
                clush -B -S -l root -w "$nodestring" --connect_timeout 30 --command_timeout 600 "if [ -e /root/job_info ]; then
                        cat /root/job_info
                    fi
                    echo \"Last provisioning run info:
GitHub Actions URL: https://github.com/daos-stack/daos/actions/runs/$runid
Runner: $runner
Stage Name: $stage_name\" > /root/job_info
                    distro=$DISTRO_WITH_VERSION
                    if ! POOL=\"${CP_PROVISIONING_POOL:-}\" restore_partition.sh daos_ci-\${distro} noreboot; then
                        rc=\${PIPESTATUS[0]}
                        # TODO: this needs to be derived from the stage name
                        while [[ \$distro = *.* ]]; do
                            distro=\${distro%.*}
                                if ! restore_partition.sh daos_ci-\${distro} noreboot; then
                                    rc=\${PIPESTATUS[0]}
                                    continue
                                else
                                    exit 0
                                fi
                        done
                        exit \"\$rc\"
                        fi"
                    clush -B -S -l root -w "$nodestring" --connect_timeout 30 --command_timeout 120 -S 'init 6' || true
                    START=$SECONDS
                    while [ $((SECONDS-START)) -lt $wait_seconds ]; do
                        if clush -B -S -l root -w "$nodestring" '[ -d /var/chef/reports ]'; then
                            exit 0
                        fi
                        sleep 1
                    done
                    exit 1
            fi
            sleep 1
        done
        exit 1
    fi

}


# This is run under the unit test framework at https://github.com/pgrange/bash_unit/
# I.e. ../bash_unit/bash_unit ci/gha_functions.sh
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

'"'"'Will-not-be-a-pragma: false'"'"' should not be considered a commit
pragma, but:
Should-not-be-a-pragma: bar will be because it was not quoted.

Skip-func-test-leap15: false
RPM-test-version: 2.5.100-13.10036.g65926e32
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
    assert_equals "$(echo "$msg" | get_commit_pragmas)" 'SHOULD_NOT_BE_A_PRAGMA=bar\ will\ be\ because\ it\ was\ not\ quoted.
SKIP_FUNC_TEST_LEAP15=false
RPM_TEST_VERSION=2.5.100-13.10036.g65926e32
SKIP_PR_COMMENTS=true
TEST_TAG=always_passes\ always_fails
EL8_VM9_LABEL=all_vm9
EL9_VM9_LABEL=all_vm9
LEAP15_VM9_LABEL=all_vm9
HW_MEDIUM_LABEL=new_icx5
HW_LARGE_LABEL=new_icx9
REQUIRED_GITHOOKS=true'

}

test_jenkins_curl() {
    JENKINS_URL="${JENKINS_URL:-https://build.hpdd.intel.com/}"
    assert_equals "$(QUIET=true VERBOSE=false jenkins_curl -X POST "${JENKINS_URL}api/xml" 3>&1 >/dev/null | tr -d '\r' | grep '^X-Content-Type-Options:')" "X-Content-Type-Options: nosniff"
}
