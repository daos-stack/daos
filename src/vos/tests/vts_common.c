/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of vos
 *
 * vos/tests/vts_common.c
 */
#include <fcntl.h>
#include <errno.h>
#include <vts_common.h>

int gc;

bool
file_exists(const char *filename)
{
	if (access(filename, F_OK) != -1)
		return true;
	else
		return false;
}

int
alloc_gen_fname(char **fname)
{
	char *file_name = NULL;
	int n;

	file_name = malloc(25);
	if (!file_name)
		return -ENOMEM;
	n = snprintf(file_name, 25, VPOOL_NAME);
	snprintf(file_name+n, 25-n, ".%d", gc++);
	*fname = file_name;

	return 0;
}

int
pool_fallocate(char **fname)
{
	int ret = 0, fd;

	ret = alloc_gen_fname(fname);
	if (ret)
		return ret;

	fd = open(*fname, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (fd < 0) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = posix_fallocate(fd, 0, VPOOL_SIZE);
exit:
	return ret;
}

inline void
io_set_oid(daos_unit_oid_t *oid)
{
	oid->id_pub.lo = rand();
	oid->id_pub.mid = rand();
	oid->id_pub.hi = rand();
	oid->id_shard = 0;
	oid->id_pad_32 = rand() % 16;
}
