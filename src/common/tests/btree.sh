#!/bin/bash

cwd=$(dirname "$0")
DAOS_DIR=${DAOS_DIR:-$(cd "$cwd/../../.." && echo "$PWD")}
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

function print_help()
{
    cat << EOF
Usage: btree.sh [OPTIONS]
    Options:
        -s [num]  Run with num keys
        ukey      Use integer keys
        perf      Run performance tests
        direct    Use direct string key
EOF
    exit 1
}

PERF=""
UINT=""
while [ $# -gt 0 ]; do
    case "$1" in
    -s)
        shift
        BAT_NUM=$1
        if [ "$BAT_NUM" -ne "$BAT_NUM" ]; then
            echo "Bad argument to -s option.  Must be numeric"
            print_help
        fi
        shift
        ;;
    perf)
        shift
        PERF="on"
        ;;
    ukey)
        shift
        UINT="+"
        ;;
    direct)
        BTR=$DAOS_DIR/build/src/common/tests/btree_direct
        KEYS=${KEYS:-"delta,lambda,kappa,omega,beta,alpha,epsilon"}
        RECORDS=${RECORDS:-"omega:loaded,delta:that,kappa:dice,beta:knows,epsilon:the,lambda:are,alpha:Everybody"}

        shift
        ;;
    *)
        echo "Unknown option $1"
        print_help
        ;;
    esac
done

set -x

if [ -z ${PERF} ]; then

    echo "B+tree functional test..."
    DAOS_DEBUG="$DDEBUG"              \
    "$BTR" -C "${UINT}${IPL}o:$ORDER" \
    -c                                \
    -o                                \
    -u "$RECORDS"                     \
    -i "$IDIR"                        \
    -q                                \
    -f "$KEYS"                        \
    -d "$KEYS"                        \
    -u "$RECORDS"                     \
    -f "$KEYS"                        \
    -r "$KEYS"                        \
    -q                                \
    -u "$RECORDS"                     \
    -q                                \
    -i "$IDIR:3"                      \
    -D

    echo "B+tree batch operations test..."
    "$BTR" -C "${UINT}${IPL}o:$ORDER" \
    -c                                \
    -o                                \
    -b "$BAT_NUM"                     \
    -D
else
    echo "B+tree performance test..."
    "$BTR" -C "${UINT}${IPL}o:$ORDER" \
    -p "$BAT_NUM"                     \
    -D

    echo "B+tree performance test using pmemobj"
    "$BTR" -m                  \
    -C "${UINT}${IPL}o:$ORDER" \
    -p "$BAT_NUM"              \
    -D
fi
