#!/bin/bash
# set -x

SCRIPT=$(basename ${BASH_SOURCE[0]:-$0})
FI_INFO="../../../util/fi_info"
ENODATA=61
DEVICE="cxi1"

usage() {
cat <<EOF1
Usage: ${SCRIPT} -t tapfile
       ${SCRIPT} --tap=tapfile

One of -t or --tap is required.

Runs fi_info on non-existant device to determine if a proper error is reported.
EOF1
exit 0
}

report() {
	local status=$1
	local tapfile=$2

	local result="FAIL"
	local ok="not ok"
	local passing=0
	local failing=1

	if [ $status -eq 0 ]; then
		result="PASS"
		ok="ok"
		passing=1
		failing=0
	fi

	# Mimic criterion output to log
	echo "[====] Running 1 test from $SCRIPT:"
	echo "[$result] $SCRIPT"
	echo "[====] Synthesis: Tested: 1 | Passing: $passing | Failing: $failing | Crashing: 0"

	# Put similar data in tapfile

cat<<EOF2 > $tapfile
TAP version 13
1..1
$ok - fi_info::test for interface not found
EOF2
}

# ################################################################

tapfile=""

while getopts t:-: OPT; do    # allow -t and -- with arg
	# support long options: https://stackoverflow.com/a/28466267/519360
	if [ "$OPT" = "-" ]; then   # long option: reformulate OPT and OPTARG
		OPT="${OPTARG%%=*}"       # extract long option name
		OPTARG="${OPTARG#$OPT}"   # extract long option argument (may be empty)
		OPTARG="${OPTARG#=}"      # if long option argument, remove assigning `=`
	fi
	case "$OPT" in
		t | tap)
			tapfile="$OPTARG"
			;;
		h)
			usage
			;;
		\?)
			exit 1    # bad short option (error reported by getopts)
			;;
		*)
			echo "Illegal option --$OPT"  # bad long option
			exit 1
			;;
	esac
done

if [ -z "$tapfile" ]; then
	usage
fi

test="FI_CXI_DEVICE_NAME=\"${DEVICE}\" ${FI_INFO} -p cxi"

echo "Running test: $test"
eval "$test"
ret=$?

status=1    # bashism: 0 means it passed
if [ $ret -eq $ENODATA ] || [ $ret -eq -$ENODATA ]; then
	status=0
fi

report $status $tapfile

exit $status
