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
    local repo="$1"
    local gpg_check="${2:-true}"

    if [ -n "$repo" ]; then
        repo="${REPOSITORY_URL}${repo}"
        if ! dnf repolist | grep "$(url_to_repo "$repo")"; then
            dnf config-manager --add-repo="${repo}"
            if ! $gpg_check; then
                disable_gpg_check "$repo"
            fi
        fi
    fi
}

disable_gpg_check() {
    local url="$1"

    repo="$(url_to_repo "$url")"
    # bug in EL7 DNF: this needs to be enabled before it can be disabled
    dnf config-manager --save --setopt="$repo".gpgcheck=1
    dnf config-manager --save --setopt="$repo".gpgcheck=0
    # but even that seems to be not enough, so just brute-force it
    if [ -d /etc/yum.repos.d ] &&
       ! grep gpgcheck /etc/yum.repos.d/"$repo".repo; then
        echo "gpgcheck=0" >> /etc/yum.repos.d/"$repo".repo
    fi
}

dump_repos() {
        for file in "$REPOS_DIR"/*.repo; do
            echo "---- $file ----"
            cat "$file"
        done
}

env > /root/last_run-env.txt
if ! grep ":$MY_UID:" /etc/group; then
  groupadd -g "$MY_UID" jenkins
fi
mkdir -p /localhome
if ! grep ":$MY_UID:$MY_UID:" /etc/passwd; then
  useradd -b /localhome -g "$MY_UID" -u "$MY_UID" -s /bin/bash jenkins
fi
mkdir -p /localhome/jenkins/.ssh
cat /tmp/ci_key.pub >> /localhome/jenkins/.ssh/authorized_keys
cat /tmp/ci_key.pub >> /root/.ssh/authorized_keys
mv /tmp/ci_key.pub /localhome/jenkins/.ssh/id_rsa.pub
mv /tmp/ci_key /localhome/jenkins/.ssh/id_rsa
mv /tmp/ci_key_ssh_config /localhome/jenkins/.ssh/config
chmod 700 /localhome/jenkins/.ssh
chmod 600 /localhome/jenkins/.ssh/{authorized_keys,id_rsa*,config}
chown -R jenkins.jenkins /localhome/jenkins/
echo "jenkins ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/jenkins

# defined in ci/functional/post_provision_config_nodes_<distro>.sh
# and catted to the remote node along with this script
post_provision_config_nodes

systemctl enable nfs-server.service
systemctl start nfs-server.service
sync
sync
exit 0
