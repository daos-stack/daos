#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${HTTPS_PROXY:=}"
: "${DAOS_LAB_CA_FILE_UR:=}"
: "${REPOSITORY_NAME:=artifactory}"
: "${BASE_DISTRO:=24.04}"

disable_repos () {
    if [ -e /etc/apt/sources.list.d/ubuntu.sources ];then
        mv /etc/apt/sources.list.d/ubuntu.sources \
           etc/apt/sources.list.d/ubuntu.sources.disabled
    elif [ -e /etc/apt/sources.list ];then
        mv /etc/apt/sources.list \
           etc/apt/sources.list.disabled
    fi
}

# Use local repo server if present
install_curl() {

    if command -v curl; then
        echo "found curl!"
        return
    else
        apt-get update
        apt-get install curl
    fi
    apt-get update
    apt-get install ca-certificates gpg gpg-agent locales
    if [[ "$BASE_DISTRO" == "20."* ]]; then
        apt-get install software-properties-common
    fi
    if command -v wget; then
        echo "found wget!"
        return
    fi
}

# Use local repo server if present
install_optional_ca() {
    ca_storage="/usr/local/share/ca-certificates/"
    if [ -n "$DAOS_LAB_CA_FILE_URL" ]; then
        curl -k --noproxy '*' -sSf -o "${ca_storage}lab_ca_file.crt" \
             "$DAOS_LAB_CA_FILE_URL"
        update-ca-certificates
    fi
}

echo "APT::Get::Assume-Yes \"true\";" > /etc/apt/apt.conf.d/no-prompt
echo "APT::Install-Recommends \"false\";" > /etc/apt/apt.conf.d/no-recommends
if [ -n "$HTTPS_PROXY" ];then
    apt_proxy="http://${HTTPS_PROXY##*//}"
    echo "Acquire::http::Proxy \"$apt_proxy\";" > \
        /etc/apt/apt.conf.d/local_proxy
    if [ -n "$REPO_FILE_URL" ]; then
        direct="${REPO_FILE_URL##*//}"
        direct="${direct%%/*}"
    echo "Acquire::http::Proxy { $direct DIRECT; };" >> \
        /etc/apt/apt.conf.d/local_proxy
    fi
fi

# These need to be installed here until we can figure out how to
# get them installed from a local repository.
apt-get update
apt-get install ca-certificates gpg gpg-agent

# Need newer golang than in distro, so need a PPA repository
apt-get install software-properties-common

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.
DISTRO_VERSION="${BASE_DISTRO##*:}"
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    install_optional_ca
    curl -k --noproxy '*' -sSf                               \
         -o "daos_ci-ubuntu${DISTRO_VERSION}-${REPOSITORY_NAME}.list" \
         "${REPO_FILE_URL}daos_ci-ubuntu${DISTRO_VERSION}-${REPOSITORY_NAME}.list"
    disable_repos
    mkdir -p /usr/local/share/keyrings/
    curl --noproxy '*' -sSf -O "$REPO_FILE_URL"esad_repo.key
    gpg --no-default-keyring --keyring ./temp-keyring.gpg            \
        --import esad_repo.key
    gpg --no-default-keyring --keyring ./temp-keyring.gpg --export   \
        --output /usr/local/share/keyrings/daos-stack-public.gpg
fi

# Need newer golang than in distro
apt-get update
apt-get upgrade
add-apt-repository ppa:longsleep/golang-backports

apt-get update
if [ -e /tmp/install.sh ]; then
    chmod +x /tmp/install.sh
    /tmp/install.sh
fi
apt-get clean all
