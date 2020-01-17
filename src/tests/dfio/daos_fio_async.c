/**
 * (C) Copyright 2020 Intel Corporation.
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

/*
 * Implementation of Async DAOS File System Fio Plugin
 */
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include <fio.h>
#include <optgroup.h>

#include <gurt/common.h>
#include <gurt/hash.h>
#include <daos.h>
#include <daos_fs.h>

#define ERR(MSG)							\
do {									\
	fprintf(stderr, "ERROR (%s:%d): %s",				\
		__FILE__, __LINE__, MSG);				\
	fflush(stderr);							\
	return -1;							\
} while (0)

#define DCHECK(rc, format, ...)						\
do {									\
        int _rc = (rc);							\
									\
	if (_rc < 0) {							\
		fprintf(stderr, "ERROR (%s:%d): %d: "			\
			format"\n", __FILE__, __LINE__,  _rc,		\
			##__VA_ARGS__);					\
		fflush(stderr);						\
		return -1;						\
        }                                                               \
} while (0)

bool daos_initialized;

struct daos_iou {
	struct io_u	*io_u;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_event_t	ev;
	bool		complete;
};

struct daos_data {
	dfs_t		*dfs;
	daos_handle_t	poh, coh, eqh;
	dfs_obj_t	*obj;
	struct io_u	**io_us;
	int		queued;
	int		num_ios;
};

struct daos_fio_options {
	void		*pad;
	char		*pool;
	char		*cont;
	char		*svcl;
	daos_size_t	chsz;
};

static struct fio_option options[] = {
	{
		.name		= "daos_pool",
		.lname		= "DAOS pool uuid",
		.type		= FIO_OPT_STR_STORE,
		.off1		= offsetof(struct daos_fio_options, pool),
		.help		= "DAOS pool uuid",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name           = "daos_cont",
		.lname          = "DAOS container uuid",
		.type           = FIO_OPT_STR_STORE,
		.off1           = offsetof(struct daos_fio_options, cont),
		.help           = "DAOS container uuid",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name           = "daos_svcl",
		.lname          = "DAOS pool replicated service",
		.type           = FIO_OPT_STR_STORE,
		.off1           = offsetof(struct daos_fio_options, svcl),
		.help           = "DAOS SVCL",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name           = "daos_chsz",
		.lname          = "DAOS chunk size in bytes",
		.type           = FIO_OPT_INT,
		.off1           = offsetof(struct daos_fio_options, chsz),
		.help           = "DAOS chunk size in bytes (default: 1MiB)",
		.def		= "1048576",
		.category	= FIO_OPT_C_ENGINE,
		.group		= FIO_OPT_G_INVALID,
	},
	{
		.name           = NULL,
	},
};

static int
daos_fio_init(struct thread_data *td)
{
	struct daos_fio_options	*eo = td->eo;
	struct daos_data	*dd;
	uuid_t			pool_uuid, co_uuid;
	d_rank_list_t		*svcl = NULL;
	daos_pool_info_t	pool_info;
	daos_cont_info_t	co_info;
	int			rc;

	if (daos_initialized)
		return 0;

	if (!eo->pool || !eo->cont || !eo->svcl)
		ERR("Missing required DAOS options\n");

	/* Allocate space for DAOS-related data */
	dd = malloc(sizeof(*dd));
	dd->queued = 0;
	dd->num_ios = td->o.iodepth;
	dd->io_us = calloc(dd->num_ios, sizeof(struct io_u *));
	if (dd->io_us == NULL)
		ERR("Failed to allocate IO queue\n");

	rc = daos_init(); 
	if (rc != -DER_ALREADY && rc)
		DCHECK(rc, "Failed to initialize daos");

	rc = uuid_parse(eo->pool, pool_uuid);
	DCHECK(rc, "Failed to parse 'Pool uuid': %s", eo->pool);
	rc = uuid_parse(eo->cont, co_uuid);
	DCHECK(rc, "Failed to parse 'Cont uuid': %s", eo->cont);
	svcl = daos_rank_list_parse(eo->svcl, ":");
	if (svcl == NULL)
		ERR("Failed to allocate svcl");

	rc = daos_pool_connect(pool_uuid, NULL, svcl, DAOS_PC_RW,
			       &dd->poh, &pool_info, NULL);
	d_rank_list_free(svcl);
	DCHECK(rc, "Failed to connect to pool");

	rc = daos_cont_open(dd->poh, co_uuid, DAOS_COO_RW, &dd->coh, &co_info,
			    NULL);
	DCHECK(rc, "Failed to open container");

	rc = dfs_mount(dd->poh, dd->coh, O_RDWR, &dd->dfs);
	DCHECK(rc, "Failed to mount DFS namespace");

	td->io_ops_data = dd;
	printf("[Init] pool_id=%s, container_id=%s, svcl=%s, chunk_size=%ld\n",
	       eo->pool, eo->cont, eo->svcl, eo->chsz);
	daos_initialized = true;

	return 0;
}

static void
daos_fio_cleanup(struct thread_data *td)
{
	struct daos_data *dd = td->io_ops_data;
	int rc;

	rc = dfs_umount(dd->dfs);
	rc = daos_cont_close(dd->coh, NULL);
	rc = daos_pool_disconnect(dd->poh, NULL);
	rc = daos_fini();

	free(dd->io_us);
	free(dd);
}

static int
daos_fio_open(struct thread_data *td, struct fio_file *f)
{
	char *file_name = f->file_name;
	struct daos_data *dd = td->io_ops_data;
	mode_t mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
	int fd_oflag = O_CREAT | O_RDWR;
	daos_oclass_id_t oc = OC_SX;
	struct daos_fio_options *eo = td->eo;
	daos_size_t chunk_size = eo->chsz ? eo->chsz : 0;
	int rc;

	rc = dfs_open(dd->dfs, NULL, file_name, mode, fd_oflag,
		      oc, chunk_size, NULL, &dd->obj);
	DCHECK(rc, "dfs_open() failed.");

	return 0;
}

static int
daos_fio_unlink(struct thread_data *td, struct fio_file *f)
{
	char *file_name = f->file_name;
	struct daos_data *dd = td->io_ops_data;
	int rc;

	rc = dfs_remove(dd->dfs, NULL, file_name, false, NULL);
	DCHECK(rc, "dfs_remove() failed.");

	return 0;
}

static int
daos_fio_invalidate(struct thread_data *td, struct fio_file *f)
{
	return 0;
}

static void
daos_fio_io_u_free(struct thread_data *td, struct io_u *io_u)
{
	struct daos_iou *io = io_u->engine_data;

	if (io) {
		io_u->engine_data = NULL;
		free(io);
	}
}

static int
daos_fio_io_u_init(struct thread_data *td, struct io_u *io_u)
{
	struct daos_iou *io;

	io = malloc(sizeof(struct daos_iou));
	if (!io) {
		td_verror(td, errno, "malloc");
		return 1;
	}
	io->io_u = io_u;
	io_u->engine_data = io;
	return 0;
}

static struct io_u *
daos_fio_event(struct thread_data *td, int event)
{
	struct daos_data *dd = td->io_ops_data;

	return dd->io_us[event];
}

static int daos_fio_getevents(struct thread_data *td, unsigned int min,
			      unsigned int max, const struct timespec *t)
{
	struct daos_data *dd = td->io_ops_data;
	unsigned int events = 0;
	struct io_u *io_u;
	int i;

	do {
		io_u_qiter(&td->io_u_all, io_u, i) {
			struct daos_iou *io = io_u->engine_data;
			bool ev_flag;

			if (io->complete)
				continue;

			daos_event_test(&io->ev, DAOS_EQ_NOWAIT, &ev_flag);
			if (!ev_flag)
				continue;

			if (io->ev.ev_error)
				io_u->error = io->ev.ev_error;
			else
				io_u->resid = 0;
			dd->io_us[events] = io_u;
			dd->queued--;
			daos_event_fini(&io->ev);
			io->complete = true;
			events++;
		}
		if (events < min)
			continue;
		break;
	} while (1);

	return events;
}

static int
daos_fio_queue(struct thread_data *td, struct io_u *io_u)
{
	struct daos_data *dd = td->io_ops_data;
	d_iov_t iov;
	struct daos_iou *io = io_u->engine_data;
	daos_off_t offset = io_u->offset;
	daos_size_t ret;
	int rc;

	if (dd->queued == td->o.iodepth)
		return FIO_Q_BUSY;

	io->sgl.sg_nr = 1; 
	io->sgl.sg_nr_out = 0; 
	d_iov_set(&io->iov, io_u->xfer_buf, io_u->xfer_buflen);
	io->sgl.sg_iovs = &io->iov;

	io->complete = false;
	rc = daos_event_init(&io->ev, DAOS_HDL_INVAL, NULL);
	DCHECK(rc, "daos_event_init() failed.");

	switch (io_u->ddir) {
	case DDIR_WRITE:
		rc = dfs_write(dd->dfs, dd->obj, &io->sgl, offset, &io->ev);
		DCHECK(rc, "dfs_write() failed.");
		break;
	case DDIR_READ:
		rc = dfs_read(dd->dfs, dd->obj, &io->sgl, offset, &ret, &io->ev);
		DCHECK(rc, "dfs_read() failed.");
		break;
	default:
		ERR("Invalid IO type\n");
	}

	dd->queued++;
	return FIO_Q_QUEUED;
}

static int
daos_fio_get_file_size(struct thread_data *td, struct fio_file *f)
{
	char *file_name = f->file_name;
	struct daos_data *dd = td->io_ops_data;
	struct stat stbuf = {0};
	int rc;

	if (!daos_initialized)
		return 0;

	rc = dfs_stat(dd->dfs, NULL, file_name, &stbuf);
	DCHECK(rc, "dfs_stat() failed.");

	f->real_file_size = stbuf.st_size;
	return 0;
}

static int
daos_fio_close(struct thread_data *td, struct fio_file *f)
{
	struct daos_data *dd = td->io_ops_data;
	dfs_obj_t *parent = NULL;
	int rc;

	rc = dfs_release(dd->obj);
	DCHECK(rc, "dfs_release() Failed");

	return 0;
}

static int
daos_fio_prep(struct thread_data fio_unused *td, struct io_u *io_u)
{
	return 0;
}

struct ioengine_ops ioengine = {
	.name			= "fio_daos_dfs_async",
	.version		= FIO_IOOPS_VERSION,
	.flags			= FIO_DISKLESSIO | FIO_NODISKUTIL | FIO_RAWIO,
	.init			= daos_fio_init,
	.prep			= daos_fio_prep,
	.cleanup		= daos_fio_cleanup,
	.open_file		= daos_fio_open,
	.invalidate		= daos_fio_invalidate,
	.queue			= daos_fio_queue,
	.getevents		= daos_fio_getevents,
	.event			= daos_fio_event,
	.io_u_init		= daos_fio_io_u_init,
	.io_u_free		= daos_fio_io_u_free,
	.close_file		= daos_fio_close,
	.unlink_file		= daos_fio_unlink,
	.get_file_size		= daos_fio_get_file_size,
	.option_struct_size	= sizeof(struct daos_fio_options),
	.options		= options,
};

