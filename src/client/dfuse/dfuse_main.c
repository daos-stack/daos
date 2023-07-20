/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <errno.h>
#include <getopt.h>
#include <dlfcn.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <sys/types.h>
#include <hwloc.h>

#define D_LOGFAC DD_FAC(dfuse)

#include "dfuse.h"

#include "daos_fs.h"
#include "daos_api.h"
#include "daos_uns.h"

#include <gurt/common.h>
/* Signal handler for SIGCHLD, it doesn't need to do anything, but it's
 * presence makes pselect() return EINTR in the dfuse_bg() function which
 * is used to detect abnormal exit.
 */
static void
noop_handler(int arg) {
}

static int bg_fd;
static struct d_fault_attr_t *start_fault_attr;

/* Send a message to the foreground thread */
static int
dfuse_send_to_fg(int rc)
{
	int nfd;
	int ret;

	if (bg_fd == 0)
		return -DER_SUCCESS;

	DFUSE_LOG_INFO("Sending %d to fg", rc);

	ret = write(bg_fd, &rc, sizeof(rc));

	close(bg_fd);
	bg_fd = 0;

	if (ret != sizeof(rc))
		return -DER_MISC;

	/* If the return code is non-zero then that means there's an issue so
	 * do not perform the rest of the operations in this function.
	 */
	if (rc != 0)
		return -DER_SUCCESS;

	ret = chdir("/");

	nfd = open("/dev/null", O_RDWR);
	if (nfd == -1)
		return -DER_MISC;

	dup2(nfd, STDIN_FILENO);
	dup2(nfd, STDOUT_FILENO);
	dup2(nfd, STDERR_FILENO);
	close(nfd);

	if (ret != 0)
		return -DER_MISC;

	DFUSE_LOG_INFO("Success");

	return -DER_SUCCESS;
}

/* Optionally go into the background
 *
 * It's not possible to simply call daemon() here as if we do that after
 * daos_init() then libfabric doesn't like it, and if we do it before
 * then there are no reporting of errors.  Instead, roll our own where
 * we create a socket pair, call fork(), and then communicate on the
 * socket pair to allow the foreground process to stay around until
 * the background process has completed.  Add in a check for SIGCHLD
 * from the background in case of abnormal exit to avoid deadlocking
 * the parent in this case.
 */
static int
dfuse_bg(struct dfuse_info *dfuse_info)
{
	sigset_t pset;
	fd_set read_set = {};
	int err;
	struct sigaction sa = {};
	pid_t child_pid;
	sigset_t sset;
	int rc;
	int di_spipe[2];

	rc = pipe(&di_spipe[0]);
	if (rc)
		return 1;

	sigemptyset(&sset);
	sigaddset(&sset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sset, NULL);

	child_pid = fork();
	if (child_pid == -1)
		return 1;

	if (child_pid == 0) {
		bg_fd = di_spipe[1];
		return 0;
	}

	sa.sa_handler = noop_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	sigemptyset(&pset);

	FD_ZERO(&read_set);
	FD_SET(di_spipe[0], &read_set);

	errno = 0;
	rc = pselect(di_spipe[0] + 1, &read_set, NULL, NULL, NULL, &pset);
	err = errno;

	if (err == EINTR) {
		printf("Child process died without reporting failure\n");
		exit(2);
	}

	if (FD_ISSET(di_spipe[0], &read_set)) {
		ssize_t b;
		int child_ret;

		b = read(di_spipe[0], &child_ret, sizeof(child_ret));
		if (b != sizeof(child_ret)) {
			printf("Read incorrect data %zd\n", b);
			exit(2);
		}
		if (child_ret) {
			printf("Exiting %d %s\n", child_ret,
			       d_errstr(child_ret));
			exit(-(child_ret + DER_ERR_GURT_BASE));
		} else {
			exit(0);
		}
	}

	printf("Socket is not set\n");
	exit(2);
}

/*
 * Creates a fuse filesystem for any plugin that needs one.
 *
 * Should be called from the post_start plugin callback and creates
 * a filesystem.
 * Returns true on success, false on failure.
 */
int
dfuse_launch_fuse(struct dfuse_info *dfuse_info, struct fuse_args *args)
{
	int rc;

	dfuse_info->di_session = fuse_session_new(args, &dfuse_ops, sizeof(dfuse_ops), dfuse_info);
	if (dfuse_info->di_session == NULL) {
		DFUSE_TRA_ERROR(dfuse_info, "Could not create fuse session");
		return -DER_INVAL;
	}

	/* This is used by the fault injection testing to simulate starting dfuse and test the
	 * error paths.  This testing is harder if we allow the mount to happen, so for the
	 * purposes of the test allow dfuse to cleanly return and exit at this point.  An
	 * added advantage here is that it allows us to run dfuse startup tests in docker
	 * containers, without the fuse devices present
	 */
	if (D_SHOULD_FAIL(start_fault_attr))
		return -DER_SUCCESS;

	rc = fuse_session_mount(dfuse_info->di_session,	dfuse_info->di_mountpoint);
	if (rc != 0) {
		DFUSE_TRA_ERROR(dfuse_info, "Could not mount fuse");
		return -DER_INVAL;
	}

	rc = dfuse_send_to_fg(0);
	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(dfuse_info, "Error sending signal to fg: "DF_RC, DP_RC(rc));

	/* Blocking */
	if (dfuse_info->di_threaded)
		rc = dfuse_loop(dfuse_info);
	else
		rc = fuse_session_loop(dfuse_info->di_session);
	if (rc != 0)
		DFUSE_TRA_ERROR(dfuse_info,
				"Fuse loop exited with return code: %d (%s)", rc, strerror(rc));

	fuse_session_unmount(dfuse_info->di_session);

	return daos_errno2der(rc);
}

#define DF_POOL_PREFIX "pool="
#define DF_CONT_PREFIX "container="

/* Extract options for pool and container from fstab style mount options. */
static void
parse_mount_option(char *mnt_string, char *pool_name, char *cont_name)
{
	char *tok;
	char *token;

	while ((token = strtok_r(mnt_string, ",", &tok))) {
		mnt_string = NULL;

		if (strncmp(token, DF_POOL_PREFIX, sizeof(DF_POOL_PREFIX) - 1) == 0) {
			strncpy(pool_name, &token[sizeof(DF_POOL_PREFIX) - 1],
				DAOS_PROP_LABEL_MAX_LEN);
		} else if (strncmp(token, DF_CONT_PREFIX, sizeof(DF_CONT_PREFIX) - 1) == 0) {
			strncpy(cont_name, &token[sizeof(DF_CONT_PREFIX) - 1],
				DAOS_PROP_LABEL_MAX_LEN);
		}
	}
}

static void
show_version(char *name)
{
	fprintf(stdout, "%s version %s, libdaos %d.%d.%d\n", name, DAOS_VERSION,
		DAOS_API_VERSION_MAJOR, DAOS_API_VERSION_MINOR, DAOS_API_VERSION_FIX);
	fprintf(stdout, "Using fuse %s\n", fuse_pkgversion());
#if HAVE_CACHE_READDIR
	fprintf(stdout, "Kernel readdir support enabled\n");
#endif
};

static void
show_help(char *name)
{
	printf(
	    "usage: %s [OPTIONS] [mountpoint [pool container]]\n"
	    "\n"
	    "Options:\n"
	    "\n"
	    "	-m --mountpoint=<path>	Mount point to use (deprecated, use positional argument)\n"
	    "\n"
	    "	   --pool=name		pool UUID/label\n"
	    "	   --container=name	container UUID/label\n"
	    "	   --path=<path>	Path to load UNS pool/container data\n"
	    "	   --sys-name=STR	DAOS system name context for servers\n"
	    "\n"
	    "	-S --singlethread	Single threaded\n"
	    "	-t --thread-count=count	Total number of threads to use\n"
	    "	-e --eq-count=count	Number of event queues to use\n"
	    "	-f --foreground		Run in foreground\n"
	    "	   --enable-caching	Enable all caching (default)\n"
	    "	   --enable-wb-cache	Use write-back cache rather than write-through (default)\n"
	    "	   --disable-caching	Disable all caching\n"
	    "	   --disable-wb-cache	Use write-through rather than write-back cache\n"
	    "	-o options		mount style options string\n"
	    "\n"
	    "	   --multi-user		Run dfuse in multi user mode\n"
	    "\n"
	    "	-h --help		Show this help\n"
	    "	-v --version		Show version\n"
	    "\n"
	    "dfuse performs a user space mount of a DAOS POSIX container at the mountpoint\n"
	    "directory that is specified as the first positional argument. This directory\n"
	    "has to exist and has to be accessible to the user, or the mount will fail.\n"
	    "Alternatively, the mountpoint directory can also be specified with the -m or\n"
	    "--mountpoint= option but this usage is deprecated.\n"
	    "\n"
	    "The DAOS pool and container can be specified in several different ways"
	    "(only one way of specifying the pool and container should be used):\n"
	    "* The DAOS pool and container can be explicitly specified on the command line\n"
	    "  as positional arguments, using either UUIDs or labels. This is the most\n"
	    "  common way to use dfuse to mount a POSIX container.\n"
	    "* The DAOS pool and container can be explicitly specified on the command line\n"
	    "  using the --pool and --container options, with either UUIDs or labels.\n"
	    "  This usage is deprecated in favor of using positional arguments.\n"
	    "* When the --path option is used, DAOS namespace attributes are loaded from\n"
	    "  that filesystem path, including the DAOS pool and container information.\n"
	    "* When the --path option is not used, then the mountpoint directory will also\n"
	    "  be checked and DAOS namespace attributes will be loaded from there if present.\n"
	    "* When using the -o mount option string, pool= and container= keys in the mount\n"
	    "  option string identify the DAOS pool and container.\n"
	    "* When the pool and container are not specified through any of these methods,\n"
	    "  dfuse will construct filesystem pathnames under the mountpoint by using the\n"
	    "  pool and container UUIDs (not labels) of *all* pools and POSIX containers to\n"
	    "  which the user running dfuse has access as pathname components.\n"
	    "  - A path to a POSIX container that is mounted this way can be traversed to\n"
	    "    access the root of that container, for example by changing directory to\n"
	    "    /mountpoint/pool_uuid/cont_uuid/.\n"
	    "  - However, listing the /mountpoint/ directory is not supported and will not\n"
	    "    show the pool UUIDs that are mounted there.\n"
	    "  - Similarly, while the user can change directory into a /mountpoint/pool_uuid/\n"
	    "    directory, listing that directory is not supported and will not show the\n"
	    "    container UUIDs that are mounted there.\n"
	    "  - Running 'fusermount3 -u /mountpoint' will unmount *all* POSIX containers that\n"
	    "    have been mounted this way, as well as the /mountpoint/pool_uuid/ directories.\n"
	    "\n"
	    "Threading and resource usage:\n"
	    "dfuse has two types of threads: fuse threads which accept and process requests from\n"
	    "the kernel, and progress threads which complete asynchronous read/write operations.\n"
	    "Each asynchronous progress thread uses one DAOS event queue to consume additional\n"
	    "network resources. As all metadata operations are blocking, the level of concurrency\n"
	    "in dfuse is limited by the number of fuse threads.\n"
	    "By default, the total thread count is one per available core to allow maximum\n"
	    "throughput. If hyperthreading is enabled, then one thread per hyperthread core\n"
	    "is used. This can be modified in two ways: Reducing the number of available\n"
	    "cores by running dfuse in a cpuset via numactl or similar tools,\n"
	    "or by using the --thread-count, --eq-count or --singlethread options:\n"
	    "* The --thread-count option controls the total number of threads.\n"
	    "* Increasing the --eq-count option at a fixed --thread-count will reduce the number\n"
	    "  of fuse threads accordingly. The default value for --eq-count is 1.\n"
	    "* The --singlethread mode will use one thread for handling fuse requests and a\n"
	    "  second thread for a single event queue, for a total of two threads.\n"
	    "\n"
	    "If dfuse is running in background mode (the default unless launched via mpirun)\n"
	    "then it will stay in the foreground until the mount is registered with the\n"
	    "kernel to allow appropriate error reporting.\n"
	    "\n"
	    "The -o option can be used to run dfuse via fstab or similar and accepts standard\n"
	    "mount options.  This will be treated as a comma separated list of key=value pairs,\n"
	    "and dfuse will use pool= and container= keys from this string.\n"
	    "\n"
	    "Caching is on by default. The caching behavior for a dfuse mount can be controlled\n"
	    "by command line options. Further caching controls can be set on a per-container\n"
	    "basis through container attributes.\n"
	    "* If the --disable-caching option is used then no caching will be performed, and the\n"
	    "  container attributes are not used. The default is --enable-caching.\n"
	    "* If --disable-wb-cache is used then the write operations for the whole mount are\n"
	    "  performed in write-through mode, and the container attributes are still used.\n"
	    "  The default is --enable-wb-cache.\n"
	    "* If --disable-caching and --enable-wb-cache are both specified,\n"
	    "  the --enable-wb-cache option is ignored and no caching is performed.\n"
	    "\n"
	    "Version: %s\n",
	    name, DAOS_VERSION);
}

int
main(int argc, char **argv)
{
	struct dfuse_info *dfuse_info                             = NULL;
	struct dfuse_pool *dfp                                    = NULL;
	struct dfuse_cont *dfs                                    = NULL;
	struct duns_attr_t duns_attr                              = {};
	uuid_t             cont_uuid                              = {};
	char               pool_name[DAOS_PROP_LABEL_MAX_LEN + 1] = {};
	char               cont_name[DAOS_PROP_LABEL_MAX_LEN + 1] = {};
	int                c;
	int                rc;
	int                rc2;
	char              *path              = NULL;
	bool               have_thread_count = false;
	int                pos_index         = 0;

	struct option      long_options[] = {{"mountpoint", required_argument, 0, 'm'},
					     {"multi-user", no_argument, 0, 'M'},
					     {"path", required_argument, 0, 'P'},
					     {"pool", required_argument, 0, 'p'},
					     {"container", required_argument, 0, 'c'},
					     {"sys-name", required_argument, 0, 'G'},
					     {"singlethread", no_argument, 0, 'S'},
					     {"thread-count", required_argument, 0, 't'},
					     {"eq-count", required_argument, 0, 'e'},
					     {"foreground", no_argument, 0, 'f'},
					     {"enable-caching", no_argument, 0, 'E'},
					     {"enable-wb-cache", no_argument, 0, 'F'},
					     {"disable-caching", no_argument, 0, 'A'},
					     {"disable-wb-cache", no_argument, 0, 'B'},
					     {"options", required_argument, 0, 'o'},
					     {"version", no_argument, 0, 'v'},
					     {"help", no_argument, 0, 'h'},
					     {0, 0, 0, 0}};

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ALLOC_PTR(dfuse_info);
	if (dfuse_info == NULL)
		D_GOTO(out_debug, rc = -DER_NOMEM);

	dfuse_info->di_threaded = true;
	dfuse_info->di_caching  = true;
	dfuse_info->di_wb_cache = true;
	dfuse_info->di_eq_count = 1;

	while (1) {
		c = getopt_long(argc, argv, "Mm:St:o:fhv", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			strncpy(pool_name, optarg, DAOS_PROP_LABEL_MAX_LEN);
			break;
		case 'c':
			strncpy(cont_name, optarg, DAOS_PROP_LABEL_MAX_LEN);
			break;
		case 'G':
			dfuse_info->di_group = optarg;
			break;
		case 'E':
			dfuse_info->di_caching  = true;
			dfuse_info->di_wb_cache = true;
			break;
		case 'F':
			dfuse_info->di_wb_cache = true;
			break;
		case 'A':
			dfuse_info->di_caching  = false;
			dfuse_info->di_wb_cache = false;
			break;
		case 'B':
			dfuse_info->di_wb_cache = false;
			break;
		case 'm':
			dfuse_info->di_mountpoint = optarg;
			break;
		case 'M':
			dfuse_info->di_multi_user = true;
			break;
		case 'P':
			path = optarg;
			break;
		case 'S':
			/* Set it to be single threaded, but allow an extra one
			 * for the event queue processing
			 */
			dfuse_info->di_threaded     = false;
			dfuse_info->di_thread_count = 2;
			break;
		case 'e':
			dfuse_info->di_eq_count = atoi(optarg);
			break;
		case 't':
			dfuse_info->di_thread_count = atoi(optarg);
			have_thread_count           = true;
			break;
		case 'f':
			dfuse_info->di_foreground = true;
			break;
		case 'o':
			parse_mount_option(optarg, pool_name, cont_name);
			break;
		case 'h':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case 'v':
			show_version(argv[0]);

			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case '?':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_INVAL);
			break;
		}
	}

	for (pos_index = optind; optind < argc; optind++) {
		switch (optind - pos_index) {
		case 0:
			dfuse_info->di_mountpoint = argv[optind];
			break;
		case 1:
			strncpy(pool_name, argv[optind], DAOS_PROP_LABEL_MAX_LEN);
			break;
		case 2:
			strncpy(cont_name, argv[optind], DAOS_PROP_LABEL_MAX_LEN);
			break;
		default:
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_INVAL);
			break;
		}
	}

	if (!dfuse_info->di_foreground && getenv("PMIX_RANK")) {
		DFUSE_TRA_WARNING(dfuse_info,
				  "Not running in background under orterun");
		dfuse_info->di_foreground = true;
	}

	if (!dfuse_info->di_mountpoint) {
		printf("Mountpoint is required\n");
		show_help(argv[0]);
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	/* If the number of threads has been specified on the command line then use that, otherwise
	 * check CPU binding.  If bound to a number of cores then launch that number of threads,
	 * if not bound them limit to 16.
	 */
	if (dfuse_info->di_threaded && !have_thread_count) {
		struct hwloc_topology *hwt;
		hwloc_const_cpuset_t   hw;
		int                    total;
		int                    allowed;

		rc = hwloc_topology_init(&hwt);
		if (rc != 0)
			D_GOTO(out_debug, rc = daos_errno2der(errno));

		rc = hwloc_topology_load(hwt);
		if (rc != 0)
			D_GOTO(out_debug, rc = daos_errno2der(errno));

		hw = hwloc_topology_get_complete_cpuset(hwt);

		total = hwloc_bitmap_weight(hw);

		rc = hwloc_get_cpubind(hwt, (struct hwloc_bitmap_s *)hw, HWLOC_CPUBIND_PROCESS);
		if (rc != 0)
			D_GOTO(out_debug, rc = daos_errno2der(errno));

		allowed = hwloc_bitmap_weight(hw);

		hwloc_topology_destroy(hwt);

		if (total == allowed)
			dfuse_info->di_thread_count = min(allowed, 16);
		else
			dfuse_info->di_thread_count = allowed;
	}

	/* Reserve one thread for each daos event queue */
	dfuse_info->di_thread_count -= dfuse_info->di_eq_count;

	if (dfuse_info->di_thread_count < 1) {
		printf("Dfuse needs at least one fuse thread.\n");
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	if (!dfuse_info->di_foreground) {
		rc = dfuse_bg(dfuse_info);
		if (rc != 0) {
			printf("Failed to background\n");
			exit(2);
		}
	}

	if (cont_name[0] && !pool_name[0]) {
		printf("Container name specified without pool\n");
		D_GOTO(out_debug, rc = -DER_INVAL);
	}

	rc = daos_init();
	if (rc != -DER_SUCCESS)
		D_GOTO(out_debug, rc);

	start_fault_attr = d_fault_attr_lookup(100);

	DFUSE_TRA_ROOT(dfuse_info, "dfuse_info");

	rc = dfuse_fs_init(dfuse_info);
	if (rc != 0)
		D_GOTO(out_fini, rc);

	/* Firsly check for attributes on the path.  If this option is set then
	 * it is expected to work.
	 */
	if (path) {
		struct duns_attr_t path_attr = {.da_flags = DUNS_NO_REVERSE_LOOKUP};

		if (pool_name[0]) {
			printf("Pool specified multiple ways\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}

		rc = duns_resolve_path(path, &path_attr);
		DFUSE_TRA_INFO(dfuse_info, "duns_resolve_path() on path: %d (%s)", rc,
			       strerror(rc));
		if (rc == ENOENT) {
			printf("Attr path does not exist\n");
			D_GOTO(out_daos, rc = daos_errno2der(rc));
		} else if (rc != 0) {
			/* Abort on all errors here, even ENODATA or ENOTSUP
			 * because the path is supposed to provide
			 * pool/container details and it's an error if it can't.
			 */
			printf("Error reading attr from path: %d (%s)\n", rc, strerror(rc));
			D_GOTO(out_daos, rc = daos_errno2der(rc));
		}

		strncpy(pool_name, path_attr.da_pool, DAOS_PROP_LABEL_MAX_LEN + 1);
		strncpy(cont_name, path_attr.da_cont, DAOS_PROP_LABEL_MAX_LEN + 1);
		duns_destroy_attr(&path_attr);
	}

	/* Check for attributes on the mount point itself to use.
	 * Abort if path exists and mountpoint has attrs as both should not be
	 * set, but if nothing exists on the mountpoint then this is not an
	 * error so keep going.
	 */
	duns_attr.da_flags = DUNS_NO_REVERSE_LOOKUP;
	rc = duns_resolve_path(dfuse_info->di_mountpoint, &duns_attr);
	DFUSE_TRA_INFO(dfuse_info, "duns_resolve_path() on mountpoint returned: %d (%s)", rc,
		       strerror(rc));
	if (rc == 0) {
		if (pool_name[0]) {
			printf("Pool specified multiple ways\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}
		/* If path was set, and is different to mountpoint then abort.
		 */
		if (path && (strcmp(path, dfuse_info->di_mountpoint) == 0)) {
			printf("Attributes set on both path and mountpoint\n");
			D_GOTO(out_daos, rc = -DER_INVAL);
		}

		strncpy(pool_name, duns_attr.da_pool, DAOS_PROP_LABEL_MAX_LEN + 1);
		strncpy(cont_name, duns_attr.da_cont, DAOS_PROP_LABEL_MAX_LEN + 1);
		duns_destroy_attr(&duns_attr);

	} else if (rc == ENOENT) {
		printf("Mount point does not exist\n");
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	} else if (rc == ENOTCONN) {
		printf("Stale mount point, run fusermount3 and retry\n");
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	} else if (rc != ENODATA && rc != ENOTSUP) {
		/* DUNS may have logged this already but won't have printed anything */
		printf("Error resolving mount point: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	/* Connect to a pool. */
	rc = dfuse_pool_connect(dfuse_info, pool_name, &dfp);
	if (rc != 0) {
		printf("Failed to connect to pool: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	if (cont_name[0] && uuid_parse(cont_name, cont_uuid) < 0)
		rc = dfuse_cont_open_by_label(dfuse_info, dfp, cont_name, &dfs);
	else
		rc = dfuse_cont_open(dfuse_info, dfp, &cont_uuid, &dfs);
	if (rc != 0) {
		printf("Failed to connect to container: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_pool, rc = daos_errno2der(rc));
	}

	rc = dfuse_fs_start(dfuse_info, dfs);
	if (rc != -DER_SUCCESS)
		D_GOTO(out_cont, rc);

	/* The container created by dfuse_cont_open() will have taken a ref on the pool, so drop the
	 * initial one.
	 */
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfp->dfp_entry);

	rc = dfuse_fs_stop(dfuse_info);

	/* Remove all inodes from the hash tables */
	rc2 = dfuse_fs_fini(dfuse_info);
	if (rc == -DER_SUCCESS)
		rc = rc2;
	fuse_session_destroy(dfuse_info->di_session);
	goto out_fini;
out_cont:
	d_hash_rec_decref(&dfp->dfp_cont_table, &dfs->dfs_entry);
out_pool:
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfp->dfp_entry);
out_daos:
	rc2 = dfuse_fs_fini(dfuse_info);
	if (rc == -DER_SUCCESS)
		rc = rc2;
out_fini:
	if (dfuse_info) {
		D_ASSERT(atomic_load_relaxed(&dfuse_info->di_inode_count) == 0);
		D_ASSERT(atomic_load_relaxed(&dfuse_info->di_fh_count) == 0);
		D_ASSERT(atomic_load_relaxed(&dfuse_info->di_pool_count) == 0);
		D_ASSERT(atomic_load_relaxed(&dfuse_info->di_container_count) == 0);
	}

	DFUSE_TRA_DOWN(dfuse_info);
	daos_fini();
out_debug:
	D_FREE(dfuse_info);
	DFUSE_LOG_INFO("Exiting with status %d", rc);
	daos_debug_fini();
out:
	dfuse_send_to_fg(rc);
	/* Convert CaRT error numbers to something that can be returned to the
	 * user.  This needs to be less than 256 so only works for CaRT, not
	 * DAOS error numbers.
	 */

	if (rc)
		return -(rc + DER_ERR_GURT_BASE);
	else
		return 0;
}
