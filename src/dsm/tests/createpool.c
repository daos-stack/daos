/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * Pool Creation Test
 *
 * This hacky program exercises the pool creation methods in the DSM server
 * API.
 */

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <daos_srv/daos_m_srv.h>

/* Super hacky. This is actually an internal function of libdaos_m_srv. */
int dsms_storage_init(void);

int
main(int argc, char *argv[])
{
	uuid_t	pool_uuid;
	uuid_t	target_uuid;
	char	uuid_str[36];
	char	buf[256];
	char   *dir;
	int	fd;
	int	rc;

	if (argc != 2) {
		printf("usage: %s <dir>\n", basename(argv[0]));
		return 1;
	}

	dir = argv[1];

	uuid_generate(pool_uuid);
	uuid_unparse_lower(pool_uuid, uuid_str);
	snprintf(buf, sizeof(buf), "%s/%s-vos", dir, uuid_str);

	printf("creating file %s\n", buf);

	fd = open(buf, O_CREAT|O_RDWR, 0666);
	assert(fd >= 0);

	rc = posix_fallocate(fd, 0, (1 << 26));	/* 64 MB */
	if (rc != 0) {
		printf("posix_fallocate: %d\n", rc);
		return 1;
	}

	rc = close(fd);
	assert(rc == 0);

	rc = dsms_storage_init();
	assert(rc == 0);

	rc = dsms_pool_create(pool_uuid, dir, target_uuid);

	uuid_unparse_lower(target_uuid, uuid_str);

	printf("dsms_pool_create: %d %s\n", rc, uuid_str);

	return 0;
}
