/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/btree.h>
#include <daos/dtx.h>
#include <daos/tests_lib.h>
#include "utest_common.h"

enum ik_btr_opc {
	BTR_OPC_UPDATE,
	BTR_OPC_LOOKUP,
	BTR_OPC_DELETE,
	BTR_OPC_DELETE_RETAIN,
};

struct test_input_value {
	bool				input;
	enum	 ik_btr_opc		opc;
	char				*optval;
};

struct test_input_value tst_fn_val;

/**
 * An example for integer key btree.
 */

#define IK_ORDER_DEF	16

static int ik_order = IK_ORDER_DEF;

struct utest_context		*ik_utx;
struct umem_attr		*ik_uma;
static umem_off_t		 ik_root_off;
static struct btr_root		*ik_root;
static daos_handle_t		 ik_toh;


/** integer key record */
struct ik_rec {
	uint64_t	ir_key;
	uint32_t	ir_val_size;
	uint32_t	ir_val_msize;
	umem_off_t	ir_val_off;
};

static char	**test_group_args;
static int	test_group_start;
static int	test_group_stop;

#define IK_TREE_CLASS	100
#define POOL_NAME "/mnt/daos/btree-test"
#define POOL_SIZE ((1024 * 1024 * 1024ULL))

/** customized functions for btree */
static int
ik_hkey_size(void)
{
	struct ik_rec irec;
	return sizeof(irec.ir_key);
}

static void
ik_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	uint64_t	*ikey;

	ikey = (uint64_t *)key_iov->iov_buf;
	/* ikey = dummy_hash(ikey); */
	memcpy(hkey, ikey, sizeof(*ikey));
}

static int
ik_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		  d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	umem_off_t		 irec_off;
	struct ik_rec		*irec;
	char			*vbuf;

	irec_off = umem_zalloc(&tins->ti_umm, sizeof(struct ik_rec));
	D_ASSERT(!UMOFF_IS_NULL(irec_off)); /* lazy bone... */

	irec = umem_off2ptr(&tins->ti_umm, irec_off);

	irec->ir_key = *(int *)key_iov->iov_buf;
	irec->ir_val_size = irec->ir_val_msize = val_iov->iov_len;

	irec->ir_val_off = umem_alloc(&tins->ti_umm, val_iov->iov_len);
	D_ASSERT(!UMOFF_IS_NULL(irec->ir_val_off));

	vbuf = umem_off2ptr(&tins->ti_umm, irec->ir_val_off);
	memcpy(vbuf, (char *)val_iov->iov_buf, val_iov->iov_len);

	rec->rec_off = irec_off;
	return 0;
}

static int
ik_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance *umm = &tins->ti_umm;
	struct ik_rec *irec;

	irec = umem_off2ptr(umm, rec->rec_off);

	if (args != NULL) {
		umem_off_t *rec_ret = (umem_off_t *) args;
		 /** Provide the buffer to user */
		*rec_ret	= rec->rec_off;
		return 0;
	}
	utest_free(ik_utx, irec->ir_val_off);
	utest_free(ik_utx, rec->rec_off);

	return 0;
}

static int
ik_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct ik_rec	*irec;
	char		*val;
	int		 val_size;
	int		 key_size;

	if (key_iov == NULL && val_iov == NULL)
		return -EINVAL;

	irec = (struct ik_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	val_size = irec->ir_val_size;
	key_size = sizeof(irec->ir_key);

	val = umem_off2ptr(&tins->ti_umm, irec->ir_val_off);
	if (key_iov != NULL) {
		key_iov->iov_len = key_size;
		if (key_iov->iov_buf == NULL)
			key_iov->iov_buf = &irec->ir_key;
		else if (key_iov->iov_buf_len >= key_size)
			memcpy(key_iov->iov_buf, &irec->ir_key, key_size);
	}

	if (val_iov != NULL) {
		val_iov->iov_len = val_size;
		if (val_iov->iov_buf == NULL)
			val_iov->iov_buf = val;
		else if (val_iov->iov_buf_len >= val_size)
			memcpy(val_iov->iov_buf, val, val_size);

	}
	return 0;
}

static char *
ik_rec_string(struct btr_instance *tins, struct btr_record *rec,
		  bool leaf, char *buf, int buf_len)
{
	struct ik_rec	*irec = NULL;
	char		*val;
	int		 nob;
	uint64_t	 ikey;

	if (!leaf) { /* NB: no record body on intermediate node */
		memcpy(&ikey, &rec->rec_hkey[0], sizeof(ikey));
		snprintf(buf, buf_len, DF_U64, ikey);
		return buf;
	}

	irec = (struct ik_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	ikey = irec->ir_key;
	nob = snprintf(buf, buf_len, DF_U64, ikey);

	buf[nob++] = ':';
	buf_len -= nob;

	val = umem_off2ptr(&tins->ti_umm, irec->ir_val_off);
	strncpy(buf + nob, val, min(irec->ir_val_size, buf_len));

	return buf;
}

static int
ik_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val_iov, d_iov_t *val_out)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct ik_rec		*irec;
	char			*val;

	irec = umem_off2ptr(umm, rec->rec_off);

	if (irec->ir_val_msize >= val_iov->iov_len) {
		umem_tx_add(umm, irec->ir_val_off, irec->ir_val_msize);

	} else {
		umem_tx_add(umm, rec->rec_off, sizeof(*irec));
		umem_free(umm, irec->ir_val_off);

		irec->ir_val_msize = val_iov->iov_len;
		irec->ir_val_off = umem_alloc(umm, val_iov->iov_len);
		D_ASSERT(!UMOFF_IS_NULL(irec->ir_val_off));
	}
	val = umem_off2ptr(umm, irec->ir_val_off);


	memcpy(val, val_iov->iov_buf, val_iov->iov_len);
	irec->ir_val_size = val_iov->iov_len;
	return 0;
}

static int
ik_rec_stat(struct btr_instance *tins, struct btr_record *rec,
		struct btr_rec_stat *stat)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct ik_rec		*irec;

	irec = umem_off2ptr(umm, rec->rec_off);

	stat->rs_ksize = sizeof(irec->ir_key);
	stat->rs_vsize = irec->ir_val_size;
	return 0;
}

static btr_ops_t ik_ops = {
	.to_hkey_size	= ik_hkey_size,
	.to_hkey_gen	= ik_hkey_gen,
	.to_rec_alloc	= ik_rec_alloc,
	.to_rec_free	= ik_rec_free,
	.to_rec_fetch	= ik_rec_fetch,
	.to_rec_update	= ik_rec_update,
	.to_rec_string	= ik_rec_string,
	.to_rec_stat	= ik_rec_stat,
};

#define IK_SEP		','
#define IK_SEP_VAL	':'

static void
ik_btr_open_create(void **state)
{
	bool		inplace = false;
	uint64_t	feats = 0;
	int		rc;
	bool	create;
	char	*arg;
	char	outbuf[64];

	create = tst_fn_val.input;
	arg = tst_fn_val.optval;

	if (daos_handle_is_valid(ik_toh)) {
		fail_msg("Tree has been opened\n");
	}

	if (create && arg != NULL) {
		if (arg[0] == '+') {
			feats = BTR_FEAT_UINT_KEY;
			arg += 1;
		}
		if (arg[0] == 'i') { /* inplace create/open */
			inplace = true;
			if (arg[1] != IK_SEP) {
				sprintf(outbuf, "wrong parameter format %s\n",
						arg);
				fail_msg("%s", outbuf);
			}
			arg += 2;
		}

		if (arg[0] != 'o' || arg[1] != IK_SEP_VAL) {
			sprintf(outbuf, "incorrect format for tree order: %s\n",
					arg);
			fail_msg("%s", outbuf);
		}

		ik_order = atoi(&arg[2]);
		if (ik_order < BTR_ORDER_MIN || ik_order > BTR_ORDER_MAX) {
			sprintf(outbuf, "Invalid tree order %d\n", ik_order);
			fail_msg("%s", outbuf);
		}
	} else if (!create) {
		inplace = (ik_root->tr_class != 0);
		if (UMOFF_IS_NULL(ik_root_off) && !inplace)
			fail_msg("Please create tree first\n");
	}

	if (create) {
		D_PRINT("Create btree with order %d%s feats "DF_X64"\n",
			ik_order, inplace ? " inplace" : "", feats);
		if (inplace) {
			rc = dbtree_create_inplace(IK_TREE_CLASS, feats,
						   ik_order, ik_uma, ik_root,
						   &ik_toh);
		} else {
			rc = dbtree_create(IK_TREE_CLASS, feats, ik_order,
					   ik_uma, &ik_root_off, &ik_toh);
		}
	} else {
		D_PRINT("Open btree%s\n", inplace ? " inplace" : "");
		if (inplace) {
			rc = dbtree_open_inplace(ik_root,
						 ik_uma,
						 &ik_toh);
		} else {
			rc = dbtree_open(ik_root_off, ik_uma, &ik_toh);
		}
	}
	if (rc != 0) {
		sprintf(outbuf, "Tree %s failed: %d\n",
				create ? "create" : "open", rc);
		fail_msg("%s", outbuf);
	}
}

static void
ik_btr_close_destroy(void **state)
{
	int		rc;
	bool	destroy;
	char	outbuf[64];

	destroy = tst_fn_val.input;

	if (daos_handle_is_inval(ik_toh)) {
		fail_msg("Invalid tree open handle\n");
	}

	if (destroy) {
		D_PRINT("Destroy btree\n");
		rc = dbtree_destroy(ik_toh, NULL);
	} else {
		D_PRINT("Close btree\n");
		rc = dbtree_close(ik_toh);
	}

	ik_toh = DAOS_HDL_INVAL;
	if (rc != 0) {
		sprintf(outbuf, "Tree %s failed: %d\n",
			destroy ? "destroy" : "close", rc);
		fail_msg("%s", outbuf);
	}
}

static int
btr_rec_verify_delete(umem_off_t *rec, d_iov_t *key)
{
	struct umem_instance	*umm;
	struct ik_rec		*irec;

	umm = utest_utx2umm(ik_utx);

	irec	  = umem_off2ptr(umm, *rec);

	if ((sizeof(irec->ir_key) != key->iov_len) ||
		(irec->ir_key != *((uint64_t *)key->iov_buf))) {
		D_ERROR("Preserved record mismatch while delete\n");
		return -1;
	}

	utest_free(ik_utx, irec->ir_val_off);
	utest_free(ik_utx, *rec);

	return 0;
}

static char *
btr_opc2str(void)
{
	enum ik_btr_opc opc;

	opc = tst_fn_val.opc;
	switch (opc) {
	default:
		return "unknown";
	case BTR_OPC_UPDATE:
		return "update";
	case BTR_OPC_LOOKUP:
		return "lookup";
	case BTR_OPC_DELETE:
		return "delete";
	case BTR_OPC_DELETE_RETAIN:
		return "delete and retain";
	}
}

static void
ik_btr_kv_operate(void **state)
{
	int					count = 0;
	umem_off_t				rec_off;
	int					rc;
	enum	ik_btr_opc	opc;
	char				*str;
	bool				verbose;
	char				outbuf[64];

	opc = tst_fn_val.opc;
	str = tst_fn_val.optval;
	verbose = tst_fn_val.input;

	if (daos_handle_is_inval(ik_toh)) {
		fail_msg("Can't find opened tree\n");
	}

	while (str != NULL && !isspace(*str) && *str != '\0') {
		char	   *val = NULL;
		d_iov_t	key_iov;
		d_iov_t	val_iov;
		uint64_t	key;

		key = strtoul(str, NULL, 0);

		if (opc == BTR_OPC_UPDATE) {
			val = strchr(str, IK_SEP_VAL);
			if (val == NULL) {
				sprintf(outbuf,
				"Invalid parameters %s(errno %d)\n",
					str, errno);
				fail_msg("%s", outbuf);
			}
			str = ++val;
		}

		str = strchr(str, IK_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}

		d_iov_set(&key_iov, &key, sizeof(key));
		switch (opc) {
		default:
			fail_msg("Invalid opcode\n");
			break;
		case BTR_OPC_UPDATE:
			d_iov_set(&val_iov, val, strlen(val) + 1);
			rc = dbtree_update(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				sprintf(outbuf,
				"Failed to update "DF_U64":%s\n", key, val);
				fail_msg("%s", outbuf);
			}
			break;

		case BTR_OPC_DELETE:
			rc = dbtree_delete(ik_toh, BTR_PROBE_EQ,
					   &key_iov, NULL);
			if (rc != 0) {
				sprintf(outbuf,
					"Failed to delete "DF_U64"\n", key);
				fail_msg("%s", outbuf);
			}
			if (verbose)
				D_PRINT("Deleted key "DF_U64"\n", key);

			if (dbtree_is_empty(ik_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_DELETE_RETAIN:
			rc = dbtree_delete(ik_toh, BTR_PROBE_EQ,
					   &key_iov, &rec_off);
			if (rc != 0) {
				sprintf(outbuf,
					"Failed to delete "DF_U64"\n", key);
				fail_msg("%s", outbuf);
			}

			/** Verify and delete rec_off here */
			rc = btr_rec_verify_delete(&rec_off, &key_iov);
			if (rc != 0) {
				fail_msg("Failed to verify and delete rec\n");
			}

			if (verbose)
				D_PRINT("Deleted key "DF_U64"\n", key);
			if (dbtree_is_empty(ik_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_LOOKUP:
			D_DEBUG(DB_TEST, "Looking for "DF_U64"\n", key);

			d_iov_set(&val_iov, NULL, 0); /* get address */
			rc = dbtree_lookup(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				sprintf(outbuf,
					"Failed to lookup "DF_U64"\n", key);
				fail_msg("%s", outbuf);
			}

			if (verbose) {
				D_PRINT("Found key "DF_U64", value %s\n",
					key, (char *)val_iov.iov_buf);
			}
			break;
		}
		count++;
	}
	if (verbose)
		D_PRINT("%s %d record(s)\n", btr_opc2str(), count);
}

static void
ik_btr_query(void **state)
{
	struct btr_attr		attr;
	struct btr_stat		stat;
	int			rc;
	char		outbuf[64];

	rc = dbtree_query(ik_toh, &attr, &stat);
	if (rc != 0) {
		sprintf(outbuf, "Failed to query btree: %d\n", rc);
		fail_msg("%s", outbuf);
	}

	D_PRINT("tree	[order=%d, depth=%d]\n", attr.ba_order, attr.ba_depth);
	D_PRINT("node	[total="DF_U64"]\n"
		"record [total="DF_U64"]\n"
		"key	[total="DF_U64", max="DF_U64"]\n"
		"val	[total="DF_U64", max="DF_U64"]\n",
		stat.bs_node_nr, stat.bs_rec_nr,
		stat.bs_key_sum, stat.bs_key_max,
		stat.bs_val_sum, stat.bs_val_max);

}

static void
ik_btr_iterate(void **state)
{
	daos_handle_t	ih;
	int		i;
	int		d;
	int		del;
	int		rc;
	int		opc;
	char		*err;
	char		*arg;

	arg = tst_fn_val.optval;

	if (daos_handle_is_inval(ik_toh)) {
		fail_msg("Can't find opened tree\n");
	}

	rc = dbtree_iter_prepare(ik_toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		err = "Failed to initialize tree\n";
		goto failed;
	}

	if (arg[0] == 'b')
		opc = BTR_PROBE_LAST;
	else
		opc = BTR_PROBE_FIRST;

	if (arg[1] == ':')
		del = atoi(&arg[2]);
	else
		del = 0;

	for (i = d = 0;; i++) {
		d_iov_t	key_iov;
		d_iov_t	val_iov;
		uint64_t	key;

		if (i == 0 || (del != 0 && d <= del)) {
			rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT,
						   NULL, NULL);
			if (rc == -DER_NONEXIST)
				break;

			if (rc != 0) {
				err = "probe failure\n";
				goto failed;
			}

			if (del != 0) {
				if (d == del)
					del = d = 0; /* done */
				else
					d++;
			}
		}

		d_iov_set(&key_iov, NULL, 0);
		d_iov_set(&val_iov, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, NULL);
		if (rc != 0) {
			err = "fetch failure\n";
			goto failed;
		}

		D_ASSERT(key_iov.iov_len == sizeof(key));
		memcpy(&key, key_iov.iov_buf, sizeof(key));

		if (d != 0) { /* delete */
			D_PRINT("Delete "DF_U64": %s\n",
				key, (char *)val_iov.iov_buf);
			rc = dbtree_iter_delete(ih, NULL);
			if (rc != 0) {
				err = "delete failure\n";
				goto failed;
			}

		} else { /* iterate */
			D_PRINT(DF_U64": %s\n", key, (char *)val_iov.iov_buf);

			if (opc == BTR_PROBE_LAST)
				rc = dbtree_iter_prev(ih);
			else
				rc = dbtree_iter_next(ih);

			if (rc == -DER_NONEXIST)
				break;

			if (rc != 0) {
				err = "move failure\n";
				goto failed;
			}
		}
	}

	D_PRINT("%s iterator: total %d, deleted %d\n",
		opc == BTR_PROBE_FIRST ? "forward" : "backward", i, d);
	dbtree_iter_finish(ih);
	goto pass;

failed:
	dbtree_iter_finish(ih);
	fail_msg("%s", err);

pass:
	print_message("Test Passed\n");
}

/* fill in @arr with natural number from 1 to key_nr, randomize their order */
void
ik_btr_gen_keys(unsigned int *arr, unsigned int key_nr)
{
	int		nr;
	int		i;

	for (i = 0; i < key_nr; i++)
		arr[i] = i + 1;

	for (nr = key_nr; nr > 0; nr--) {
		unsigned int	tmp;
		int		j;

		j = rand() % nr;
		if (j != nr - 1) {
			tmp = arr[j];
			arr[j] = arr[nr - 1];
			arr[nr - 1] = tmp;
		}
	}
}

#define DEL_BATCH	10000
/**
 * batch btree operations:
 * 1) insert @key_nr number of integer keys
 * 2) lookup all the rest keys
 * 3) delete nr=DEL_BATCH keys
 * 4) repeat 2) and 3) util all keys are deleted
 */
static void
ik_btr_batch_oper(void **state)
{
	unsigned int	*arr;
	char		 buf[64];
	int		 i;
	unsigned int	key_nr;
	bool		 verbose;

	key_nr = atoi(tst_fn_val.optval);
	verbose = key_nr < 20;

	if (key_nr == 0 || key_nr > (1U << 28)) {
		D_PRINT("Invalid key number: %d\n", key_nr);
		fail();
	}

	D_ALLOC_ARRAY(arr, key_nr);
	if (arr == NULL) {
		fail_msg("Array allocation failed");
		return;
	}

	D_PRINT("Batch add %d records.\n", key_nr);
	ik_btr_gen_keys(arr, key_nr);
	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d:%d", arr[i], arr[i]);
		tst_fn_val.opc = BTR_OPC_UPDATE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = verbose;
		ik_btr_kv_operate(NULL);
	}

	ik_btr_query(NULL);
	/* lookup all rest records, delete 10000 of them, and repeat until
	 * deleting all records.
	 */
	ik_btr_gen_keys(arr, key_nr);
	for (i = 0; i < key_nr;) {
		int	j;

		D_PRINT("Batch lookup %d records.\n", key_nr - i);
		for (j = i; j < key_nr; j++) {
			sprintf(buf, "%d", arr[j]);
			tst_fn_val.opc = BTR_OPC_LOOKUP;
			tst_fn_val.optval = buf;
			tst_fn_val.input = verbose;
			ik_btr_kv_operate(NULL);
		}

		D_PRINT("Batch delete %d records.\n",
			min(key_nr - i, DEL_BATCH));

		for (j = 0; i < key_nr && j < DEL_BATCH; i++, j++) {
			sprintf(buf, "%d", arr[i]);
			tst_fn_val.opc = BTR_OPC_DELETE;
			tst_fn_val.optval = buf;
			tst_fn_val.input = verbose;
			ik_btr_kv_operate(NULL);
		}
	}
	ik_btr_query(NULL);
	D_FREE(arr);
}

static void
ik_btr_perf(void **state)
{
	unsigned int	*arr;
	char		 buf[64];
	int		 i;
	double		 then;
	double		 now;
	unsigned int	key_nr;

	key_nr = atoi(tst_fn_val.optval);

	if (key_nr == 0 || key_nr > (1U << 28)) {
		D_PRINT("Invalid key number: %d\n", key_nr);
		fail();
	}

	D_PRINT("Btree performance test, order=%u, keys=%u\n",
		ik_order, key_nr);

	D_ALLOC_ARRAY(arr, key_nr);
	if (arr == NULL)
		fail_msg("Array allocation failed\n");

	/* step-1: Insert performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d:%d", arr[i], arr[i]);
		tst_fn_val.opc = BTR_OPC_UPDATE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		ik_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("insert = %10.2f/sec\n", key_nr / (now - then));

	/* step-2: lookup performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d", arr[i]);
		tst_fn_val.opc = BTR_OPC_LOOKUP;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		ik_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("lookup = %10.2f/sec\n", key_nr / (now - then));

	/* step-3: delete performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d", arr[i]);
		tst_fn_val.opc = BTR_OPC_DELETE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		ik_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("delete = %10.2f/sec\n", key_nr / (now - then));
	D_FREE(arr);
}


static void
ik_btr_drain(void **state)
{
	static const int drain_keys  = 10000;
	static const int drain_creds = 23;

	unsigned int	*arr;
	unsigned int	 drained = 0;
	char		 buf[64];
	int		 i;

	D_ALLOC_ARRAY(arr, drain_keys);
	if (arr == NULL) {
		fail_msg("Array allocation failed");
		return;
	}

	D_PRINT("Batch add %d records.\n", drain_keys);
	ik_btr_gen_keys(arr, drain_keys);
	for (i = 0; i < drain_keys; i++) {
		sprintf(buf, "%d:%d", arr[i], arr[i]);
		tst_fn_val.opc	  = BTR_OPC_UPDATE;
		tst_fn_val.optval = buf;
		tst_fn_val.input  = false;

		ik_btr_kv_operate(NULL);
	}

	ik_btr_query(NULL);
	while (1) {
		int	creds = drain_creds;
		bool	empty = false;
		int	rc;

		rc = dbtree_drain(ik_toh, &creds, NULL, &empty);
		if (rc) {
			fail_msg("Failed to drain btree: %s\n", d_errstr(rc));
			fail();
		}
		drained += drain_creds - creds;
		D_PRINT("Drained %d of %d KVs, empty=%d\n",
			drained, drain_keys, empty);
		if (empty)
			break;
	}

	D_FREE(arr);
}

static struct option btr_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "drain",	no_argument,		NULL,	'e'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "update",	required_argument,	NULL,	'u'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "dyn_tree",	no_argument,		NULL,	't'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "del_retain", required_argument,	NULL,	'r'	},
	{ "query",	no_argument,		NULL,	'q'	},
	{ "iterate",	required_argument,	NULL,	'i'	},
	{ "batch",	required_argument,	NULL,	'b'	},
	{ "perf",	required_argument,	NULL,	'p'	},
	{ NULL,		0,			NULL,	0	},
};

static int
use_pmem() {

	int rc;

	D_PRINT("Using pmem\n");
	rc = utest_pmem_create(POOL_NAME, POOL_SIZE,
			       sizeof(*ik_root), NULL,
			       &ik_utx);
	D_ASSERT(rc == 0);
	return rc;
}

static void
ts_group(void **state) {

	int	opt = 0;
	void	**st = NULL;

	while ((opt = getopt_long(test_group_stop-test_group_start+1,
				  test_group_args+test_group_start,
				  "tmC:Deocqu:d:r:f:i:b:p:",
				  btr_ops,
				  NULL)) != -1) {
		tst_fn_val.optval = optarg;
		tst_fn_val.input = true;

		switch (opt) {
		case 'C':
			ik_btr_open_create(st);
			break;
		case 'D':
			tst_fn_val.input = true;
			ik_btr_close_destroy(st);
			break;
		case 'o':
			tst_fn_val.input = false;
			tst_fn_val.optval = NULL;
			ik_btr_open_create(st);
			break;
		case 'c':
			tst_fn_val.input = false;
			ik_btr_close_destroy(st);
			break;
		case 'e':
			ik_btr_drain(st);
			break;
		case 'q':
			ik_btr_query(st);
			break;
		case 'u':
			tst_fn_val.opc = BTR_OPC_UPDATE;
			ik_btr_kv_operate(st);
			break;
		case 'f':
			tst_fn_val.opc = BTR_OPC_LOOKUP;
			ik_btr_kv_operate(st);
			break;
		case 'd':
			tst_fn_val.opc = BTR_OPC_DELETE;
			ik_btr_kv_operate(st);
			break;
		case 'r':
			tst_fn_val.opc = BTR_OPC_DELETE_RETAIN;
			ik_btr_kv_operate(st);
			break;
		case 'i':
			ik_btr_iterate(st);
			break;
		case 'b':
			ik_btr_batch_oper(st);
			break;
		case 'p':
			ik_btr_perf(st);
			break;
		default:
			D_PRINT("Unsupported command %c\n", opt);
		case 'm':
		case 't':
			/* handled previously */
			break;
		}
	}
}

static int
run_cmd_line_test(char *test_name, char **args, int start_idx, int stop_idx)
{

	const struct CMUnitTest btree_test[] = {
		{test_name, ts_group, NULL, NULL},
	};

	test_group_args = args;
	test_group_start = start_idx;
	test_group_stop = stop_idx;

	return cmocka_run_group_tests_name(test_name,
					   btree_test,
					   NULL,
					   NULL);
}

int
main(int argc, char **argv)
{
	struct timeval	tv;
	int		rc = 0;
	int		opt;
	int		dynamic_flag = 0;
	int		start_idx;
	char		*test_name;
	int		stop_idx;

	d_register_alt_assert(mock_assert);

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	ik_toh = DAOS_HDL_INVAL;
	ik_root_off = UMOFF_NULL;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	if (argc == 1) {
		print_message("Invalid format.\n");
		return -1;
	}

	stop_idx = argc-1;
	if (strcmp(argv[1], "--start-test") == 0) {
		start_idx = 2;
		test_name = argv[2];
		if (strcmp(argv[3], "-t") == 0) {
			D_PRINT("Using dynamic tree order\n");
			dynamic_flag = BTR_FEAT_DYNAMIC_ROOT;
			if (strcmp(argv[4], "-m") == 0)
				rc = use_pmem();
		} else if (strcmp(argv[3], "-m") == 0) {
			rc = use_pmem();
			if (strcmp(argv[4], "-t") == 0) {
				D_PRINT("Using dynamic tree order\n");
				dynamic_flag = BTR_FEAT_DYNAMIC_ROOT;
			}
		}
	} else {
		start_idx = 0;
		test_name = "Btree testing tool";
		optind = 0;
		/* Check for -m option first */
		while ((opt = getopt_long(argc, argv, "tmC:Deocqu:d:r:f:i:b:p:",
					  btr_ops, NULL)) != -1) {
			if (opt == 'm') {
				rc = use_pmem();
				break;
			}
			if (opt == 't') {
				D_PRINT("Using dynamic tree order\n");
				dynamic_flag = BTR_FEAT_DYNAMIC_ROOT;
			}
		}
	}

	rc = dbtree_class_register(IK_TREE_CLASS,
				   dynamic_flag | BTR_FEAT_UINT_KEY, &ik_ops);
	D_ASSERT(rc == 0);

	if (ik_utx == NULL) {
		D_PRINT("Using vmem\n");
		rc = utest_vmem_create(sizeof(*ik_root), &ik_utx);
		D_ASSERT(rc == 0);
	}

	ik_root = utest_utx2root(ik_utx);
	ik_uma = utest_utx2uma(ik_utx);

	/* start over */
	optind = 0;
	rc = run_cmd_line_test(test_name, argv, start_idx, stop_idx);
	daos_debug_fini();
	rc += utest_utx_destroy(ik_utx);
	if (rc != 0)
		printf("Error: %d\n", rc);

	return rc;
}
