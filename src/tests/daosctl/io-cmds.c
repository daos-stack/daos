/**
 * (C) Copyright 2018-2020 Intel Corporation.
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

/* generic */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>
#include <argp.h>
#include <uuid/uuid.h>

/* daos specific */
#include <daos.h>
#include <daos_api.h>
#include <daos_types.h>
#include <tests_lib.h>
#include <daos_mgmt.h>
#include <daos/common.h>
#include <daos/checksum.h>

#include "common_utils.h"

/**
 * This file contains commands that do some basic I/O operations.
 *
 * For each command there are 3 items of interest: a structure that
 * contains the arguments for the command; a callback function that
 * takes the arguments from argp and puts them in the structure; a
 * function that sends the arguments to the DAOS API and handles the
 * reply.  All commands share the same structure and callback function
 * at present.
 */

static int dts_obj_class = OC_S1;

struct io_cmd_options {
	char          *server_group;
	char          *pool_uuid;
	char          *cont_uuid;
	char          *server_list;
	uint64_t      size;
	daos_obj_id_t *oid;
	char          *pattern;
};

#define UPDATE_CSUM_SIZE	32
#define IOREQ_IOD_NR	5
#define IOREQ_SG_NR	5
#define IOREQ_SG_IOD_NR	5

#define TEST_PATTERN_SIZE 64

static const unsigned char PATTERN_0[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0};

static const unsigned char PATTERN_1[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 63};

struct ioreq {
	daos_handle_t		oh;
	daos_event_t		ev;
	daos_key_t		dkey;
	daos_key_t		akey;
	d_iov_t			val_iov[IOREQ_SG_IOD_NR][IOREQ_SG_NR];
	d_sg_list_t		sgl[IOREQ_SG_IOD_NR];
	daos_recx_t		rex[IOREQ_SG_IOD_NR][IOREQ_IOD_NR];
	daos_epoch_range_t	erange[IOREQ_SG_IOD_NR][IOREQ_IOD_NR];
	daos_iod_t		iod[IOREQ_SG_IOD_NR];
	daos_iod_type_t		iod_type;
	uint64_t		fail_loc;
};

struct container_info {
	uuid_t         pool_uuid;
	char           *server_group;
	d_rank_list_t  pool_service_list;
	daos_handle_t  poh;
	uuid_t         cont_uuid;
	daos_handle_t  coh;
};

/**
 * Callback function for io commands works with argp to put
 * all the arguments into a structure.
 */
static int
parse_cont_args_cb(int key, char *arg,
		   struct argp_state *state)
{
	struct io_cmd_options *options = state->input;

	switch (key) {
	case 'c':
		options->cont_uuid = arg;
		break;
	case 'i':
		options->pool_uuid = arg;
		break;
	case 'l':
		options->server_list = arg;
		break;
	case 'o':
		parse_oid(arg, options->oid);
		break;
	case 'p':
		options->pattern = arg;
		break;
	case 's':
		options->server_group = arg;
		break;
	case 'z':
		parse_size(arg, &(options->size));
		break;
	}
	return 0;
}

int
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type)
{
	int rc;
	int i;

	memset(req, 0, sizeof(*req));

	req->iod_type = iod_type;

	/* init sgl */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		req->sgl[i].sg_nr = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init record extent */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		int j;

		for (j = 0; j < IOREQ_IOD_NR; j++) {
			req->rex[i][j].rx_nr = 1;
			req->rex[i][j].rx_idx = 0;

			/** epoch range: required by the wire format */
			req->erange[i][j].epr_lo = 0;
			req->erange[i][j].epr_hi = DAOS_EPOCH_MAX;
		}

		/* I/O descriptor */
		req->iod[i].iod_recxs = req->rex[i];
		req->iod[i].iod_nr = IOREQ_IOD_NR;
		req->iod[i].iod_type = iod_type;
	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(coh, oid, 0, &req->oh, NULL);
	return rc;
}

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	d_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_io_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	if (nr < 1 || nr > IOREQ_SG_IOD_NR) {
		printf("Invalid request, nr is %i\n", nr);
		exit(-EINVAL);
	}
	/** akey */
	for (i = 0; i < nr; i++)
		d_iov_set(&req->iod[i].iod_name, (void *)akey[i],
			     strlen(akey[i]));
}

static int
open_container(struct container_info *oc_info)
{
	int              rc;
	unsigned int     flag = DAOS_PC_EX;
	daos_pool_info_t pinfo = {0};
	daos_cont_info_t cinfo;

	rc = daos_pool_connect(oc_info->pool_uuid, oc_info->server_group,
			       &oc_info->pool_service_list, flag,
			       &oc_info->poh, &pinfo, NULL);
	if (rc) {
		printf("Pool connect fail, result: %d\n", rc);
		exit(-EINVAL);
	}

	rc = daos_cont_open(oc_info->poh, oc_info->cont_uuid, DAOS_COO_RW,
			    &oc_info->coh, &cinfo, NULL);
	if (rc) {
		printf("daos_cont_open failed, rc: %d\n", rc);
		exit(-EINVAL);
	}
	return 0;
}

void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = daos_obj_close(req->oh, NULL);
	if (rc != 0)
		printf("problem closing object %i\n", rc);
	daos_fail_loc_set(0);

}

/* no wait for async insert, for sync insert it still will block */
static int
insert_internal_nowait(daos_key_t *dkey, int nr, d_sg_list_t *sgls,
		       daos_iod_t *iods, daos_handle_t th, struct ioreq *req)
{
	int rc;

	/** execute update operation */
	rc = daos_obj_update(req->oh, th, 0, dkey, nr, iods, sgls, NULL);

	return rc;
}

static void
lookup_internal(daos_key_t *dkey, int nr, d_sg_list_t *sgls,
		daos_iod_t *iods, daos_handle_t th, struct ioreq *req,
		bool empty)
{
	int rc;


	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, th, 0, dkey, nr, iods, sgls, NULL, NULL);
	if (rc != 0) {
		printf("object fetch failed with %i\n", rc);
		exit(1);
	}

	/* Only single iov for each sgls during the test */
	if (!empty && req->ev.ev_error == 0)
		if (sgls->sg_nr_out != 1) {
			printf("something went wrong, I don't know what\n");
			exit(1);
		}
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value,
		     daos_size_t *size, int nr)
{
	d_sg_list_t *sgl = req->sgl;
	int i;

	if (nr < 1 || nr > IOREQ_SG_IOD_NR) {
		printf("Invalid request, nr is %i\n", nr);
		exit(-EINVAL);
	}
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 1;
		d_iov_set(&sgl[i].sg_iovs[0], value[i], size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *size, bool lookup,
		     uint64_t *idx, int nr)
{
	daos_iod_t *iod = req->iod;
	int i;

	if (nr < 1 || nr > IOREQ_SG_IOD_NR) {
		printf("Invalid request, nr is %i\n", nr);
		exit(-EINVAL);
	}

	for (i = 0; i < nr; i++) {
		/* record extent */
		iod[i].iod_type = req->iod_type;
		iod[i].iod_size = size[i];
		if (req->iod_type == DAOS_IOD_ARRAY) {
			iod[i].iod_recxs[0].rx_idx = idx[i] + i * 10485760;
			iod[i].iod_recxs[0].rx_nr = 1;
		}
		iod[i].iod_nr = 1;
	}
}

static void
insert_single(const char *dkey, const char *akey, uint64_t idx,
	      void *value, daos_size_t size, daos_handle_t th,
	      struct ioreq *req)
{
	int nr = 1;

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, &akey, nr);

	/* set sgl */
	if (value != NULL)
		ioreq_sgl_simple_set(req, &value, &size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, &size, false, &idx, nr);

	int rc = insert_internal_nowait(&req->dkey, nr,
					value == NULL ? NULL : req->sgl,
					req->iod, th, req);

	if (rc != 0)
		printf("object update failed \n");
}

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t size, daos_handle_t th,
	      struct ioreq *req)
{
	/*daos_size_t read_size = DAOS_REC_ANY;*/
	daos_size_t read_size = 128;

	fflush(stdout);
	/* dkey */
	ioreq_dkey_set(req, dkey);
	/* akey */
	ioreq_io_akey_set(req, &akey, 1);
	/* set sgl */
	ioreq_sgl_simple_set(req, &val, &size, 1);

	/* set iod */
	ioreq_iod_simple_set(req, &read_size, true, &idx, 1);
	lookup_internal(&req->dkey, 1, req->sgl, req->iod, th, req,
			false);
}

/**
 * Process a write command.
 */
int
cmd_write_pattern(int argc, const char **argv, void *ctx)
{
	int              rc = -ENXIO;
	daos_obj_id_t    oid;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	const char	 akey[] = "test_update akey";

	const unsigned char *rec = PATTERN_1;

	struct container_info cinfo;

	struct argp_option options[] = {
		{"server-group", 's', "SERVER-GROUP", 0,
		 "ID of the server group that owns the pool"},
		{"servers",       'l',   "server rank-list", 0,
		 "Pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{"p-uuid", 'i', "UUID", 0,
		 "ID of the pool where data is to be written."},
		{"c-uuid", 'c', "UUID", 0,
		 "ID of the container where data is to be written."},
		{"size",           'z',    "size",             0,
		 "How much to write in bytes or with k/m/g (e.g. 10g)"},
		{"pattern",       'p',   "pattern",           0,
		 "Data pattern to be written, one of: [0, 1]"},
		{0}
	};
	struct argp argp = {options, parse_cont_args_cb};

	struct io_cmd_options io_options = {"daos_server",
					    NULL, NULL, NULL,
					    0, NULL, "all_zeros"};

	cinfo.server_group = io_options.server_group;
	cinfo.pool_service_list = (d_rank_list_t){NULL, 0};

	cinfo.server_group = io_options.server_group;
	cinfo.pool_service_list = (d_rank_list_t){NULL, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &io_options);

	/* uuid needs extra parsing */
	if (io_options.pool_uuid == NULL)
		return -EINVAL;
	rc = uuid_parse(io_options.pool_uuid, cinfo.pool_uuid);
	if (io_options.cont_uuid == NULL)
		return -EINVAL;
	rc = uuid_parse(io_options.cont_uuid, cinfo.cont_uuid);

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(io_options.server_list,
			     &cinfo.pool_service_list);
	if (rc < 0) {
		D_PRINT("Rank list parameter parsing failed with %i\n", rc);
		return rc;
	}

	rc = open_container(&cinfo);

	oid = dts_oid_gen(dts_obj_class, 0, 0);

	if (!strncmp("all_zeros", io_options.pattern, 3))
		rec = PATTERN_0;
	else if (!strncmp("sequential", io_options.pattern, 3))
		rec = PATTERN_1;

	ioreq_init(&req, cinfo.coh, oid, DAOS_IOD_SINGLE);

	/** Insert */
	insert_single(dkey, akey, 0, (void *)rec, 64, DAOS_TX_NONE, &req);

	/** done with the container */
	daos_cont_close(cinfo.coh, NULL);
	printf("%" PRIu64 "-%" PRIu64 "\n", oid.hi, oid.lo);

	if (cinfo.poh.cookie != 0)
		daos_pool_disconnect(cinfo.poh, NULL);
	return rc;
}

/**
 * Read data written with the write-pattern command and verify
 * that its correct.
 */
int
cmd_verify_pattern(int argc, const char **argv, void *ctx)
{
	char buf[128];
	int rc = 0;
	struct container_info cinfo;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	const char	 akey[] = "test_update akey";

	struct argp_option options[] = {
		{"server-group", 's', "SERVER-GROUP", 0,
		 "ID of the server group that owns the pool"},
		{"servers",       'l',   "server rank-list", 0,
		 "pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{"p-uuid", 'i', "UUID", 0,
		 "ID of the pool that hosts the container to be read from."},
		{"c-uuid", 'c', "UUID", 0,
		 "ID of the container."},
		{"oid", 'o', "OID", 0, "ID of the object."},
		{"size",           'z',    "size",             0,
		 "how much to read in bytes or with k/m/g (e.g. 10g)"},
		{"pattern",       'p',   "pattern",           0,
		 "which of the available data patterns to verify"},
		{0}
	};
	daos_obj_id_t	 oid;
	struct argp argp = {options, parse_cont_args_cb};
	struct io_cmd_options io_options = {"daos_server",
					    NULL, NULL, NULL,
					    0, &oid, "all_zeros"};

	cinfo.server_group = io_options.server_group;
	cinfo.pool_service_list = (d_rank_list_t){NULL, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &io_options);

	/* uuid needs extra parsing */
	if (io_options.pool_uuid == NULL)
		return -EINVAL;
	rc = uuid_parse(io_options.pool_uuid, cinfo.pool_uuid);
	if (io_options.cont_uuid == NULL)
		return -EINVAL;
	rc = uuid_parse(io_options.cont_uuid, cinfo.cont_uuid);

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(io_options.server_list,
			     &cinfo.pool_service_list);
	if (rc < 0) {
		D_PRINT("Rank list parameter parsing failed with %i\n", rc);
		return rc;
	}

	rc = open_container(&cinfo);

	printf("%" PRIu64 "-%" PRIu64 "\n", oid.hi, oid.lo);
	ioreq_init(&req, cinfo.coh, oid, DAOS_IOD_SINGLE);

	memset(buf, 0, sizeof(buf));
	lookup_single(dkey, akey, 0, buf, sizeof(buf), DAOS_TX_NONE, &req);

	/** Verify data consistency */
	printf("size = %lu\n", req.iod[0].iod_size);
	if (req.iod[0].iod_size != TEST_PATTERN_SIZE) {
		printf("sizes don't match\n");
		exit(1);
	}

	for (int i = 0; i < TEST_PATTERN_SIZE; i++) {
		if (buf[i] != PATTERN_1[i]) {
			printf("Data mismatch at position %i value %i",
			       i, buf[i]);
			break;
		}
	}

	ioreq_fini(&req);
	if (cinfo.poh.cookie != 0)
		daos_pool_disconnect(cinfo.poh, NULL);

	return rc;
}
