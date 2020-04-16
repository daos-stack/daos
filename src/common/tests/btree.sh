#!/bin/bash

cwd=$(dirname "$0")
DAOS_DIR=${DAOS_DIR:-$(cd "$cwd/../../.." && echo "$PWD")}
BTR=$DAOS_DIR/build/src/common/tests/btree
VCMD=()
if [ "$USE_VALGRIND" = "yes" ]; then
    VCMD=("valgrind" "--tool=pmemcheck")
fi

ORDER=${ORDER:-3}
DDEBUG=${DDEBUG:-0}
BAT_NUM=${BAT_NUM:-"200000"}


KEYS=${KEYS:-"3,6,5,7,2,1,4"}
RECORDS=${RECORDS:-"7:loaded,3:that,5:dice,2:knows,4:the,6:are,1:Everybody"}

function print_help()
{
    cat << EOF
Usage: btree.sh [OPTIONS]
    Options:
        -s [num]  Run with num keys
        dyn       Run with dynamic root
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
    dyn)
        DYN="-t"
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
set -e

gen_test_conf_string()
{
        name=""
        [ ! -z "$1" ] && name="${name}_IDIR=$1"
        [ ! -z "$2" ] && name="${name}_IPL=$2"
        [ ! -z "$3" ] && name="${name}_PMEM=$3"
        echo "$name"
}

run_test()
{
    printf "\nOptions: IPL='%s' IDIR='%s' PMEM='%s'\n" "$IPL" "$IDIR" "$PMEM"
    if [ -z ${PERF} ]; then

        test_conf=$(gen_test_conf_string "${IDIR}" "${IPL}" "${PMEM}")

        echo "B+tree functional test..."
        DAOS_DEBUG="$DDEBUG"                        \
        "${VCMD[@]}" "$BTR" \
        --start-test "BTR030_functional_test_${test_conf}" \
        "${DYN}" "${PMEM}" -C "${UINT}${IPL}o:$ORDER" \
        -c                                          \
        -o                                          \
        -u "$RECORDS"                               \
        -i "$IDIR"                                  \
        -q                                          \
        -f "$KEYS"                                  \
        -d "$KEYS"                                  \
        -u "$RECORDS"                               \
        -f "$KEYS"                                  \
        -r "$KEYS"                                  \
        -q                                          \
        -u "$RECORDS"                               \
        -q                                          \
        -i "$IDIR:3"                                \
        -D

        echo "B+tree batch operations test..."
        "${VCMD[@]}" "$BTR" \
        --start-test "BTR031_batch_operations_test_${test_conf}" \
        "${DYN}" "${PMEM}" -C "${UINT}${IPL}o:$ORDER" \
        -c                                          \
        -o                                          \
        -b "$BAT_NUM"                               \
        -D

        echo "B+tree drain test..."
        "${VCMD[@]}" "$BTR" \
        --start-test "BTR032_drain_test_${test_conf}" \
        "${DYN}" "${PMEM}" -C "${UINT}${IPL}o:$ORDER" \
        -e -D

    else
        echo "B+tree performance test..."
        "${VCMD[@]}" "$BTR" \
        --start-test "BTR033_performance_test_${test_conf}" \
        "${DYN}" "${PMEM}" -C "${UINT}${IPL}o:$ORDER" \
        -p "$BAT_NUM"                               \
        -D
    fi
}

for IPL in "i," ""; do
    for IDIR in "f" "b"; do
        for PMEM in "-m" ""; do
            run_test
        done
    done
done
