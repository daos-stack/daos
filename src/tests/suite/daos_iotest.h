/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_iotest.h
 */
#ifndef __DAOS_IOTEST_H_
#define __DAOS_IOTEST_H_

#include "daos_test.h"
#include <sys/stat.h>
#include <fcntl.h>

extern int dts_obj_class;
extern int dts_obj_replica_cnt;

extern int dts_ec_obj_class;
extern int dts_ec_grp_size;

#define OW_IOD_SIZE	1024ULL
#define SEGMENT_SIZE (10 * 1048576) /* 10MB */
#define IO_SIZE_NVME (5ULL << 10) /* all records  >= 4K */
#define IO_SIZE_SCM 64

void
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type, test_arg_t *arg);

void
ioreq_fini(struct ioreq *req);

void
insert_single(const char *dkey, const char *akey, uint64_t idx, void *value,
	      daos_size_t iod_size, daos_handle_t th, struct ioreq *req);

void
insert_single_with_flags(const char *dkey, const char *akey, uint64_t idx,
			 void *value, daos_size_t iod_size, daos_handle_t th,
			 struct ioreq *req, uint64_t flags);

void
insert_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *value, daos_size_t iod_size, int rx_nr,
			daos_handle_t th, struct ioreq *req);

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t data_size, daos_handle_t th,
	      struct ioreq *req);

void
lookup_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *val, daos_size_t iod_size, daos_size_t data_size,
			daos_handle_t th, struct ioreq *req);

void
lookup_empty_single(const char *dkey, const char *akey, uint64_t idx,
		    void *val, daos_size_t data_size, daos_handle_t th,
		    struct ioreq *req);

int
enumerate_dkey(daos_handle_t th, uint32_t *number, daos_key_desc_t *kds,
	       daos_anchor_t *anchor, void *buf, daos_size_t len,
	       struct ioreq *req);
int
enumerate_akey(daos_handle_t th, char *dkey, uint32_t *number,
	       daos_key_desc_t *kds, daos_anchor_t *anchor, void *buf,
	       daos_size_t len, struct ioreq *req);
void
enumerate_rec(daos_handle_t th, char *dkey, char *akey,
	      daos_size_t *size, uint32_t *number, daos_recx_t *recxs,
	      daos_epoch_range_t *eprs, daos_anchor_t *anchor, bool incr,
	      struct ioreq *req);

void
insert(const char *dkey, int nr, const char **akey, daos_size_t *iod_size,
       int *rx_nr, uint64_t *idx, void **val, daos_handle_t th,
       struct ioreq *req, uint64_t flags);

void
insert_nowait(const char *dkey, int nr, const char **akey,
	      daos_size_t *iod_size, int *rx_nr, uint64_t *idx, void **val,
	      daos_handle_t th, struct ioreq *req, uint64_t flags);

void
insert_wait(struct ioreq *req);

void
insert_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req);

void
inset_recxs_dkey_uint64(uint64_t *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req);

void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
	daos_size_t *iod_size, void **val, daos_size_t *data_size,
	daos_handle_t th, struct ioreq *req, bool empty);

void
punch_obj(daos_handle_t th, struct ioreq *req);

void
punch_dkey(const char *dkey, daos_handle_t th, struct ioreq *req);

void
punch_dkey_with_flags(const char *dkey, daos_handle_t th, struct ioreq *req,
		      uint64_t flags);

void
punch_akey(const char *dkey, const char *akey, daos_handle_t th,
	   struct ioreq *req);

void
punch_akey_with_flags(const char *dkey, const char *akey, daos_handle_t th,
		      struct ioreq *req, uint64_t flags);

void
punch_recxs(const char *dkey, const char *akey, daos_recx_t *recxs,
	    int nr, daos_handle_t th, struct ioreq *req);
void
punch_single(const char *dkey, const char *akey, uint64_t idx,
	     daos_handle_t th, struct ioreq *req);

void
lookup_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req);

void
io_simple_internal(void **state, daos_obj_id_t oid, unsigned int size,
		   daos_iod_type_t iod_type, const char dkey[],
	const char akey[]);

void
close_reopen_coh_oh(test_arg_t *arg, struct ioreq *req, daos_obj_id_t oid);

int
obj_setup(void **state);

int
obj_teardown(void **state);

int io_conf_run(test_arg_t *arg, const char *io_conf);

int pool_storage_info(test_arg_t *arg, daos_pool_info_t *pinfo);

/* below list the structure defined for epoch io testing */

enum test_level {
	TEST_LVL_DAOS		= 0,
	TEST_LVL_VOS		= 1,
	/* fake/file IO to simulate/replay, use it for verification */
	TEST_LVL_FIO		= 2,
	TEST_LVLS		= 3,
};

enum test_op_type {
	TEST_OP_MIN		= 0,
	TEST_OP_UPDATE		= 0,
	TEST_OP_PUNCH		= 1,
	/* above are modification OP, below are read-only OP */
	TEST_OP_FETCH		= 2,
	TEST_OP_ENUMERATE	= 3,
	TEST_OP_ADD		= 4,
	TEST_OP_EXCLUDE		= 5,
	TEST_OP_POOL_QUERY	= 6,
	TEST_OP_MAX		= 6,
};

static inline bool
test_op_is_modify(int op)
{
	return (op == TEST_OP_UPDATE || op == TEST_OP_PUNCH);
}

struct test_op_record;
typedef int (*test_op_cb)(test_arg_t *arg, struct test_op_record *op,
			  char **buf, daos_size_t *buf_size);

struct test_op_dict {
	enum test_op_type	 op_type;
	char			*op_str;
	test_op_cb		 op_cb[TEST_LVLS];
};

struct test_key_record {
	/* link to epoch_io_args::op_list */
	d_list_t		 or_list;
	char			*or_dkey;
	char			*or_akey;
	int			 or_fd_array;
	int			 or_fd_single;
	daos_size_t		 or_iod_size;
	/* the epoch last replayed */
	daos_epoch_t		 or_replayed_epoch;
	/* modification OP queue on this key, ordered by tid */
	d_list_t		 or_queue;
	/* # OP in the queue */
	uint32_t		 or_op_num;
};

struct test_update_fetch_arg {
	daos_recx_t		*ua_recxs;
	int			*ua_values;
	int			ua_recx_num;
	int			ua_single_value;
	uint32_t                 ua_array : 1, /* false for single */
	    ua_verify                     : 1;
	bool			snap;
};

struct test_add_exclude_arg {
	d_rank_t	ua_rank;
	int		ua_tgt;
};

struct test_punch_arg {
	bool		 pa_singv;
	daos_recx_t	*pa_recxs;
	int		 pa_recxs_num;
};

/* one OP record per cmd line in the ioconf file */
struct test_op_record {
	/* link to test_key_record::or_queue */
	d_list_t		or_queue_link;
	struct test_key_record	*or_key_rec; /* back pointer */
	int			tx;
	daos_epoch_t		*snap_epoch;
	enum test_op_type	or_op;
	union {
		struct test_update_fetch_arg	uf_arg;
		struct test_punch_arg		pu_arg;
		struct test_add_exclude_arg	ae_arg;
	};
};

#endif
