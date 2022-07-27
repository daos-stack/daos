#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${BASE_DISTRO:=opensuse/leap:15.3}"
: "${JENKINS_URL:=}"
: "${REPOS:=}"

disable_repo () {
    if [ -n "$REPO_FILE_URL" ]; then
        pushd /etc/dnf/repos.d/
            mv daos_ci-leap15-artifactory.repo{,.tmp}
            for file in *.repo; do
                true > "$file"
            done
            mv daos_ci-leap15-artifactory.repo{.tmp,}
        popd
    fi
}

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

if [ -n "$REPO_FILE_URL" ]; then
    zypper mr --all --disable
    zypper addrepo                                                            \
        "${REPO_FILE_URL%/*/}/opensuse-proxy/distribution/leap/${BASE_DISTRO##*:}/repo/oss/" \
          temp_opensuse_oss_proxy
    zypper --non-interactive install curl
    mkdir -p /etc/zypp/repos.d
    pushd /etc/zypp/repos.d/
        curl -k -f -o daos_ci-leap15-artifactory.repo.tmp \
             "$REPO_FILE_URL"daos_ci-leap15-artifactory.repo
        for file in *.repo; do
            true > $file
        done
        mv daos_ci-leap15-artifactory.repo{.tmp,}
    popd
fi
zypper --non-interactive --gpg-auto-import-keys install \
    dnf dnf-plugins-core
mkdir -p /etc/dnf/repos.d
pushd /etc/zypp/repos.d/
    for file in *.repo; do
        sed -e '/type=NONE/d' < $file > /etc/dnf/repos.d/$file
    done
popd
zypper --non-interactive clean --all
dnf config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False

for repo in $REPOS; do
    branch="master"
    build_number="lastSuccessfulBuild"
    if [[ $repo = *@* ]]; then
        branch="${repo#*@}"
        repo="${repo%@*}"
        if [[ $branch = *:* ]]; then
            build_number="${branch#*:}"
            branch="${branch%:*}"
        fi
    fi
    echo -e "[$repo:$branch:$build_number]\n\
name=$repo:$branch:$build_number\n\
baseurl=${JENKINS_URL}job/daos-stack/job/$repo/job/$branch/$build_number/artifact/artifacts/leap15/\n\
enabled=1\n\
gpgcheck=False\n" >> /etc/dnf/repos.d/$repo:$branch:$build_number.repo
    cat /etc/dnf/repos.d/$repo:$branch:$build_number.repo
done

if [ -e /tmp/install.sh ]; then
    dnf upgrade
    disable_repo
    /tmp/install.sh
    dnf clean all
    rm -f /tmp/install.sh
fi

if [ -e /etc/profile.d/lmod.sh ]; then
    if ! grep MODULEPATH=.*/usr/share/modules /etc/profile.d/lmod.sh; then
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/' \
               /etc/profile.d/lmod.sh
    fi
fi

# this should not be needed, but docker containers have for leap 15
# have failed in the past if it is not present.
update-ca-certificates
