#!/bin/sh
# This script acts as a wrapper around daos_test.  It starts and stops
# daos servers and cleans up any pools left by the tests.  This script
# presumes the directory layout you'd find after a default build of daos.

function usage {
    echo "launcher.sh serverhostfile outputdir"\
    " [-t tests-to-run] [-b bindir] [-s setup-script] [-c threadcount]"\
    " [-u subtests-to-run] [-d client-hostfile] [-e env-file] [-h]"
}

function count_servers {
    cat $1 | wc -l
}

# start daos servers, one for each host in hostfile
function start_servers {
    echo "starting servers"
    orterun --np $1 --hostfile $2 --enable-recovery \
	--report-uri $5 -x D_LOG_MASK=INFO -x PATH -x CRT_PHY_ADDR_STR \
	-x OFI_INTERFACE -x OFI_PORT -x D_LOG_FILE=$3/daos.log \
	-x FI_PSM2_NAME_SERVER -x FI_SOCKETS_MAX_CONN_RETRY -x PSM2_MULTI_EP \
	-x CRT_CREDIT_EP_CTX -x FI_LOG_LEVEL -x LD_LIBRARY_PATH \
	-x DD_SUBSYS -x DD_MASK -x CCI_CONFIG \
	daos_server -d /tmp/.daos -g daos_server -c $4
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
	ssh -n "${tokens[0]}" "pkill daos_engine && pkill daos_server"
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
    if [[ $1 =~ "none" ]]; then
	hostfileopt="--np 1 "
    else
	client_count=$( count_servers $1)
	if (( client_count > 0 )); then
	    hostfileopt="--np "$client_count" --hostfile "$1
	fi
    fi

    # for some reason I had to assign this to a variable
    # if I included directly below it fails
    uriopt="--ompi-server file:"$2
    if [[ $4 ]]
    then
	# sub_test provided
	orterun $hostfileopt $uriopt daos_test -$3 -u $4
    else
	orterun $hostfileopt $uriopt daos_test -$3
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

# uncomment this if you want to debug the script or just see
# everything as it happens
#set -x

# start main script that coordinates all the action
if [ "$#" -lt 3 ]; then
    usage
    exit 1;
fi

# these are required positional parameters
hostfile=$1
outputdir=$2

if [ ! -d  $outputdir ]; then
    echo "invalid output directory parameter"
    usage
    exit 1;
fi
if [ ! -f  $hostfile ]; then
    echo "hostfile not found"
    usage
    exit 1;
fi

# there are a few optional parameters too
while [ "$3" != "" ]; do
    case $3 in
        -b | --bindir )         shift
                                bindir=$3
                                ;;
        -c | --count )          shift
                                threadcount=$3
                                ;;
        -d | --hostfile )       shift
                                clienthostfile=$3
                                ;;
        -e | --env )            shift
                                envfile=$3
                                ;;
        -s | --setup )          shift
                                setupfile=$3
                                ;;
        -t | --tests)           shift
	                        tests=$3
                                ;;
        -u | --subtest)         shift
	                        subtest=$3
                                ;;
        * )                     usage
                                exit 1
    esac
    shift
done

# bindir and setup path are both optional but you must
# supply one or the other so executables can be found
pathset=0
if [[ ! -z "$bindir" ]]; then
    export PATH=$bindir:$PATH
    pathset=1
fi
if [[ ! -z "$setupfile" ]]; then
    source $setupfile
    pathset=1
fi
if (( pathset == 0  )); then
    echo "you must supply either a setup file i.e. -s"
    echo "or a bindir i.e. -b"
    usage
    exit 1;
fi

# if caller supplied an env script, run it
if [[ ! -z "$envfile" ]]; then
    source $envfile
fi

# make sure some critical environment variables are
# set or nothing will work
if [ -z "$CRT_PHY_ADDR_STR" ]; then
    echo "Network setup is suspect"
    usage
    exit 1
fi
if [ -z "$OFI_INTERFACE" ]; then
    echo "Network setup is suspect"
    usage
    exit 1
fi

# default thread count if not set
if [[ -z "$threadcount" ]]; then
    threadcount=1
fi

# set client hostfile
if [[ -z "$clienthostfile" ]]; then
    clienthostfile="none"
fi

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

# clients and servers find each other with uri in a file,
# establish the name for this file
urifile=$outputdir"/launcher_urifile"$((1 + RANDOM % 1000))

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
	    start_servers $host_count $hostfile $outputdir\
                          $threadcount $urifile &
	    sleep 5
	    start_test $clienthostfile $urifile ${tests:$i:1} $j
	    kill_servers2 $hostfile
	    delete_pools $hostfile
	    rm $urifile
	done
    else
	# not rebuild so don't worry about sub_tests
	start_servers $host_count $hostfile $outputdir $threadcount\
                      $urifile &
	sleep 5
	start_test $clienthostfile $urifile ${tests:$i:1}
	kill_servers2 $hostfile
	delete_pools $hostfile
	rm $urifile
    fi
done
