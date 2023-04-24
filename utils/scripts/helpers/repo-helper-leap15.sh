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

# shellcheck disable=SC2120
disable_repos () {
    local repos_dir="$1"
    shift
    local save_repos
    IFS=" " read -r -a save_repos <<< "${*:-} daos_ci-leap15-artifactory"
    if [ -n "$REPO_FILE_URL" ]; then
        pushd "$repos_dir"
        local repo
        for repo in "${save_repos[@]}"; do
            mv "$repo".repo{,.tmp}
        done
        for file in *.repo; do
            true > "$file"
        done
        for repo in "${save_repos[@]}"; do
            mv "$repo".repo{.tmp,}
        done
        popd
    fi
}

install_curl() {

    if command -v curl; then
        return
    fi

    zypper mr --all --disable
    zypper addrepo                                                                           \
        "${REPO_FILE_URL%/*/}/opensuse-proxy/distribution/leap/${BASE_DISTRO##*:}/repo/oss/" \
          temp_opensuse_oss_proxy
    zypper --non-interactive install curl
    zypper removerepo temp_opensuse_oss_proxy
}

install_dnf() {

    if command -v dnf; then
        return
    fi

    zypper mr --all --disable
    zypper addrepo                                                                           \
        "${REPO_FILE_URL%/*/}/opensuse-proxy/distribution/leap/${BASE_DISTRO##*:}/repo/oss/" \
          temp_opensuse_oss_proxy
    zypper --non-interactive install dnf{,-plugins-core}
    zypper removerepo temp_opensuse_oss_proxy
    zypper mr --all --enable
}

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

MAJOR_VER="${BASE_DISTRO##*:}"
MAJOR_VER="${MAJOR_VER%%.*}"
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    mkdir -p /etc/zypp/repos.d
    pushd /etc/zypp/repos.d/
    curl -k -f -o daos_ci-leap$MAJOR_VER-artifactory.repo        \
         "$REPO_FILE_URL"daos_ci-leap$MAJOR_VER-artifactory.repo
    disable_repos /etc/zypp/repos.d/
    popd
    install_dnf
else
    zypper --non-interactive --gpg-auto-import-keys install \
        dnf dnf-plugins-core
fi
mkdir -p /etc/dnf/repos.d
pushd /etc/zypp/repos.d/
for file in *.repo; do
    sed -e '/type=NONE/d' < "$file" > "/etc/dnf/repos.d/$file"
done
popd
zypper --non-interactive clean --all
dnf config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False

daos_base="job/daos-stack/job/"
artifacts="/artifact/artifacts/leap15/"
save_repos=()
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
baseurl=${JENKINS_URL}$daos_base$repo/job/$branch/$build_number$artifacts\n\
enabled=1\n\
gpgcheck=False\n" >> /etc/dnf/repos.d/$repo:$branch:$build_number.repo
    cat /etc/dnf/repos.d/$repo:$branch:$build_number.repo
    save_repos+=("$repo:$branch:$build_number")
done

if [ -e /tmp/install.sh ]; then
    dnf upgrade
    disable_repos /etc/dnf/repos.d/ "${save_repos[@]}"
    /tmp/install.sh
    dnf clean all
    rm -f /tmp/install.sh
fi

if [ -e /etc/profile.d/lmod.sh ]; then
    if ! grep "MODULEPATH=.*/usr/share/modules" /etc/profile.d/lmod.sh; then
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/' \
               /etc/profile.d/lmod.sh
    fi
fi

# This command should not be needed, but docker containers for leap 15 have
# in the past failed to validate HTTPS certificates if this command is not
# run here.  Running this command just makes sure things work.
update-ca-certificates
