#!/bin/bash

set -eux

# functions common to more than one distro specific provisioning
url_to_repo() {
    local url="$1"

    local repo=${url#*://}
    repo="${repo//%/}"
    repo="${repo//\//_}"

    echo "$repo"
}

add_repo() {
    local add_repo="$1"
    local match="${2:-}"
    local gpg_check="${3:-true}"

    if [ -z "$add_repo" ]; then
        # can't add non-existent repos
        return
    fi

    local repo
    # see if a package we know is in the repo is present
    if [ -n "$match" ] &&
       repo=$(dnf repoquery --qf "%{repoid}" "$match" 2>/dev/null |
              grep ..\*); then
        DNF_REPO_ARGS+=" --enablerepo=$repo"
    else
        local repo_url="${REPOSITORY_URL}${add_repo}"
        local repo_name
        repo_name=$(url_to_repo "$repo_url")
        if ! dnf repolist | grep "$repo_name"; then
            dnf config-manager --add-repo="${repo_url}" >&2
            if ! $gpg_check; then
                disable_gpg_check "$repo_url" >&2
            fi
        fi
        DNF_REPO_ARGS+=" --enablerepo=$repo_name"
    fi
}

add_group_repo() {
    local match="${1:-}"

    add_repo "$DAOS_STACK_GROUP_REPO" "$match"
    group_repo_post
}

add_local_repo() {
    add_repo "$DAOS_STACK_LOCAL_REPO" 'argobots' false
}

disable_gpg_check() {
    local url="$1"

    repo="$(url_to_repo "$url")"
    # bug in EL7 DNF: this needs to be enabled before it can be disabled
    dnf config-manager --save --setopt="$repo".gpgcheck=1
    dnf config-manager --save --setopt="$repo".gpgcheck=0
    # but even that seems to be not enough, so just brute-force it
    if [ -d "$REPOS_DIR" ] &&
       [ -f "$REPOS_DIR"/"$repo".repo ]; then
        if ! grep gpgcheck "$REPOS_DIR"/"$repo".repo; then
            echo "gpgcheck=0" >> "$REPOS_DIR"/"$repo".repo
        fi
    else
         echo "Could not find $REPOS_DIR/$repo.repo in $REPOS_DIR:"
         ls -l "$REPOS_DIR"
         exit 1
    fi
}

dump_repos() {
        for file in "$REPOS_DIR"/*.repo; do
            echo "---- $file ----"
            cat "$file"
        done
}

env > /root/last_run-env.txt
mkdir -p /localhome
if userinfo=$(grep ":$MY_UID:$MY_UID:" /etc/passwd); then
    userdel "${userinfo%%:*}"
fi
if groupinfo=$(grep ":$MY_UID:" /etc/group); then
    groupdel "${groupinfo%%:*}"
fi
if ! groupadd -g "$MY_UID" "${REMOTE_ACCT:-jenkins}"; then
  echo "Couldn't add group ${REMOTE_ACCT:-jenkins} "\
       "with gid $MY_UID, pressing on..."
fi
if ! useradd -b /localhome -g "$MY_UID" -u "$MY_UID" \
             -s /bin/bash jenkins; then
  echo "Couldn't add user jenkins with uid $MY_UID, pressing on..."
fi
home=/localhome/${REMOTE_ACCT:-jenkins}
jenkins_ssh="$home"/.ssh
mkdir -p "${jenkins_ssh}"
if ! grep -q -s -f /tmp/ci_key.pub "${jenkins_ssh}/authorized_keys"; then
  cat /tmp/ci_key.pub >> "${jenkins_ssh}/authorized_keys"
fi
root_ssh=/root/.ssh
if ! grep -q -f /tmp/ci_key.pub "${root_ssh}/authorized_keys"; then
  cat /tmp/ci_key.pub >> "${root_ssh}/authorized_keys"
fi
cp /tmp/ci_key.pub "${jenkins_ssh}/id_rsa.pub"
cp /tmp/ci_key "${jenkins_ssh}/id_rsa"
cp /tmp/ci_key_ssh_config "${jenkins_ssh}/config"
chmod 700 "${jenkins_ssh}"
chmod 600 "${jenkins_ssh}"/{authorized_keys,id_rsa*,config}
chown -R "${REMOTE_ACCT:-jenkins}"."${REMOTE_ACCT:-jenkins}" "$home"
echo "${REMOTE_ACCT:-jenkins} ALL=(ALL) NOPASSWD: ALL" > \
      /etc/sudoers.d/"${REMOTE_ACCT:-jenkins}"

if ${FOR_DAOS:-true}; then
    # There is really nothing we want to do for non-daos nodes

    # defined in ci/functional/post_provision_config_nodes_<distro>.sh
    # and catted to the remote node along with this script
    post_provision_config_nodes

    systemctl enable nfs-server.service
    systemctl start nfs-server.service
    sync
    sync
fi
exit 0
