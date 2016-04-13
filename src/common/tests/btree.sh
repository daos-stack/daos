#!/bin/sh

DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/../../..; echo $PWD)}
BTR=$DAOS_DIR/build/common/tests/btree

ORDER=${ORDER:-3}
DDEBUG=${DDEBUG:-0}
INPLACE=${INPLACE:-"no"}

IPL=""
if [ "x$INPLACE" == "xyes" ]; then
	IPL="i,"
fi

KEYS=${KEYS:-"3,6,5,7,2,1,4"}
RECORDS=${RECORDS:-"7:loaded,3:that,5:dice,2:knows,4:the,6:are,1:Everybody"}

DAOS_DEBUG=$DDEBUG			\
$BTR	-C ${IPL}o:$ORDER		\
	-c				\
	-o				\
	-u $RECORDS			\
	-i				\
	-f $KEYS			\
	-D
