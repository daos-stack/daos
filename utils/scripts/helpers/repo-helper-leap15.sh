#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${BASE_DISTRO:=opensuse/leap:15.5}"
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

# Use local repo server if present
install_curl() {

    if command -v curl; then
        return
    fi

    if command -v dnf; then
        dnf -y install curl
        return
    fi

    if command -v zypper; then
        zypper mr --all --disable
        zypper addrepo                                                                           \
            "${REPO_FILE_URL%/*/}/opensuse-proxy/distribution/leap/${BASE_DISTRO##*:}/repo/oss/" \
              temp_opensuse_oss_proxy
        zypper --non-interactive install curl
        zypper removerepo temp_opensuse_oss_proxy
    fi
}

install_dnf() {

    if command -v dnf; then
        dnf -y install dnf-plugins-core
        return
    fi

    # DO NOT do a "zypper mr --all --disable" here as it rewrites the repo
    # files, removing options that zypper doesn't use but dnf does
    if ! zypper --non-interactive --gpg-auto-import-keys install dnf{,-plugins-core}; then
        rc=${PIPESTATUS[0]}
        echo "Got an $rc error installing dnf?" >&2
        exit "$rc"
    fi
    zypper removerepo temp_opensuse_oss_proxy
}

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

MAJOR_VER="${BASE_DISTRO##*:}"
MAJOR_VER="${MAJOR_VER%%.*}"
if command -v dnf; then
    repos_dir=/etc/yum.repos.d/
else
    repos_dir=/etc/zypp/repos.d/
fi
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    mkdir -p "$repos_dir"
    pushd "$repos_dir"
    curl -k -f -o daos_ci-leap"$MAJOR_VER"-artifactory.repo        \
         "$REPO_FILE_URL"daos_ci-leap"$MAJOR_VER"-artifactory.repo
    disable_repos "$repos_dir"
    popd
    install_dnf
else
    if ! command -v dnf; then
        zypper --non-interactive --gpg-auto-import-keys install \
            dnf dnf-plugins-core
    fi
fi
if [ ! -d /etc/yum.repos.d/ ]; then
    mkdir -p /etc/yum.repos.d/
    pushd "$repos_dir"
    for file in *.repo; do
        if [ ! -s "$file" ]; then
            continue
        fi
        echo "Fixing $file"
        sed -e '/type=NONE/d' < "$file" > "/etc/yum.repos.d/$file"
    done
    popd
fi
if command -v zypper; then
    zypper --non-interactive clean --all
fi
dnf config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False

daos_base="job/daos-stack/job/"
artifacts="/artifact/artifacts/leap15/"
save_repos=()
for repo in $REPOS; do
    # don't install daos@ repos since we are building daos
    if [[ $repo = daos@* ]]; then
        continue
    fi
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
gpgcheck=False\n
module_hotfixes=true\n" >> "$repos_dir$repo:$branch:$build_number".repo
    cat "$repos_dir$repo:$branch:$build_number".repo
    save_repos+=("$repo:$branch:$build_number")
done

disable_repos "$repos_dir" "${save_repos[@]}"

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
