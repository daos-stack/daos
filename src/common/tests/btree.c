/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#include <daos/btree.h>
#include <daos/dtx.h>
#include <daos/tests_lib.h>
#include "utest_common.h"

/**
 * An example for integer key btree.
 */

TMMID_DECLARE(struct ik_rec, 0);

#define IK_ORDER_DEF	16

static int ik_order = IK_ORDER_DEF;

struct utest_context		*ik_utx;
struct umem_attr		*ik_uma;
static TMMID(struct btr_root)	ik_root_mmid;
static struct btr_root		*ik_root;
static daos_handle_t		ik_toh;


/** integer key record */
struct ik_rec {
	uint64_t	ir_key;
	uint32_t	ir_val_size;
	uint32_t	ir_val_msize;
	umem_id_t	ir_val_mmid;
};

#define IK_TREE_CLASS	100
#define POOL_NAME "/mnt/daos/btree-test"
#define POOL_SIZE ((1024 * 1024  * 1024ULL))

/** customized functions for btree */
static int
ik_hkey_size(struct btr_instance *tins)
{
	struct ik_rec irec;
	return sizeof(irec.ir_key);
}

static void
ik_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	uint64_t	*ikey;

	ikey = (uint64_t *)key_iov->iov_buf;
	/* ikey = dummy_hash(ikey); */
	memcpy(hkey, ikey, sizeof(*ikey));
}

static int
ik_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct ik_rec)   irec_mmid;
	struct ik_rec	      *irec;
	char		      *vbuf;

	irec_mmid = umem_znew_typed(&tins->ti_umm, struct ik_rec);
	D_ASSERT(!TMMID_IS_NULL(irec_mmid)); /* lazy bone... */

	irec = umem_id2ptr_typed(&tins->ti_umm, irec_mmid);

	irec->ir_key = *(int *)key_iov->iov_buf;
	irec->ir_val_size = irec->ir_val_msize = val_iov->iov_len;

	irec->ir_val_mmid = umem_alloc(&tins->ti_umm, val_iov->iov_len);
	D_ASSERT(!UMMID_IS_NULL(irec->ir_val_mmid));

	vbuf = umem_id2ptr(&tins->ti_umm, irec->ir_val_mmid);
	memcpy(vbuf, (char *)val_iov->iov_buf, val_iov->iov_len);

	rec->rec_mmid = umem_id_t2u(irec_mmid);
	return 0;
}

static int
ik_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance *umm = &tins->ti_umm;
	TMMID(struct ik_rec) irec_mmid;
	struct ik_rec *irec;

	irec_mmid = umem_id_u2t(rec->rec_mmid, struct ik_rec);
	irec = umem_id2ptr_typed(umm, irec_mmid);

	if (args != NULL) {
		umem_id_t *rec_ret = (umem_id_t *) args;
		 /** Provide the buffer to user */
		*rec_ret	= rec->rec_mmid;
		return 0;
	}
	utest_free(ik_utx, irec->ir_val_mmid);
	utest_free_typed(ik_utx, irec_mmid);

	return 0;
}

static int
ik_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct ik_rec	*irec;
	char		*val;
	int		 val_size;
	int		 key_size;

	if (key_iov == NULL && val_iov == NULL)
		return -EINVAL;

	irec = (struct ik_rec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	val_size = irec->ir_val_size;
	key_size = sizeof(irec->ir_key);

	val = umem_id2ptr(&tins->ti_umm, irec->ir_val_mmid);
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
			memcpy(key_iov->iov_buf, val, val_size);

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

	irec = (struct ik_rec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	ikey = irec->ir_key;
	nob = snprintf(buf, buf_len, DF_U64, ikey);

	buf[nob++] = ':';
	buf_len -= nob;

	val = umem_id2ptr(&tins->ti_umm, irec->ir_val_mmid);
	strncpy(buf + nob, val, min(irec->ir_val_size, buf_len));

	return buf;
}

static int
ik_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key, daos_iov_t *val_iov)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct ik_rec		*irec;
	char			*val;
	TMMID(struct ik_rec)	 irec_mmid;

	irec_mmid = umem_id_u2t(rec->rec_mmid, struct ik_rec);
	irec = umem_id2ptr_typed(umm, irec_mmid);

	if (irec->ir_val_msize >= val_iov->iov_len) {
		umem_tx_add(umm, irec->ir_val_mmid, irec->ir_val_msize);

	} else {
		umem_tx_add_mmid_typed(umm, irec_mmid);
		umem_free(umm, irec->ir_val_mmid);

		irec->ir_val_msize = val_iov->iov_len;
		irec->ir_val_mmid = umem_alloc(umm, val_iov->iov_len);
		D_ASSERT(!UMMID_IS_NULL(irec->ir_val_mmid));
	}
	val = umem_id2ptr(umm, irec->ir_val_mmid);

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
	TMMID(struct ik_rec)	 irec_mmid;

	irec_mmid = umem_id_u2t(rec->rec_mmid, struct ik_rec);
	irec = umem_id2ptr_typed(umm, irec_mmid);

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

static int
ik_btr_open_create(bool create, char *args)
{
	bool		inplace = false;
	uint64_t	feats = 0;
	int		rc;

	if (!daos_handle_is_inval(ik_toh)) {
		D_ERROR("Tree has been opened\n");
		return -1;
	}

	if (create && args != NULL) {
		if (args[0] == '+') {
			feats = BTR_FEAT_UINT_KEY;
			args += 1;
		}
		if (args[0] == 'i') { /* inplace create/open */
			inplace = true;
			if (args[1] != IK_SEP) {
				D_ERROR("wrong parameter format %s\n", args);
				return -1;
			}
			args += 2;
		}

		if (args[0] != 'o' || args[1] != IK_SEP_VAL) {
			D_ERROR("incorrect format for tree order: %s\n", args);
			return -1;
		}

		ik_order = atoi(&args[2]);
		if (ik_order < BTR_ORDER_MIN || ik_order > BTR_ORDER_MAX) {
			D_ERROR("Invalid tree order %d\n", ik_order);
			return -1;
		}
	} else if (!create) {
		inplace = (ik_root->tr_class != 0);
		if (TMMID_IS_NULL(ik_root_mmid) && !inplace) {
			D_ERROR("Please create tree first\n");
			return -1;
		}
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
					   ik_uma, &ik_root_mmid, &ik_toh);
		}
	} else {
		D_PRINT("Open btree%s\n", inplace ? " inplace" : "");
		if (inplace) {
			rc = dbtree_open_inplace(ik_root,
						 ik_uma,
						 &ik_toh);
		} else {
			rc = dbtree_open(ik_root_mmid, ik_uma, &ik_toh);
		}
	}
	if (rc != 0) {
		D_ERROR("Tree %s failed: %d\n", create ? "create" : "open", rc);
		return -1;
	}
	return 0;
}

static int
ik_btr_close_destroy(bool destroy)
{
	int rc;

	if (daos_handle_is_inval(ik_toh)) {
		D_ERROR("Invalid tree open handle\n");
		return -1;
	}

	if (destroy) {
		D_PRINT("Destroy btree\n");
		rc = dbtree_destroy(ik_toh);
	} else {
		D_PRINT("Close btree\n");
		rc = dbtree_close(ik_toh);
	}

	ik_toh = DAOS_HDL_INVAL;
	if (rc != 0) {
		D_ERROR("Tree %s failed: %d\n",
			destroy ? "destroy" : "close", rc);
		return -1;
	}
	return rc;
}

static int
btr_rec_verify_delete(umem_id_t *rec, daos_iov_t *key)
{
	TMMID(struct ik_rec)	irec_mmid;
	struct umem_instance	*umm;
	struct ik_rec		*irec;

	umm = utest_utx2umm(ik_utx);

	irec_mmid = umem_id_u2t(*rec, struct ik_rec);
	irec	  = umem_id2ptr_typed(umm, irec_mmid);

	if ((sizeof(irec->ir_key) != key->iov_len) ||
	    (irec->ir_key != *((uint64_t *)key->iov_buf))) {
		D_ERROR("Preserved record mismatch while delete\n");
		return -1;
	}

	utest_free(ik_utx, irec->ir_val_mmid);
	utest_free_typed(ik_utx, irec_mmid);

	return 0;
}

enum ik_btr_opc {
	BTR_OPC_UPDATE,
	BTR_OPC_LOOKUP,
	BTR_OPC_DELETE,
	BTR_OPC_DELETE_RETAIN,
};

static char *
btr_opc2str(enum ik_btr_opc opc)
{
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

static int
ik_btr_kv_operate(enum ik_btr_opc opc, char *str, bool verbose)
{
	int		count = 0;
	umem_id_t	rec_mmid;
	int		rc;

	if (daos_handle_is_inval(ik_toh)) {
		D_ERROR("Can't find opened tree\n");
		return -1;
	}

	while (str != NULL && !isspace(*str) && *str != '\0') {
		char	   *val = NULL;
		daos_iov_t  key_iov;
		daos_iov_t  val_iov;
		uint64_t    key;

		key = strtoul(str, NULL, 0);

		if (opc == BTR_OPC_UPDATE) {
			val = strchr(str, IK_SEP_VAL);
			if (val == NULL) {
				D_ERROR("Invalid parameters %s(errno %d)\n",
					str, errno);
				return -1;
			}
			str = ++val;
		}

		str = strchr(str, IK_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}

		daos_iov_set(&key_iov, &key, sizeof(key));
		switch (opc) {
		default:
			return -1;
		case BTR_OPC_UPDATE:
			daos_iov_set(&val_iov, val, strlen(val) + 1);
			rc = dbtree_update(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				D_ERROR("Failed to update "DF_U64":%s\n",
					key, val);
				return -1;
			}
			break;

		case BTR_OPC_DELETE:
			rc = dbtree_delete(ik_toh, &key_iov, NULL);
			if (rc != 0) {
				D_ERROR("Failed to delete "DF_U64"\n", key);
				return -1;
			}
			if (verbose)
				D_PRINT("Deleted key "DF_U64"\n", key);

			if (dbtree_is_empty(ik_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_DELETE_RETAIN:
			rc = dbtree_delete(ik_toh, &key_iov, &rec_mmid);
			if (rc != 0) {
				D_ERROR("Failed to delete "DF_U64"\n", key);
				return -1;
			}

			/** Verify and delete rec_mmid here */
			rc = btr_rec_verify_delete(&rec_mmid, &key_iov);
			if (rc != 0) {
				D_ERROR("Failed to verify and delete rec\n");
				return -1;
			}

			if (verbose)
				D_PRINT("Deleted key "DF_U64"\n", key);
			if (dbtree_is_empty(ik_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_LOOKUP:
			D_DEBUG(DB_TEST, "Looking for "DF_U64"\n", key);

			daos_iov_set(&val_iov, NULL, 0); /* get address */
			rc = dbtree_lookup(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				D_ERROR("Failed to lookup "DF_U64"\n", key);
				return -1;
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
		D_PRINT("%s %d record(s)\n", btr_opc2str(opc), count);
	return 0;
}

static int
ik_btr_query(void)
{
	struct btr_attr		attr;
	struct btr_stat		stat;
	int			rc;

	rc = dbtree_query(ik_toh, &attr, &stat);
	if (rc != 0) {
		D_ERROR("Failed to query btree: %d\n", rc);
		return -1;
	}

	D_PRINT("tree   [order=%d, depth=%d]\n", attr.ba_order, attr.ba_depth);
	D_PRINT("node   [total="DF_U64"]\n"
		"record [total="DF_U64"]\n"
		"key    [total="DF_U64", max="DF_U64"]\n"
		"val    [total="DF_U64", max="DF_U64"]\n",
		stat.bs_node_nr, stat.bs_rec_nr,
		stat.bs_key_sum, stat.bs_key_max,
		stat.bs_val_sum, stat.bs_val_max);

	return 0;
}

static int
ik_btr_iterate(char *args)
{
	daos_handle_t	ih;
	int		i;
	int		d;
	int		del;
	int		rc;
	int		opc;
	char		*err;

	if (daos_handle_is_inval(ik_toh)) {
		D_ERROR("Can't find opened tree\n");
		return -1;
	}

	rc = dbtree_iter_prepare(ik_toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		err = "initialize";
		goto failed;
	}

	if (args[0] == 'b')
		opc = BTR_PROBE_LAST;
	else
		opc = BTR_PROBE_FIRST;

	if (strlen(args) >= 3 && args[1] == ':')
		del = atoi(&args[2]);
	else
		del = 0;

	for (i = d = 0;; i++) {
		daos_iov_t	key_iov;
		daos_iov_t	val_iov;
		uint64_t	key;

		if (i == 0 || (del != 0 && d <= del)) {
			rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT,
					       NULL, NULL);
			if (rc == -DER_NONEXIST)
				break;

			if (rc != 0) {
				err = "probe";
				goto failed;
			}

			if (del != 0) {
				if (d == del)
					del = d = 0; /* done */
				else
					d++;
			}
		}

		daos_iov_set(&key_iov, NULL, 0);
		daos_iov_set(&val_iov, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, NULL);
		if (rc != 0) {
			err = "fetch";
			goto failed;
		}

		D_ASSERT(key_iov.iov_len == sizeof(key));
		memcpy(&key, key_iov.iov_buf, sizeof(key));

		if (d != 0) { /* delete */
			D_PRINT("Delete "DF_U64": %s\n",
				key, (char *)val_iov.iov_buf);
			rc = dbtree_iter_delete(ih, NULL);
			if (rc != 0) {
				err = "delete";
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
				err = "move";
				goto failed;
			}
		}
	}

	D_PRINT("%s iterator: total %d, deleted %d\n",
		opc == BTR_PROBE_FIRST ? "forward" : "backward", i, d);
	dbtree_iter_finish(ih);
	return 0;
 failed:
	D_PRINT("Iterator %s failed: %d\n", err, rc);
	dbtree_iter_finish(ih);
	return -1;
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
static int
ik_btr_batch_oper(unsigned int key_nr)
{
	unsigned int	*arr;
	char		 buf[64];
	int		 i;
	int		 rc;
	bool		 verbose = key_nr < 20;

	if (key_nr == 0 || key_nr > (1U << 28)) {
		D_PRINT("Invalid key number: %d\n", key_nr);
		return -1;
	}

	D_ALLOC_ARRAY(arr, key_nr);
	D_ASSERT(arr != NULL);

	D_PRINT("Batch add %d records.\n", key_nr);
	ik_btr_gen_keys(arr, key_nr);
	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d:%d", arr[i], arr[i]);

		rc = ik_btr_kv_operate(BTR_OPC_UPDATE, buf, verbose);
		if (rc != 0) {
			D_PRINT("Batch update failed: %d\n", rc);
			return -1;
		}
	}

	ik_btr_query();
	/* lookup all rest records, delete 10000 of them, and repeat until
	 * deleting all records.
	 */
	ik_btr_gen_keys(arr, key_nr);
	for (i = 0; i < key_nr;) {
		int	j;

		D_PRINT("Batch lookup %d records.\n", key_nr - i);
		for (j = i; j < key_nr; j++) {
			sprintf(buf, "%d", arr[j]);

			rc = ik_btr_kv_operate(BTR_OPC_LOOKUP, buf, verbose);
			if (rc != 0) {
				D_PRINT("Batch lookup failed: %d\n", rc);
				return -1;
			}
		}

		D_PRINT("Batch delete %d records.\n",
			min(key_nr - i, DEL_BATCH));

		for (j = 0; i < key_nr && j < DEL_BATCH; i++, j++) {
			sprintf(buf, "%d", arr[i]);

			rc = ik_btr_kv_operate(BTR_OPC_DELETE, buf, verbose);
			if (rc != 0) {
				D_PRINT("Batch delete failed: %d\n", rc);
				return -1;
			}
		}
	}
	ik_btr_query();
	return 0;
}

static int
ik_btr_perf(unsigned int key_nr)
{
	unsigned int	*arr;
	char		 buf[64];
	int		 i;
	int		 rc = 0;
	double		 then;
	double		 now;

	if (key_nr == 0 || key_nr > (1U << 28)) {
		D_PRINT("Invalid key number: %d\n", key_nr);
		return -1;
	}

	D_PRINT("Btree performance test, order=%u, keys=%u\n",
		ik_order, key_nr);

	D_ALLOC_ARRAY(arr, key_nr);
	D_ASSERT(arr != NULL);

	/* step-1: Insert performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d:%d", arr[i], arr[i]);

		rc = ik_btr_kv_operate(BTR_OPC_UPDATE, buf, false);
		if (rc != 0) {
			D_PRINT("update failed: %d\n", rc);
			D_GOTO(out, rc = -1);
		}
	}
	now = dts_time_now();
	D_PRINT("insert = %10.2f/sec\n", key_nr / (now - then));

	/* step-2: lookup performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d", arr[i]);

		rc = ik_btr_kv_operate(BTR_OPC_LOOKUP, buf, false);
		if (rc != 0) {
			D_PRINT("lookup failed: %d\n", rc);
			D_GOTO(out, rc = -1);
		}
	}
	now = dts_time_now();
	D_PRINT("lookup = %10.2f/sec\n", key_nr / (now - then));

	/* step-3: delete performance */
	ik_btr_gen_keys(arr, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		sprintf(buf, "%d", arr[i]);

		rc = ik_btr_kv_operate(BTR_OPC_DELETE, buf, false);
		if (rc != 0) {
			D_PRINT("delete failed: %d\n", rc);
			D_GOTO(out, rc = -1);
		}
	}
	now = dts_time_now();
	D_PRINT("delete = %10.2f/sec\n", key_nr / (now - then));

out:
	D_FREE(arr);
	return rc;
}

static struct option btr_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "update",	required_argument,	NULL,	'u'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "del_retain", required_argument,	NULL,	'r'	},
	{ "query",	no_argument,		NULL,	'q'	},
	{ "iterate",	required_argument,	NULL,	'i'	},
	{ "batch",	required_argument,	NULL,	'b'	},
	{ "perf",	required_argument,	NULL,	'p'	},
	{ NULL,		0,			NULL,	0	},
};

int
main(int argc, char **argv)
{
	struct timeval	tv;
	int		rc = 0;
	int		opt;

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	ik_toh = DAOS_HDL_INVAL;
	ik_root_mmid = TMMID_NULL(struct btr_root);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = dbtree_class_register(IK_TREE_CLASS, BTR_FEAT_UINT_KEY, &ik_ops);
	D_ASSERT(rc == 0);

	optind = 0;

	/* Check for -m option first */
	while ((opt = getopt_long(argc, argv, "mC:Docqu:d:r:f:i:b:p:", btr_ops,
				  NULL)) != -1) {
		if (opt == 'm') {
			D_PRINT("Using pmem\n");
			rc = utest_pmem_create(POOL_NAME, POOL_SIZE,
					       sizeof(*ik_root), &ik_utx);
			D_ASSERT(rc == 0);
			break;
		}
	}

	if (ik_utx == NULL) {
		D_PRINT("Using vmem\n");
		rc = utest_vmem_create(sizeof(*ik_root), &ik_utx);
		D_ASSERT(rc == 0);
	}

	ik_root = utest_utx2root(ik_utx);
	ik_uma = utest_utx2uma(ik_utx);

	/* start over */
	optind = 0;

	while ((opt = getopt_long(argc, argv, "mC:Docqu:d:r:f:i:b:p:", btr_ops,
				  NULL)) != -1) {
		switch (opt) {
		case 'C':
			rc = ik_btr_open_create(true, optarg);
			break;
		case 'D':
			rc = ik_btr_close_destroy(true);
			break;
		case 'o':
			rc = ik_btr_open_create(false, NULL);
			break;
		case 'c':
			rc = ik_btr_close_destroy(false);
			break;
		case 'q':
			rc = ik_btr_query();
			break;
		case 'u':
			rc = ik_btr_kv_operate(BTR_OPC_UPDATE, optarg, true);
			break;
		case 'f':
			rc = ik_btr_kv_operate(BTR_OPC_LOOKUP, optarg, true);
			break;
		case 'd':
			rc = ik_btr_kv_operate(BTR_OPC_DELETE, optarg, true);
			break;
		case 'r':
			rc = ik_btr_kv_operate(BTR_OPC_DELETE_RETAIN, optarg,
					       true);
			break;
		case 'i':
			rc = ik_btr_iterate(optarg);
			break;
		case 'b':
			rc = ik_btr_batch_oper(atoi(optarg));
			break;
		case 'p':
			rc = ik_btr_perf(atoi(optarg));
			break;
		default:
			D_PRINT("Unsupported command %c\n", opt);
		case 'm':
			/* handled previously */
			rc = 0;
			break;
		}
		if (rc != 0)
			break;
	}
	daos_debug_fini();
	rc += utest_utx_destroy(ik_utx);
	if (rc != 0)
		printf("Error: %d\n", rc);

	return rc;
}
