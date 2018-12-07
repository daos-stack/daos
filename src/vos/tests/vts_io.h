/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_io.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#ifndef __VTS_IO_H__
#define __VTS_IO_H__

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <time.h>
#include "vts_common.h"

#include <daos/common.h>
#include <daos_srv/vos.h>
#include <vos_obj.h>
#include <vos_internal.h>

#define UPDATE_DKEY_SIZE	32
#define UPDATE_DKEY		"dkey"
#define UPDATE_AKEY_SIZE	32
#define UPDATE_AKEY		"akey"
#define UPDATE_AKEY_FIXED	"akey.fixed"
#define	UPDATE_BUF_SIZE		64
#define UPDATE_REC_SIZE		16
#define UPDATE_CSUM_SIZE	32
#define VTS_IO_OIDS		1
#define VTS_IO_KEYS		100000
#define NUM_UNIQUE_COOKIES	20

enum vts_test_flags {
	TF_IT_ANCHOR		= (1 << 0),
	TF_ZERO_COPY		= (1 << 1),
	TF_OVERWRITE		= (1 << 2),
	TF_PUNCH		= (1 << 3),
	TF_REC_EXT		= (1 << 4),
	TF_FIXED_AKEY		= (1 << 5),
	TF_REPORT_AGGREGATION	= (1 << 6),
	IF_USE_ARRAY		= (1 << 7),
	TF_USE_CSUM		= (1 << 8),
	IF_DISABLED		= (1 << 30),
};

#define VTS_BUF_SIZE 128
struct io_test_args {
	char			 fname[VTS_BUF_SIZE];
	struct vos_test_ctx	 ctx;
	daos_unit_oid_t		 oid;
	/* Optional addn container create params */
	uuid_t			 addn_co_uuid;
	daos_handle_t		 addn_co;
	d_list_t		 req_list;
	/* testing flags, see vts_test_flags */
	unsigned long		 ta_flags;
	const char		*dkey;
	const char		*akey;
	int			 ofeat;
	int			 akey_size;
	int			 dkey_size;
	int			 co_create_step;
	bool			 cookie_flag;
};

/** test counters */
struct vts_counter {
	/* To verify during enumeration */
	unsigned long		cn_dkeys;
	/* # dkey with the fixed akey */
	unsigned long		cn_fa_dkeys;
	/** # oids */
	unsigned long		cn_oids;
	/* # punch */
	unsigned long		cn_punch;
};

struct io_req {
	d_list_t		rlist;
	daos_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	struct d_uuid		cookie;
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	daos_epoch_t		epoch;
};


daos_epoch_t		gen_rand_epoch(void);
struct d_uuid		gen_rand_cookie(void);
void			gen_rand_key(char *rkey, char *key, int ksize);
bool			is_found(uuid_t cookie);
daos_unit_oid_t		gen_oid(daos_ofeat_t ofeats);
void			inc_cntr(unsigned long op_flags);
void			inc_cntr_manual(unsigned long op_flags,
					struct vts_counter *cntr);
void			test_args_reset(struct io_test_args *args,
					uint64_t pool_size);
int			io_test_obj_update(struct io_test_args *arg,
					   int epoch, daos_key_t *dkey,
					   daos_iod_t *iod,
					   daos_sg_list_t *sgl,
					   struct d_uuid *cookie,
					   bool verbose);
int			io_test_obj_fetch(struct io_test_args *arg,
					  int epoch, daos_key_t *dkey,
					  daos_iod_t *iod,
					  daos_sg_list_t *sgl,
					  bool verbose);
int			setup_io(void **state);
int			setup_io_int_akey(void **state);
int			setup_io_int_dkey(void **state);
int			setup_io_lex_akey(void **state);
int			setup_io_lex_dkey(void **state);
int			teardown_io(void **state);

#endif

