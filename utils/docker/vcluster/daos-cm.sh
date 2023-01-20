#!/bin/bash

set -o pipefail

VERSION=0.2
CWD="$(realpath $(dirname $0))"

DAOS_POOL_SIZE=10G

ANSI_COLOR_BLACK=30
ANSI_COLOR_RED=31
ANSI_COLOR_GREEN=32
ANSI_COLOR_YELLOW=33
ANSI_COLOR_BLUE=34
ANSI_COLOR_MAGENTA=35
ANSI_COLOR_CYAN=36
ANSI_COLOR_WHITE=37
ANSI_COLOR_BRIGHT_BLACK=90
ANSI_COLOR_BRIGHT_RED=91
ANSI_COLOR_BRIGHT_GREEN=92
ANSI_COLOR_BRIGHT_YELLOW=93
ANSI_COLOR_BRIGHT_BLUE=94
ANSI_COLOR_BRIGHT_MAGENTA=95
ANSI_COLOR_BRIGHT_CYAN=96
ANSI_COLOR_BRIGHT_WHITE=97

TRACE_LEVEL_QUIET=-1
TRACE_LEVEL_STANDARD=0
TRACE_LEVEL_VERBOSE=1
TRACE_LEVEL_DEBUG=2
TRACE_LEVEL=$TRACE_LEVEL_STANDARD

function debug
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_DEBUG ]]
	then
		echo -e "[\e[${ANSI_COLOR_GREEN}mDEBUG  \e[00m] $@"
	fi
}


function info
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_VERBOSE ]]
	then
		echo -e "[\e[${ANSI_COLOR_CYAN}mINFO   \e[00m] $@"
	fi
}

function warning
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_STANDARD ]]
	then
		echo -e "[\e[${ANSI_COLOR_YELLOW}mWARNING\e[00m] $@" 1>&2
	fi
}

function error
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_STANDARD ]]
	then
		echo -e "[\e[${ANSI_COLOR_BRIGHT_RED}mERROR  \e[00m] $@" 1>&2
	fi
}

function fatal
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_STANDARD ]]
	then
		echo -e "[\e[${ANSI_COLOR_RED}mFATAL  \e[00m] $@" 1>&2
	fi
	exit 1
}

function check_cmds
{
	for cmd in $@
	do
		{ hash $cmd > "/dev/null" 2>&1 ; } || { fatal "$cmd command not installed" ; }
	done
}

check_cmds docker

function usage
{
	cat <<- EOF
		usage: daos-cm.sh [OPTIONS] CMD [ARGS]

		Manage DAOS Virtual Cluster containers

		Options:
		   -z, --pool-size <size>   total size of DAOS pool (default "10G")
		   -h, --help               show this help message and exit
		   -V, --version            show version number
		   -q, --quiet              quiet mode
		   -v, --verbose            verbose mode
		   -D, --debug              debug mode

		Commands:
		   start <ip address>       start DAOS virtual cluster
		   stop                     stop DAOS virtual cluster
		   state                    display the state of the DAOS virtual cluster containers
	EOF
}

function run
{
	if [[ $TRACE_LEVEL -ge $TRACE_LEVEL_STANDARD ]]
	then
		"$@"
	else
		"$@" &> /dev/null
	fi
}

function state
{
	info "State of the DAOS virtual cluster containers"
	if ! run docker compose ps ; then
		fatal "Docker platform not healthy"
	fi
}

function stop
{
	info "Stopping DAOS virtual cluster containers"
	if ! run docker compose down ; then
		fatal "DAOS virtual cluster could not be properly stopped"
	fi
}

function start
{
	DAOS_IFACE_IP="$1"
	DAOS_POOL_SIZE="$2"

	info "Starting DAOS virtual cluster containers"
	if ! run env DAOS_IFACE_IP="$DAOS_IFACE_IP" docker compose up --detach daos_server daos_admin daos_client ; then
		fatal "DAOS virtual cluster containers could no be started"
	fi

	info "Waiting for daos-server services to be started"
	timeout_counter=0
	until [[ $timeout_counter -ge 5 ]] || run docker exec daos-server systemctl --quiet is-active daos_server ; do
		info "daos-server not yet ready: waiting 1s"
		sleep 1
		(( timeout_counter++ ))
	done
	if [[ $timeout_counter -ge 5 ]] ; then
		fatal "DAOS server could not be started"
	fi

	info "Formatting DAOS storage"
	if ! run docker exec daos-admin dmg storage format --host-list=daos-server ; then
		fatal "DAOS storage could not be formatted"
	fi

	info "Checking system"
	if ! run docker exec daos-admin dmg system query --verbose ; then
		fatal "DAOS system not healthy"
	fi

	info "Creating pool tank of $DAOS_POOL_SIZE"
	if ! run docker exec daos-admin dmg pool create --size="$DAOS_POOL_SIZE" tank ; then
		fatal "DAOS pool tank of $DAOS_POOL_SIZE could not be created"
	fi

	info "Checking pool tank"
	if ! run docker exec daos-admin dmg pool query tank ; then
		fatal "DAOS pool tank not healthy"
	fi

	info "Creating POSIX container posix-fs in tank pool"
	if ! run docker exec daos-client daos container create --type=posix --label=posix-fs tank ; then
		fatal "DAOS POSIX container posix-fs could not be created in tank pool"
	fi

	info "Checking container posix-fs"
	if ! run docker exec daos-client daos container query --verbose tank posix-fs ; then
		fatal "DAOS POSIX container posix-fs could not be created in tank pool"
	fi

	info "Creating mount point /mnt/daos-posix-fs in client container"
	if ! run docker exec daos-client mkdir /mnt/daos-posix-fs ; then
		fatal "Mount point /mnt/daos-posix-fs could not be created in daos-client container"
	fi

	info "Mounting DAOS posix-fs container on /mnt/daos-posix-fs"
	if ! run docker exec daos-client dfuse --mountpoint="/mnt/daos-posix-fs" --pool=tank --container=posix-fs ; then
		fatal "DAOS POSIX container posix-fs could not be created in tank pool"
	fi

	info "Checking mount point /mnt/daos-posix-fs"
	if ! run docker exec daos-client /usr/bin/df --human-readable --type=fuse.daos ; then

                fatal "Mount point /mnt/daos-posix-fs not properly mounted"
        fi

	[[ $TRACE_LEVEL -ge $TRACE_LEVEL_STANDARD ]] || return 0

	cat <<- EOF

		================================================================================
		Mount point /mnt/daos-posix-fs is ready on daos-client container.
		fio could be run on DAOS POSIX container with the following command:

		docker exec daos-client /usr/bin/fio --name=random-write --ioengine=pvsync --rw=randwrite --bs=4k --size=128M --nrfiles=4 --directory=/mnt/daos-posix-fs --numjobs=8 --iodepth=16 --runtime=60 --time_based --direct=1 --buffered=0 --randrepeat=0 --norandommap --refill_buffers --group_reporting
		================================================================================
	EOF
}

OPTIONS=$(getopt -o "z:hVvDq" --long "pool-size:,help,version,verbose,debug,quiet" -- "$@")
eval set -- "$OPTIONS"
while true
do
	case "$1" in
		-z|--pool-size) DAOS_POOL_SIZE="$2" ; shift 2 ;;
		-h|--help) usage ; exit 0;;
		-V|--version) echo "daos-cm.sh version=$VERSION" ; exit 0 ;;
		-v|--verbose) TRACE_LEVEL=$TRACE_LEVEL_VERBOSE ; shift 1 ;;
		-D|--debug) TRACE_LEVEL=$TRACE_LEVEL_DEBUG ; set -x ; shift 1 ;;
		-q|--quiet) TRACE_LEVEL=$TRACE_LEVEL_QUIET ; shift 1 ;;
		--) shift ; break ;;
		*) fatal "unrecognized command line option" ;;
	esac
done

[[ $1 ]] || fatal "Command not defined: start, stop or state"
[[ $1 != "start" || $2 ]] || fatal "Start command: missing IP address"
CMD="$1"
DAOS_IFACE_IP="$2"

cd "$CWD"
case "$CMD" in
	start) start "$DAOS_IFACE_IP" "$DAOS_POOL_SIZE" ;;
	stop) stop ;;
	state) state ;;
	*) fatal "Unsupported command $CMD: try with start, stop or state" ;;
esac
