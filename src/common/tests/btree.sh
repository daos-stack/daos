#!/bin/sh

DAOS_DIR=${DAOS_DIR:-$(cd $(dirname $0)/../../..; echo $PWD)}
BTR=$DAOS_DIR/build/src/common/tests/btree

ORDER=${ORDER:-3}
DDEBUG=${DDEBUG:-0}
INPLACE=${INPLACE:-"no"}
BACKWARD=${BACKWARD:-"no"}
BAT_NUM=${BAT_NUM:-"200000"}

IPL=""
if [ "x$INPLACE" == "xyes" ]; then
	IPL="i,"
fi

IDIR="f"
if [ "x$BACKWARD" == "xyes" ]; then
	IDIR="b"
fi

KEYS=${KEYS:-"3,6,5,7,2,1,4"}
RECORDS=${RECORDS:-"7:loaded,3:that,5:dice,2:knows,4:the,6:are,1:Everybody"}

echo "B+tree functional test..."
DAOS_DEBUG=$DDEBUG			\
$BTR	-C ${IPL}o:$ORDER		\
	-c				\
	-o				\
	-u $RECORDS			\
	-i $IDIR			\
	-q				\
	-f $KEYS			\
	-d $KEYS			\
    -u $RECORDS         \
    -f $KEYS            \
    -r $KEYS            \
	-q				\
	-u $RECORDS			\
	-q				\
	-i $IDIR:3			\
	-D

echo "B+tree batch operations test..."
$BTR	-C ${IPL}o:$ORDER		\
	-c				\
	-o				\
	-b $BAT_NUM			\
	-D
