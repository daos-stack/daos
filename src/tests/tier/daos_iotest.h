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
 * This file is part of daos
 *
 * tests/suite/daos_iotest.h
 */
#ifndef __DAOS_IOTEST_H_
#define __DAOS_IOTEST_H_

#include "daos_test.h"

#define UPDATE_CSUM_SIZE	32
#define IOREQ_IOD_NR	5
#define IOREQ_SG_NR	5
#define IOREQ_SG_IOD_NR	5

struct ioreq {
	daos_handle_t		oh;
	test_arg_t		*arg;
	daos_event_t		ev;
	daos_key_t		dkey;
	daos_iov_t		val_iov[IOREQ_SG_IOD_NR][IOREQ_SG_NR];
	daos_sg_list_t		sgl[IOREQ_SG_IOD_NR];
	daos_csum_buf_t		csum;
	char			csum_buf[UPDATE_CSUM_SIZE];
	daos_recx_t		rex[IOREQ_SG_IOD_NR][IOREQ_IOD_NR];
	daos_epoch_range_t	erange[IOREQ_SG_IOD_NR][IOREQ_IOD_NR];
	daos_iod_t		iod[IOREQ_SG_IOD_NR];
	daos_iod_type_t		iod_type;
	uint64_t		fail_loc;
};

#define SEGMENT_SIZE (10 * 1048576) /* 10MB */

void
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type, test_arg_t *arg);

void
ioreq_fini(struct ioreq *req);

void
insert_single(const char *dkey, const char *akey, uint64_t idx,
	      void *value, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req);

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req);

void
enumerate_dkey(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	       daos_hash_out_t *anchor, void *buf, daos_size_t len,
	       struct ioreq *req);

void
insert(const char *dkey, int nr, const char **akey, uint64_t *idx, void **val,
	daos_size_t *size, daos_epoch_t *epoch, struct ioreq *req);

void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
	daos_size_t *read_size, void **val, daos_size_t *size,
	daos_epoch_t *epoch, struct ioreq *req);

int
obj_setup(void **state);

int
obj_teardown(void **state);

#endif
