/*
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * \file
 *
 * System functionalities
 */

#define D_LOGFAC DD_FAC(server)

#include "srv_internal.h"

#define GROUP_ID_MAX_LEN 64
#define ADDR_STR_MAX_LEN 128

#define _S(m) #m
#define S(m) _S(m)

/*
 * Return a URI string, like "ofi+sockets://192.168.1.70:44821", that callers
 * are responsible for freeing with D_FREE.
 */
static char *
create_tag_uri(const char *base_uri, int i)
{
	char   *uri;
	char   *p;
	int	port;

	/* Locate the ":" between the host and the port. */
	p = strrchr(base_uri, ':');
	if (p == NULL)
		return NULL;

	/*
	 * Allocate the tag URI buffer and copy base_uri in except for the
	 * port.
	 */
	D_ALLOC(uri, ADDR_STR_MAX_LEN + 1);
	if (uri == NULL)
		return NULL;
	strncpy(uri, base_uri, p + 1 - base_uri);

	/* Print the port into the tag URI buffer. */
	port = atoi(p + 1) + i;
	D_ASSERT(port <= 65535 /* maximal port number */);
	sprintf(uri + strlen(uri), "%d", port);

	return uri;
}

static int
add_server(crt_group_t *group, d_rank_t rank, char *uri, int ntags)
{
	crt_node_info_t	info;
	int		i;
	int		rc;

	for (i = 0; i < ntags; i++) {
		info.uri = create_tag_uri(uri, i);
		rc = crt_group_node_add(group, rank, i, info);
		D_FREE(info.uri);
		if (rc != 0) {
			D_ERROR("failed to add node: rank=%u tag=%d uri=%s\n",
				rank, i, info.uri);
			return rc;
		}
	}

	return 0;
}

/* This function doesn't try to restore the primary group state upon errors. */
int
dss_sys_map_load(const char *path, crt_group_id_t grpid, d_rank_t self,
		 int ntags)
{
	crt_group_t    *group;
	FILE	       *file;
	char		name[GROUP_ID_MAX_LEN + 1];
	int		size;
	char		all_or_self[5];
	int		i;
	int		rc;

	group = crt_group_lookup(NULL /* primary group */);
	D_ASSERT(group != NULL);

	file = fopen(path, "r");
	if (file == NULL) {
		rc = daos_errno2der(errno);
		D_ERROR("%s: %s\n", path, strerror(errno));
		goto out;
	}

	/* "name daos_server" */
	rc = fscanf(file, "%*s %"S(GROUP_ID_MAX_LEN)"s", name);
	if (rc == EOF) {
		rc = -DER_INVAL;
		goto out_file;
	}
	if (strcmp(name, grpid) != 0) {
		D_ERROR("invalid group name: %s != %s\n", name, grpid);
		rc = -DER_INVAL;
	}

	/* "size 2" */
	rc = fscanf(file, "%*s %d", &size);
	if (rc == EOF) {
		rc = -DER_INVAL;
		goto out_file;
	}

	/* "all" */
	rc = fscanf(file, "%4s", all_or_self);
	if (rc == EOF) {
		rc = -DER_INVAL;
		goto out_file;
	}

	/*
	 * "0 ofi+sockets://10.7.1.70:31416"
	 * "1 ofi+sockets://10.7.1.71:31416"
	 * ...
	 */
	for (i = 0; i < size; i++) {
		d_rank_t	rank;
		char		uri[ADDR_STR_MAX_LEN + 1];

		rc = fscanf(file, "%d %"S(ADDR_STR_MAX_LEN)"s", &rank, uri);
		if (rc == EOF) {
			rc = -DER_INVAL;
			goto out_file;
		}

		if (rank == self) {
			rc = 0;
			continue;
		}

		rc = add_server(group, rank, uri, ntags);
		if (rc != 0) {
			D_ERROR("failed to add server %u %s: %d\n", rank, uri,
				rc);
			goto out_file;
		}
	}

out_file:
	fclose(file);
out:
	return rc;
}
