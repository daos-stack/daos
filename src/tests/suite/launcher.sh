#!/bin/sh
# This script acts as a wrapper around daos_test.  It starts and stops
# daos servers and cleans up any pools left by the tests.  This script
# presumes the directory layout you'd find after a default build of daos.

function usage {
    echo "launcher.sh hostfile outputdir bindir"\
    " [-c threadcount] [-t tests-to-run] [-u subtests-to-run] [-h]"
}

function count_servers {
    cat $1 | wc -l
}

# start daos servers, one for each host in hostfile
function start_servers {
    echo "starting servers"
    $1/orterun --np $2 --hostfile $3 --enable-recovery -x D_LOG_MASK=INFO \
	-x D_LOG_FILE=$4/daos.log $1/daos_server -g daos_server -c $5 -a$4
}

# this is the elegant way to do this
# doesn't actually seem to work, disappointing since it works from C
# leaving it in case someone smarter than I am wants to fix it
function kill_servers1 {
    echo "stopping servers"
    kill -2 $1
}

# this is the big hammer but it has the benefit of actually working
function kill_servers2 {
    echo "stopping servers"
    cat $1 | while read hostline
    do
	tokens=( $hostline )
	echo "killing " ${tokens[0]}
	ssh -n ${tokens[0]} "pkill daos_io_server && pkill daos_server"
    done
}

# clean out the tmpfs directory on all the servers
function delete_pools {
    echo "deleting pools"
    cat $1 | while read hostline
    do
	tokens=( $hostline )
	ssh -n ${tokens[0]} "rm -rf /mnt/daos/*"
    done
}

# run the test suite
function start_test {
    echo "starting test"
    if [[ $3 ]]
    then
	# sub_test provided
	$1/daos_test -$2 -u $3
    else
	$1/daos_test -$2
    fi
}

# deal with default test list or -a
function preprocess_tests {
    # not specified then default is all
    if [ -z "$tests" ]; then
	tests="mpcCiAeoROdr";
    fi

    # if all specified with a then change it to
    # a list contain all the tests
    if [[ $test =~ "a" ]]; then
	tests="mpcCiAeoROdr";
    fi
}

# start main script that coordinates all the action
if [ "$#" -lt 3 ]; then
    usage
    exit 1;
fi

# these are required positional parameters
hostfile=$1
outputdir=$2
bindir=$3

if [ ! -d  $outputdir ]; then
    echo "invalid output directory parameter"
    usage
    exit 1;
fi
if [ ! -d  $bindir ]; then
    echo "invalid bin directory parameter"
    usage
    exit 1;
fi
if [ ! -f  $hostfile ]; then
    echo "hostfile not found"
    usage
    exit 1;
fi

# there are a couple optional parameters too
while [ "$4" != "" ]; do
    case $4 in
        -c | --count )          shift
                                threadcount=$4
                                ;;
        -t | --tests)           shift
	                        tests=$4
                                ;;
        -u | --subtest)         shift
	                        subtest=$4
                                ;;
        * )                     usage
                                exit 1
    esac
    shift
done

# subtest only makes sense if caller supplies a test as well
if [ ! -z "${subtest// }" ]; then
    if [ -z "${tests// }" ]; then
	echo "subtest requires that a test be specified as well/n"
	usage
	exit 1;
    else
	# a test must be specified but only one
	if [ ${#tests} -ge 2 ]; then
	    echo "subtest can only be used with a single test/n"
	    usage
	    exit 1;
	fi
    fi
fi

# you could confuse this script if hostfile isn't
# one host per line, maybe that isn't allowed by mpirun, not sure
host_count=$( count_servers $hostfile )

preprocess_tests $tests

# loop through each requested test and run it in a fresh environment
for (( i=0; i<${#tests}; i++ )); do

    # for rebuild, run each sub_test separately
    if [[ ${tests:$i:1} = "r" ]]; then
	# allow for a single subtest to be run or all
	if [ ! -z "${subtest// }" ]; then
	    subtestend=$subtest
	    subteststart=$subtest
	else
	    # if there are new/removed rebuild tests, edit this
	    subtestend=17
	    subteststart=1
	fi
	for (( j=$subteststart;j<=$subtestend;j+=1 )); do
	    start_servers $bindir $host_count $hostfile $outputdir\
                          $threadcount &
	    sleep 5
	    start_test $bindir ${tests:$i:1} $j
	    kill_servers2 $hostfile
	    delete_pools $hostfile
	done
    else
	# not rebuild so don't worry about sub_tests
	start_servers $bindir $host_count $hostfile $outputdir $threadcount &
	sleep 5
	start_test $bindir ${tests:$i:1}
	kill_servers2 $hostfile
	delete_pools $hostfile
    fi
done
