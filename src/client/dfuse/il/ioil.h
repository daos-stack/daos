/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __IOIL_H__
#define __IOIL_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daos_fs.h"

struct ioil_cont {
	/* Container open handle */
	daos_handle_t	ioc_coh;
	/* ioil pool descriptor */
	struct ioil_pool *ioc_pool;
	/* uuid of container */
	uuid_t		ioc_uuid;
	/* dfs handle */
	dfs_t		*ioc_dfs;
	/* List of containers */
	d_list_t	ioc_containers;
	/* Number of files open in container */
	int		ioc_open_count;
};

struct fd_entry {
	struct ioil_cont	*fd_cont;
	dfs_obj_t		*fd_dfsoh;
	off_t			fd_pos;
	int			fd_flags;
	int			fd_status;
};

ssize_t
ioil_do_pread(char *buff, size_t len, off_t position,
	      struct fd_entry *entry, int *errcode);
ssize_t
ioil_do_preadv(const struct iovec *iov, int count, off_t position,
	       struct fd_entry *entry, int *errcode);
ssize_t
ioil_do_writex(const char *buff, size_t len, off_t position,
	       struct fd_entry *entry, int *errcode);
ssize_t
ioil_do_pwritev(const struct iovec *iov, int count, off_t position,
		struct fd_entry *entry, int *errcode);

#endif /* __IOIL_H__ */
