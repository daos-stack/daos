#!/bin/sh
# this script is for convenience, edit as appropriate for your
# installation location


# if environment indicates PSM2 is in use, add orterun settings
MESSAGING=
if [ "$CRT_PHY_ADDR_STR" = "ofi+psm2" ]; then
    MESSAGING="--mca mtl ^psm2,ofi"
fi

#BASE=set-daos-base-path-here
$BASE/install/bin/orterun --quiet $MESSAGING --np 1 \
    --ompi-server file:$BASE/install/tmp/urifile \
    $BASE/install/bin/daosctl $@
