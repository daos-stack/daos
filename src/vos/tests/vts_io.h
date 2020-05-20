/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#define UPDATE_AKEY_SV		"akey.sv"
#define UPDATE_AKEY_ARRAY	"akey.array"
#define	UPDATE_BUF_SIZE		64
#define UPDATE_REC_SIZE		16
#define UPDATE_CSUM_SIZE	32
#define UPDATE_CSUM_MAX_COUNT	2
#define UPDATE_CSUM_BUF_SIZE	(UPDATE_CSUM_SIZE * UPDATE_CSUM_MAX_COUNT +  \
					2 * UPDATE_CSUM_SIZE)
#define VTS_IO_OIDS		1
#define VTS_DB_KEYS		(D_LOG_ENABLED(DB_TRACE) ? 500 : 100000)
#define VTS_IO_KEYS		(DAOS_ON_VALGRIND ? 100 : VTS_DB_KEYS)

enum vts_test_flags {
	TF_IT_ANCHOR		= (1 << 0),
	TF_ZERO_COPY		= (1 << 1),
	TF_OVERWRITE		= (1 << 2),
	TF_PUNCH		= (1 << 3),
	TF_REC_EXT		= (1 << 4),
	TF_FIXED_AKEY		= (1 << 5),
	IF_USE_ARRAY		= (1 << 6),
	TF_USE_VAL		= (1 << 7),
	TF_USE_CSUMS		= (1 << 8),
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
	/* testing flags, see vts_test_flags */
	unsigned long		 ta_flags;
	const char		*dkey;
	const char		*akey;
	void			*custom;
	int			 ofeat;
	int			 akey_size;
	int			 dkey_size;
	int			 co_create_step;
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

daos_epoch_t		gen_rand_epoch(void);
daos_unit_oid_t		gen_oid(daos_ofeat_t ofeats);
void			inc_cntr(unsigned long op_flags);
void			test_args_reset(struct io_test_args *args,
					uint64_t pool_size);
int			io_test_obj_update(struct io_test_args *arg,
					   daos_epoch_t epoch, uint64_t flags,
					   daos_key_t *dkey, daos_iod_t *iod,
					   d_sg_list_t *sgl,
					   struct dtx_handle *dth,
					   bool verbose);
int			io_test_obj_fetch(struct io_test_args *arg,
					  daos_epoch_t epoch, uint64_t flags,
					  daos_key_t *dkey,
					  daos_iod_t *iod,
					  d_sg_list_t *sgl,
					  bool verbose);
int			setup_io(void **state);
int			teardown_io(void **state);
void			set_iov(d_iov_t *iov, char *buf, int int_flag);

void			vts_key_gen(char *dest, size_t len, bool is_dkey,
				    struct io_test_args *arg);

static inline uint32_t
hash_key(d_iov_t *key, int flag)
{
	if (flag)
		return *(uint64_t *)key->iov_buf;

	return d_hash_string_u32((char *)key->iov_buf, key->iov_len);
}

#endif

