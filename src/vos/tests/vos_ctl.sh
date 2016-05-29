#!/bin/sh

# "on"	turn on zero-copy
# "off"	turn off zero-copy
#
ZC=${ZC:-off}

DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/../../..; echo $PWD)}
VOS_CTL=$DAOS_DIR/build/src/vos/tests/vos_ctl

DDEBUG=${DDEBUG:-0}

# update and fetch parameters for epoch1
#
UPDATA_E1="adam:eva,back:forth,bacon:eggs,bed:breakfast,birds:bees,coat:tie"
FETCH_E1="bacon,bed,birds,coat"

# update and fetch parameters for epoch2:
# - "coat" has no value, so it is a punch
# - "bed" and "birds" are overwritten
# - "rise" is a new one
#
UPDATE_E2="bed:pillow,birds:angry,coat:,rise:fall"
FETCH_E2="bacon,bed,birds,rise,coat"

DAOS_DEBUG=$DDEBUG				\
$VOS_CTL	-e 1 -z $ZC -u $UPDATA_E1	\
		-e 2 -z $ZC -u $UPDATE_E2	\
		-e 1 -z $ZC -f $FETCH_E1	\
		-e 2 -z $ZC -f $FETCH_E2	\
		-e 1 -i -e 2 -I
