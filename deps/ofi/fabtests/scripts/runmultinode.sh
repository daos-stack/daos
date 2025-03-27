#!/bin/bash

Options=$(getopt --options h:,n:,p:,I:,-x:,z: \
		  		--longoptions hosts:,processes-per-node:,provider:,xfer-method:,iterations:,ci:,cleanup,help \
				-- "$@")

eval set -- "$Options"

hosts=[]
ppn=1
iterations=1
pattern=""
xfer-method="msg"
cleanup=false
help=false
ci=""

while true; do
	case "$1" in
		-h|--hosts)
			IFS=',' read -r -a hosts <<< "$2"; shift 2 ;;
		-n|--processes-per-node) 
			ppn=$2; shift 2 ;;
		-p|--provider)
			provider="$2"; shift 2 ;;
		-I|--iterations)
			iterations=$2; shift 2 ;;
		-z|--pattern)
			pattern="-z $2"; shift 2 ;;
		--cleanup)
			cleanup=true; shift ;;
		-x|--xfer-method)
			xfer-method="$2"; shift 2 ;;
		--ci)
			ci="$2"; shift 2 ;;
		--help) 
			help=true; shift ;;
		--)
			shift; break ;;
	esac
done

if $help ; then
	echo "Run the multinode test suite on the nodes provided for many procceses" 
	echo "multinode tests are run in performance mode"
	echo "Options"
	echo "\t-h,--hosts list of host names to run the tests on"
	echo "\t-n,--processes-per-node number of processes to be run on each node.\
				Total number of fi_mulinode tests run will be n*number of hosts"
	echo "\t-p,--provider libfabric provider to run the multinode tests on"
	echo "\t-C,--cabability multinode cabability to use (rma or default: msg)"
	echo "\t-I,-- iterations number of iterations for the multinode test \
				to run each pattern on"
	echo "\t--cleanup end straggling processes. Does not rerun tests"
	echo "\t--help show this message"
	exit 1
fi
		
num_hosts=${#hosts[@]}
max_ranks=$(($num_hosts*$ppn))
ranks=$max_ranks;
server=${hosts[0]}
start_server=0
output="multinode_server_${num_hosts}_${ppn}.log"
ret=0

if ! $cleanup ; then
	cmd="${ci}fi_multinode -n $ranks -s $server -p '$provider' -x $xfer-method $pattern -I $iterations -T"
	echo $cmd
	for node in "${hosts[@]}"; do
		for i in $(seq 1 $ppn); do
			if [ $start_server -eq 0 ]; then
				echo STARTING SERVER
				if [ "$ci" == "" ]; then
					ssh $node $cmd &> $output &
				else 
					ssh $node $cmd | tee $output &
				fi
				server_pid=$!
				start_server=1
				sleep .5
			else
				echo "starting proc $i/$ppn on $node"
				if [ "$ci" == "" ]; then
					tput cuu1
				fi
				ssh $node $cmd &> /dev/null &
			fi
			sleep .05
		done
	done

	echo "Wait for processes to finish..."
	wait $server_pid
	ret=$?
fi

echo Cleaning up
for node in "${hosts[@]}"; do
	ssh $node "ps -eo comm,pid | grep '^fi_multinode' | awk '{print \$2}' | xargs kill -9" >& /dev/null
done;

if ! $cleanup ; then
	echo "Output: $PWD/$output"
fi

exit $ret 
