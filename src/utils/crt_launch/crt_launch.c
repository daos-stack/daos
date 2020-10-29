/*
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
 /**
 * MPI-based and cart-based crt_launch application that facilitates launching
 * cart-based clients and servers when no pmix is used.
 *
 * Usage is mpirun -x OFI_INTERFACE=eth0 -H <hosts> crt_launch <app to run>
 *
 * crt_launch will prepare environment for app and exec provided <app to run>
 * The environment consists of envariables:
 *
 * CRT_L_RANK - rank for <app to run> to use. Rank is negotiated across all
 * instances of crt_launch so that each exec-ed app is passed a unique rank.
 *
 * CRT_L_GRP_CFG - Path to group configuration file generated in /tmp/ having
 * form of crt_launch-info-XXXXXX where X's are replaced by random string.
 *
 * OFI_PORT - port to use for
 *
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <cart/api.h>
#include <cart/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <getopt.h>

#include <gurt/common.h>

#define URI_MAX 4096

/*
 * Start port from which crt_launch will hand out ports to launched
 * servers. This start port has to match system-reserved range of ports
 * which can be set via following command as a super-user.
 *
 * Command below will reserve 100 ports from 31415. Those ports will not
 * be handed out to random cart contexts.
 *
 * echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports
 *
 * Alternatively a port selected must be outside of the local port
 * range specified by:
 * /proc/sys/net/ipv4/ip_local_port_range
 */
#define START_PORT 31416

struct host {
	int	my_rank;
	char	self_uri[URI_MAX];
	int	ofi_port;
	int	is_client;
};

static int	my_rank;

struct options_t {
	int	is_client;
	int	show_help;
	char	*app_to_exec;
	int	app_args_indx;
	int	start_port;
};

struct options_t g_opt;

static void
show_usage(const char *msg)
{
	printf("----------------------------------------------\n");
	printf("%s\n", msg);
	printf("Usage: crt_launch [-cph] <-e app_to_exec app_args>\n");
	printf("Options:\n");
	printf("-c	: Indicate app is a client\n");
	printf("-p	: Optional argument to set first port to use\n");
	printf("-h	: Print this help and exit\n");
	printf("----------------------------------------------\n");
}

static int
parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"client",	no_argument,		0, 'c'},
		{"port",	required_argument,	0, 'p'},
		{"help",	no_argument,		0, 'h'},
		{"exec",	required_argument,	0, 'e'},
		{0, 0, 0, 0}
	};

	g_opt.start_port = START_PORT;

	while (1) {
		rc = getopt_long(argc, argv, "e:p:ch", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 'c':
			g_opt.is_client = true;
			break;
		case 'h':
			g_opt.show_help = true;
			break;
		case 'e':
			g_opt.app_to_exec = optarg;
			g_opt.app_args_indx = optind - 1;
			return 0;
		case 'p':
			g_opt.start_port = atoi(optarg);
			break;
		default:
			g_opt.show_help = true;
			return 1;
		}
	}

	return 0;
}

/* Retrieve self uri via CART */
static int
get_self_uri(struct host *h, int rank)
{
	char		*uri;
	crt_context_t	ctx;
	char		*p;
	int		len;
	int		rc;
	char		*str_port = NULL;

	/* Assign ports sequentually to each rank */
	D_ASPRINTF(str_port, "%d", g_opt.start_port + rank);
	if (str_port == NULL)
		return -DER_NOMEM;

	setenv("OFI_PORT", str_port, 1);

	rc = crt_init(0, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = crt_context_create(&ctx);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = crt_self_uri_get(0, &uri);
	if (rc != 0) {
		D_ERROR("crt_self_uri_get() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	len = strlen(uri);

	strncpy(h->self_uri, uri, URI_MAX-1);

	/* Find port number - first from the end number separated by :*/
	/* URIs have a form of: ofi+sockets://10.8.1.55:48259 */
	p = uri+len;
	while (*p != ':') {
		p--;
		if (p == uri)
			break;
	}

	/* Replace : with space */
	*p = ' ';

	p++;
	h->ofi_port = atoi(p);
	D_FREE(uri);

	rc = crt_context_destroy(ctx, 1);
	if (rc != 0) {
		D_ERROR("ctx_context_destroy() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

out:
	D_FREE(str_port);
	return rc;
}

/*
 * Generate group configuration file. Each entry consists of
 * rank<space>uri lines.
 *
 * At the end point CRT_L_GRP_CFG envariable to a generated file
 */
static int
generate_group_file(int world_size, struct host *h)
{
	FILE	*f;
	char	grp_info_template[] = "/tmp/crt_launch-info-XXXXXXX";
	int	tmp_fd;
	int	i;

	tmp_fd = mkstemp(grp_info_template);

	if (tmp_fd == -1) {
		D_ERROR("mkstemp() failed on %s, error: %s\n",
			grp_info_template, strerror(errno));
		return -1;
	}

	f = fdopen(tmp_fd, "w");
	if (f == NULL) {
		printf("fopen failed on %s, error: %s\n",
			grp_info_template, strerror(errno));
		return -1;
	}

	for (i = 0; i < world_size; i++) {
		if (h[i].is_client == false)
			fprintf(f, "%d %s\n", h[i].my_rank, h[i].self_uri);
	}

	fclose(f);
	setenv("CRT_L_GRP_CFG", grp_info_template, true);

	return 0;
}


int main(int argc, char **argv)
{
	int		world_size;
	struct host	*hostbuf = NULL;
	struct host	*recv_buf = NULL;
	int		rc = 0;
	char		str_rank[255];
	char		str_port[255];

	if (argc < 2) {
		show_usage("Insufficient number of arguments");
		return -1;
	}

	rc = parse_args(argc, argv);
	if (rc != 0) {
		show_usage("Failed to parse arguments");
		return -1;
	}

	if (g_opt.show_help) {
		show_usage("Help");
		return -1;
	}

	if (g_opt.app_to_exec == NULL) {
		show_usage("-e option is required\n");
		return -1;
	}

	if (access(g_opt.app_to_exec, F_OK) == -1) {
		fprintf(stderr, "ERROR: Unable to locate '%s'\n",
			g_opt.app_to_exec);
		return -1;
	}

	/*
	 * Using MPI negotiate ranks between each process and retrieve
	 * URI.
	 */
	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	hostbuf = calloc(sizeof(*hostbuf), 1);
	if (!hostbuf) {
		D_ERROR("Failed to allocate hostbuf\n");
		D_GOTO(exit, rc = -1);
	}

	recv_buf = calloc(sizeof(struct host), world_size);
	if (!recv_buf) {
		D_ERROR("Failed to allocate recv_buf\n");
		D_GOTO(exit, rc = -1);
	}

	hostbuf->is_client = g_opt.is_client;
	hostbuf->my_rank = my_rank;
	rc = get_self_uri(hostbuf, my_rank);
	if (rc != 0) {
		D_ERROR("Failed to retrieve self uri\n");
		D_GOTO(exit, rc);
	}

	MPI_Allgather(hostbuf, sizeof(struct host), MPI_CHAR,
		recv_buf, sizeof(struct host), MPI_CHAR,
		   MPI_COMM_WORLD);

	/* Generate group configuration file */
	rc = generate_group_file(world_size, recv_buf);
	if (rc != 0) {
		D_ERROR("generate_group_file() failed\n");
		D_GOTO(exit, rc);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	sprintf(str_rank, "%d", hostbuf->my_rank);
	sprintf(str_port, "%d", hostbuf->ofi_port);
	/* Set CRT_L_RANK and OFI_PORT */
	setenv("CRT_L_RANK", str_rank, true);
	setenv("OFI_PORT", str_port, true);
exit:
	if (hostbuf)
		free(hostbuf);

	if (recv_buf)
		free(recv_buf);

	MPI_Finalize();

	if (rc == 0) {
		/* Exec passed application with rest of arguments */
		execve(g_opt.app_to_exec, &argv[g_opt.app_args_indx], environ);
	}

	return 0;


}
