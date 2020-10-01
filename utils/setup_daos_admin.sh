#!/bin/bash

set -ue

die()
{
        msg=${1:-"unknown error"}
        echo "$msg" >&2
        exit 1
}

check_environment()
{
        if [ -z "${SL_PREFIX:-""}" ]; then
                die "no DAOS development environment (use setup_local.sh ?)"
        fi

}

check_which()
{
        cmd=$1
        if ! command -v "$cmd" >/dev/null 2>&1; then
                die "$cmd not found in \$PATH (yum install $cmd ?)"
        fi
}

check_prereqs()
{
        if [ $EUID != 0 ]; then
                die "this script must be run as root or with sudo"
        fi

        check_which patchelf

        DA_SRC=$1
        if ! [ -f "$DA_SRC" ]; then
                die "$DA_SRC does not exist. did you build it?"
        fi
}

check_environment

if [ $# -eq 1 ]; then
        DAOS_LOC=$1
else
        DAOS_LOC=$SL_PREFIX
fi

DA_SRC=$DAOS_LOC/bin/daos_admin
DA_DST=/usr/bin/daos_admin

echo "This script will install daos_admin for developer builds (not for production)."

echo -n "Installing $DA_SRC -> $DA_DST ... "
chmod -x "$DA_SRC" || true
cp "$DA_SRC" "$DA_DST"
if [ "$SL_PREFIX" != "DAOS_LOC" ]; then
        rpath=$(patchelf --print-rpath $DA_DST | \
              sed "s|$SL_PREFIX|$DAOS_LOC|g")
        patchelf --set-rpath "$rpath" $DA_DST
fi
chmod 4755 "$DA_DST"
echo "Done."

USR_SPDK=/usr/share/spdk
USR_CTL=/usr/share/daos/control
echo -n "Creating SPDK script links under $USR_SPDK ... "
mkdir -p "$USR_SPDK/scripts" "$USR_CTL"
if ! [ -e "$USR_SPDK/scripts/setup.sh" ]; then
        ln -sf "$SL_PREFIX/share/spdk/scripts/setup.sh" "$USR_SPDK/scripts"
fi
if ! [ -e "$USR_SPDK/scripts/common.sh" ]; then
        ln -sf "$SL_PREFIX/share/spdk/scripts/common.sh" "$USR_SPDK/scripts"
fi
if ! [ -e "$USR_SPDK/include" ]; then
        ln -s "$SL_PREFIX/include" "$USR_SPDK"/include
fi
if ! [ -e "$USR_CTL/setup_spdk.sh" ]; then
	ln -s "$SL_PREFIX/share/daos/control/setup_spdk.sh" "$USR_CTL"
fi
echo "Done."
