#!/bin/bash

while getopts "i:t:h:p:m:I:T:H:U:v" opt
do
	case "${opt}" in
	i) iflag=1
		ival="${OPTARG}";;
	t) tflag=1
		tval="${OPTARG}";;
	h) hflag=1
	    hval="${OPTARG}";;
	p) pflag=1
	    pval="${OPTARG}";;
	m) mflag=1
	    mval="${OPTARG}";;
	I) Iflag=1
		ifile="${OPTARG}";;
	T) Tflag=1
		tfile="${OPTARG}";;
	H) Hflag=1
	    hfile="${OPTARG}";;
	U) Uflag=1
	    Uval="${OPTARG}";;
	v) vflag=1
		;;
	*) printf "Usage: %s: [-i ivalue] [-t tvalue] or [-h hvalue] %s: [-p pvalue] ([-I Ivalue] [-T Tvalue] or [-H Hvalue]) [-p pvalue] [-m mvalue] [-U Uvalue ] [-v]  args\n" 
		exit 2;;
	esac
done

if [ ! -z "$vflag" ]; then
	printf 'Option -v %s specified\n'
fi

if [ ! -z "$iflag" ]; then
	if [ ! -z "$vflag" ]; then
		printf 'Option -i %s specified\n' "$ival"
	fi
fi
 
if [ ! -z "$tflag" ]; then
	if [ ! -z "$vflag" ]; then
		printf 'Option -t "%s" specified\n' "$tval"
	fi
fi
 
if [ ! -z "$hflag" ]; then
	if [! -z "$vflag" ]; then
		printf 'Option -h "%s" specified\n' "$hval"
	fi
	ival="$hval"
	tval="$hval" 
fi

if [ ! -z "$pflag" ]; then
	if [ ! -z "$vflag" ]; then
		printf 'Option -p "%s" specified\n' "$pval"
	fi
fi

if [ ! -z "$mflag" ]; then
	if [ ! -z "$vflag" ]; then
		printf 'Option -m "%s" specified\n' "$mval"
	fi
fi

if [ ! -z "$Iflag" ]; then
	if [! -z "$vflag" ]; then
		printf 'Option -I %s specified\n' "$ifile"
	fi
	if [ ! -e "$ifile" ]; then
		printf 'File %s not found\n' "$ifile"
		exit 2
	fi
fi
 
if [ ! -z "$Tflag" ]; then
	if [! -z "$vflag" ]; then
		printf 'Option -T "%s" specified\n' "$tfile"
	fi
	if [ ! -e "$tfile" ]; then
		printf 'File %s not found\n' "$tfile"
		exit 2
	fi
fi
 
if [ ! -z "$Uflag" ]; then
	if [ ! -z "$vflag" ]; then
		printf 'Option -U "%s" specified\n' "$Uval"
	fi
fi

shift "$(($OPTIND -1))"

if [ ! -z "$vflag" ]; then
	printf "Remaining arguments are: %s\n" "$*"
fi

PID=$$
awk -v inits="$ival" -v targs="$tval" -v ifile="$ifile" -v  tfile="$tfile" -v prov="$pval" -v merc="$mflag" -v UCX="$Uflag" -v verbose="$vflag" -v pid="$PID" '
BEGIN {
	no_spec = 0
	if (!length(verbose))
		verbose = 0
	else
		verbose = 1
	printf("pid: %s\n", pid);
	if (verbose) {
		printf("initiators: %s, targets: %s\n", inits, targs)
		printf("ifile: %s, tfile: %s\n", ifile, tfile);
		printf("provider: %s\n", prov)
	}

	if (length(merc) && verbose)
		printf("Running mercury tests\n")
	if (length(UCX) && verbose)	
		printf("Running UCX tests\n")
	if (!length(merc) && !length(UCX)) {
		no_spec = 1
		if (verbose) {
			printf("Running all tests=, no_spec %d\n", no_spec)
		}
	}

	if (length(prov)) {
		if (!check_prov(prov)) {
			printf("Invalid provider: %s\n", prov) 
			exit 3
		}
	} else {
		prov="dc_x"	
	}
	i_idx = 0
	t_idx = 0
	if (length(inits)) {
		i_idx = process_hoststring(inits, init_arr, i_idx)
		if (verbose) {
			for (i in init_arr)
				printf("i_index: %d, value: %s\n", i, init_arr[i]) 
			print "i_idx: " i_idx
		}
	}
	if (length(targs)) {
		t_idx = process_hoststring(targs, targ_arr, t_idx)
		if (verbose) {
			for (i in targ_arr)
				printf("t_index: %d, value: %s\n", i, targ_arr[i]) 
			print "t_idx: " t_idx
		}
	}
	if (length(ifile)) {
		i_idx = process_hostfile(ifile, init_arr, i_idx)
		if (verbose) {
			for (i in init_arr)
				printf("i_index: %d, value: %s\n", i, init_arr[i]) 
			print "i_idx: " i_idx
		}
	}
	if (length(tfile)) {
		if (verbose) {
			t_idx = process_hostfile(tfile, targ_arr, t_idx)
			for (i in targ_arr)
				printf("t_index: %d, value: %s\n", i, targ_arr[i]) 
			print "t_idx: " t_idx
		}
	}
	if (verbose) {
		printf("# of initiators: %d\n", length(init_arr))
		printf("# of targets: %d\n", length(targ_arr))
	}
	if (length(init_arr) && length(targ_arr))
		gen_pairs(init_arr, targ_arr, pair_arr)

	if ( length(UCX) || no_spec)
		process_pairs(pair_arr, "ucx")
	if ( length(merc) || no_spec)
		process_pairs(pair_arr, "merc")
}

function process_pairs(pair_arr, ttype,   hosts,addrs,a_arr,results,i,k) {
	for (i in pair_arr) {
		if (verbose) {
			print pair_arr[i]
		}
		split(pair_arr[i], hosts, ",")
		addrs = getIBaddresses(hosts[2])
		split(addrs, a_arr, ",")
		printf("checking for no_spec: %d\n", no_spec)
		if ( ttype == "ucx")
			run_ucx(hosts, prov, addrs, a_arr, pair_arr[i], results)
		else if (ttype == ""merc)
			run_merc(hosts, prov, "ucx", addrs, a_arr, pair_arr[i], results)

	}
	for (k in results)
		printf("%s: %s\n", k, results[k])
}

function run_merc(hosts, plug, prov, addrs, a_arr, pair, results,    output, j) {
	run_merc_target(hosts[2], plug, prov)
	system("sleep 5")
	run_merc_initiator(hosts[1], hosts[2], a_arr[1], plug,  prov, output)
	if (verbose)
		for (j in output) 
			print output[j]
	results[pair "-put"] = output[length(output)]
	for (j in output)
		delete output[j]

	run_merc_target(hosts[2], prov, "ucp_am_lat")
	system("sleep 5")
	run_merc_initiator(hosts[1], hosts[2], a_arr[1], prov, output, "ucp_am_lat")
	if (verbose)
		for (j in output)
			print output[j]
	results[pair "-get"] = output[length(output)]
	for (j in output)
		delete output[j]
}

function run_merc_target(host, plug, prov,  cmd) {
	printf("running merc %s server on %s\n", op, host)
	cmd = "ssh $LOGNAME@" host " UCX_TLS="prov" UCX_NET_DEVICES=mlx5_0:1 na_perf_server -t "op" -n " ri " -w " wi " -s " size "  >/dev/null 2>&1 &"

	if(verbose)
		print "target cmd: " cmd
	system(cmd);
}

function run_merc_initiator(host, target, taddr, plug, prov, output, op,    wi, ri, size, cmd, i) {
	i = 1
	printf("running merc %s client on %s, to server: %s\n", op, host, target)
	wi= get_merc_iters(op)
	ri = 100 * wi
	size = get_merc_size(op)

	cmd = "ssh $LOGNAME@" host " UCX_TLS="prov" UCX_NET_DEVICES=mlx5_0:1 merc_perftest -t " op " -n " ri " -w " wi " -s " size " " taddr " 2>/dev/null"

	if (verbose)
		print "init cmd: " cmd
	while ((cmd | getline) > 0)
		output[i++] = $0
	close(cmd)
}

function run_ucx(hosts, prov, addrs, a_arr, pair, results,    output, i, j, k) {
	run_ucx_target(hosts[2], prov,  "ucp_put_bw")
	system("sleep 5")
	run_ucx_initiator(hosts[1], hosts[2], a_arr[1], prov, output, "ucp_put_bw")
	if (verbose)
		for (j in output) 
			print output[j]
	results[pair "-put"] = output[length(output)]
	for (j in output)
		delete output[j]

	run_ucx_target(hosts[2], prov, "ucp_am_lat")
	system("sleep 5")
	run_ucx_initiator(hosts[1], hosts[2], a_arr[1], prov, output, "ucp_am_lat")
	if (verbose)
		for (j in output)
			print output[j]
	results[pair "-lat"] = output[length(output)]
	for (j in output)
		delete output[j]

	run_ucx_target(hosts[2], prov, "ucp_get")
	system("sleep 5")
	run_ucx_initiator(hosts[1], hosts[2], a_arr[1], prov, output, "ucp_get")
	if (verbose)
		for (j in output)
			print output[j]
	results[pair "-get"] = output[length(output)]
	for (j in output) 
		delete output[j]
}

function run_ucx_target(host, prov, op,  wi, ri, size, cmd) {
	printf("running ucx %s server on %s\n", op, host)
	wi= get_ucx_iters(op)
	ri = 100 * wi
	size = get_ucx_size(op)
	cmd = "ssh $LOGNAME@" host " UCX_TLS="prov" UCX_NET_DEVICES=mlx5_0:1 ucx_perftest -t "op" -n " ri " -w " wi " -s " size "  >/dev/null 2>&1 &"

	if(verbose)
		print "target cmd: " cmd
	system(cmd);
}

function run_ucx_initiator(host, target, taddr, prov, output, op,    wi, ri, size, cmd, i) {
	i = 1
	printf("running ucx %s client on %s, to server: %s\n", op, host, target)
	wi= get_ucx_iters(op)
	ri = 100 * wi
	size = get_ucx_size(op)

	cmd = "ssh $LOGNAME@" host " UCX_TLS="prov" UCX_NET_DEVICES=mlx5_0:1 ucx_perftest -t " op " -n " ri " -w " wi " -s " size " " taddr " 2>/dev/null"

	if (verbose)
		print "init cmd: " cmd
	while ((cmd | getline) > 0)
		output[i++] = $0
	close(cmd)
}

function get_ucx_iters(op) {
	if (op == "ucp_get" || op == "ucp_put_bw")
		return 1000
	else
		return 10000
}

function get_ucx_size(op) {
	if (op == "ucp_get" || op == "ucp_put_bw") 
		return 1048576
	else
		return 2048 
}

function gen_pairs(ia, ta, pa,  i, j, k) {
	k = 1
	for (i in ia)
		for (j in ta) 
			if (ia[i] != ta[j])
				pa[k++] = ia[i] "," ta[j]
}
			
function process_hoststring(str,out_arr,oarr_idx, split_arr,range_arr,dups,i,j) {
	# oarr_idx = 0
	if (index(str,",")) {
		split(str, split_arr, ",")
		for (i in split_arr)
			if (index(split_arr[i], "[")) {
				cnt = process_range(split_arr[i], range_arr)
				dups = 0
				for (j = 1; j <= cnt; j++)
					dups += add_host(out_arr, range_arr[j], oarr_idx+j)
				oarr_idx += cnt - dups
			} else {
				oarr_idx -= add_host(out_arr, split_arr[i], ++oarr_idx)
			}
	} else if ( index(str, "]")) {
		cnt = process_range(str, range_arr)
        dups = 0
        for (j = 1; j <= cnt; j++)
			dups += add_host(out_arr, range_arr[j], oarr_idx+j)
        oarr_idx += cnt - dups
     } else
		oarr_idx -= add_host(out_arr, str, ++oarr_idx)
	
	return oarr_idx;
}

function process_range(rstr, range_arr,    range,rpre,hname,st,end,i) {
	if (substr(rstr, length(rstr)) != "]" || !index(rstr, "[")) {
		printf("Invalid host range: %s\n",rstr) 
		exit 3
	}
	hname = substr(rstr, 1, index(rstr, "[") - 1)
	if (substr(hname,length(hname)) != "-") {
		printf("Invalid host range: %s\n",rstr) 
		exit 3
	}
	rpre = substr(rstr, index(rstr, "[") + 1)
	range = substr(rpre, 1, length(rpre)-1)
	if ( split(range, ra, "-") != 2) {
		printf("Invalid host range: %s\n",rstr) 
		exit 3
	}
	st = ra[1]+0
	end = ra[2]+0
	if (st == 0 || end == 0 || st > end) {
		printf("Invalid host range: %s\n",rstr) 
		exit 3
	}
	for (i = 1; st <= end; i++) {
		range_arr[i] = hname st++
	#	printf("index: %d, entry: %s\n", i, range_arr[i])
	}
	return i-1
}

function getIBaddresses(host,  ib0, ib1,    cmd, ret) {
	ib0 = 0
	ib1 = 0
	cmd = "ssh $LOGNAME@" host " ifconfig 2>/dev/null" 	
	while ((cmd | getline) > 0)
		if ($1 == "ib0:") {
			ib0 = 1
		} else if (ib0 == 1 && $1 == "inet") {
			ret = $2
			ib0 = 0
		} else if ($1 == "ib1:") {
			ib1 = 1
		} else if (ib1 == 1 && $1 == "inet") {
			ret = ret "," $2
			ib1 = 0
		}
	close(cmd)
	return ret
}

function process_hostfile(fname, out_arr, oarr_idx,   host) {
	while (getline host < fname > 0)
 		oarr_idx -= add_host(out_arr, host, ++oarr_idx)
	return oarr_idx;
}

function add_host(out_arr, host, idx,   i) {
	for (i = 1; i < idx; i++)
		if (out_arr[i] == host)
			return 1
	out_arr[idx] = host
	return 0
}

function check_prov(p) {
	if (p == "dc_x" || p == "ud_x" || p == "ud_x" || p == "rc_x" || p == "dc_mlx5")
		return 1
	else
		return 0
}'
