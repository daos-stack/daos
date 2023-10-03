/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <stdlib.h>
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

static char	**test_group_args;
static int	test_group_start;
static int	test_group_stop;

enum sk_btr_opc {
	BTR_OPC_UPDATE,
	BTR_OPC_LOOKUP,
	BTR_OPC_DELETE,
	BTR_OPC_DELETE_RETAIN,
};

struct test_input_value {
	bool					input;
	enum	 sk_btr_opc		opc;
	char					*optval;
};

struct test_input_value tst_fn_val;

/** string key record */
struct sk_rec {
	uint64_t	sr_key_len;
	uint32_t	sr_val_size;
	uint32_t	sr_val_msize;
	umem_off_t	sr_val_off;
	char		sr_key[0];
};

#define SK_TREE_CLASS	100
#define POOL_NAME "/mnt/daos/btree-direct-test"
#define POOL_SIZE ((1024 * 1024  * 1024ULL))

#define SK_ORDER_DEF	16

static int sk_order = SK_ORDER_DEF;

struct utest_context		*sk_utx;
struct umem_attr		*sk_uma;
static umem_off_t		 sk_root_off;
static struct btr_root		*sk_root;
static daos_handle_t		 sk_toh;

#define min(x, y) ((x) < (y) ? (x) : (y))

static void sk_key_encode(struct btr_instance *tins,
			  d_iov_t *key, daos_anchor_t *anchor)
{
	size_t copy_size = key->iov_len;

	if (key->iov_len > DAOS_ANCHOR_BUF_MAX)
		copy_size = DAOS_ANCHOR_BUF_MAX;

	memcpy(&anchor->da_buf[0], key->iov_buf, copy_size);
}

static void sk_key_decode(struct btr_instance *tins,
			  d_iov_t *key, daos_anchor_t *anchor)
{
	key->iov_buf = &anchor->da_buf[0];
	key->iov_buf_len = strlen((char *)anchor->da_buf) + 1;
	key->iov_len = key->iov_buf_len;
}

static int
sk_key_cmp(struct btr_instance *tins, struct btr_record *rec,
	   d_iov_t *key_iov)
{
	struct sk_rec	*srec;
	char		*s1;
	char		*s2;
	uint64_t	 len;
	int		 rc;

	srec = (struct sk_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);

	/* NB: Since strings are null terminated, this should suffice to
	 * make shorter string less than larger one
	 */
	len = min(srec->sr_key_len, key_iov->iov_len);

	s1 = &srec->sr_key[0];
	s2 = key_iov->iov_buf;
	rc = strncasecmp(s1, s2, len);

	if (rc != 0)
		return dbtree_key_cmp_rc(rc);

	return dbtree_key_cmp_rc(strncmp(s1, s2, len));
}

static int
sk_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	      d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct sk_rec		*srec;
	char			*vbuf;
	umem_off_t		 srec_off;

	srec_off = umem_zalloc(&tins->ti_umm,
				   sizeof(*srec) + key_iov->iov_len);
	D_ASSERT(!UMOFF_IS_NULL(srec_off)); /* lazy bone... */

	srec = (struct sk_rec *)umem_off2ptr(&tins->ti_umm, srec_off);

	memcpy(&srec->sr_key[0], key_iov->iov_buf, key_iov->iov_len);
	srec->sr_key_len = key_iov->iov_len;
	srec->sr_val_size = srec->sr_val_msize = val_iov->iov_len;

	srec->sr_val_off = umem_alloc(&tins->ti_umm, val_iov->iov_len);
	D_ASSERT(!UMOFF_IS_NULL(srec->sr_val_off));

	vbuf = umem_off2ptr(&tins->ti_umm, srec->sr_val_off);
	memcpy(vbuf, (char *)val_iov->iov_buf, val_iov->iov_len);

	rec->rec_off = srec_off;
	return 0;
}

static int
sk_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance *umm = &tins->ti_umm;
	struct sk_rec *srec;

	srec = umem_off2ptr(umm, rec->rec_off);

	if (args != NULL) {
		umem_off_t *rec_ret = (umem_off_t *) args;
		 /** Provide the buffer to user */
		*rec_ret	= rec->rec_off;
		return 0;
	}
	umem_free(umm, srec->sr_val_off);
	umem_free(umm, rec->rec_off);

	return 0;
}

static int
sk_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct sk_rec	*srec;
	char		*val;
	size_t		 val_size;
	size_t		 key_size;

	if (key_iov == NULL && val_iov == NULL)
		return -EINVAL;

	srec = (struct sk_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	val_size = srec->sr_val_size;
	key_size = srec->sr_key_len;

	val = umem_off2ptr(&tins->ti_umm, srec->sr_val_off);
	if (key_iov != NULL) {
		key_iov->iov_len = key_size;
		key_iov->iov_buf_len = key_size;
		if (key_iov->iov_buf == NULL)
			key_iov->iov_buf = &srec->sr_key[0];
		else if (key_iov->iov_buf_len >= key_size)
			memcpy(key_iov->iov_buf, &srec->sr_key[0], key_size);
	}

	if (val_iov != NULL) {
		val_iov->iov_len = val_size;
		val_iov->iov_buf_len = val_size;
		if (val_iov->iov_buf == NULL)
			val_iov->iov_buf = val;
		else if (val_iov->iov_buf_len >= val_size)
			memcpy(val_iov->iov_buf, val, val_size);

	}
	return 0;
}

static char *
sk_rec_string(struct btr_instance *tins, struct btr_record *rec,
	      bool leaf, char *buf, int buf_len)
{
	struct sk_rec	*srec = NULL;
	char		*val;
	char		*skey;

	if (!leaf) { /* NB: no record body on intermediate node */
		snprintf(buf, buf_len, "--");
		return buf;
	}

	srec = (struct sk_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	skey = &srec->sr_key[0];
	val = umem_off2ptr(&tins->ti_umm, srec->sr_val_off);

	snprintf(buf, buf_len, "%s:%s", skey, val);
	buf[buf_len - 1] = 0;

	return buf;
}

static int
sk_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key, d_iov_t *val_iov, d_iov_t *val_out)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct sk_rec		*srec;
	char			*val;

	srec = umem_off2ptr(umm, rec->rec_off);

	if (srec->sr_val_msize >= val_iov->iov_len) {
		umem_tx_add(umm, srec->sr_val_off, srec->sr_val_msize);

	} else {
		umem_tx_add(umm, rec->rec_off, sizeof(*srec));
		umem_free(umm, srec->sr_val_off);

		srec->sr_val_msize = val_iov->iov_len;
		srec->sr_val_off = umem_alloc(umm, val_iov->iov_len);
		D_ASSERT(!UMOFF_IS_NULL(srec->sr_val_off));
	}
	val = umem_off2ptr(umm, srec->sr_val_off);

	memcpy(val, val_iov->iov_buf, val_iov->iov_len);
	srec->sr_val_size = val_iov->iov_len;
	return 0;
}

static int
sk_rec_stat(struct btr_instance *tins, struct btr_record *rec,
	    struct btr_rec_stat *stat)
{
	struct umem_instance	*umm = &tins->ti_umm;
	struct sk_rec		*srec;

	srec = umem_off2ptr(umm, rec->rec_off);

	stat->rs_ksize = srec->sr_key_len;
	stat->rs_vsize = srec->sr_val_size;
	return 0;
}

static btr_ops_t sk_ops = {
	.to_key_cmp	= sk_key_cmp,
	.to_key_encode	= sk_key_encode,
	.to_key_decode	= sk_key_decode,
	.to_rec_alloc	= sk_rec_alloc,
	.to_rec_free	= sk_rec_free,
	.to_rec_fetch	= sk_rec_fetch,
	.to_rec_update	= sk_rec_update,
	.to_rec_string	= sk_rec_string,
	.to_rec_stat	= sk_rec_stat,
};

#define SK_SEP		','
#define SK_SEP_VAL	':'

static void
sk_btr_open_create(void **state)
{
	bool		inplace = false;
	uint64_t	feats = BTR_FEAT_DIRECT_KEY;
	int		rc;
	bool	create;
	char	*arg;
	char	outbuf[64];

	create = tst_fn_val.input;
	arg = tst_fn_val.optval;

	if (daos_handle_is_valid(sk_toh)) {
		fail_msg("Tree has been opened\n");
	}

	if (create && arg != NULL) {
		if (arg[0] == 'i') { /* inplace create/open */
			inplace = true;
			if (arg[1] != SK_SEP) {
				sprintf(outbuf,
					"wrong parameter format %s\n",	arg);
				fail_msg("%s", outbuf);
			}
			arg += 2;
		}

		if (arg[0] != 'o' || arg[1] != SK_SEP_VAL) {
			sprintf(outbuf,
				"incorrect format for tree order: %s\n", arg);
			fail_msg("%s", outbuf);
		}

		sk_order = atoi(&arg[2]);
		if (sk_order < BTR_ORDER_MIN || sk_order > BTR_ORDER_MAX) {
			sprintf(outbuf, "Invalid tree order %d\n", sk_order);
			fail_msg("%s", outbuf);
		}
	} else if (!create) {
		inplace = (sk_root->tr_class != 0);
		if (UMOFF_IS_NULL(sk_root_off) && !inplace)
			fail_msg("Please create tree first\n");
	}

	if (create) {
		D_PRINT("Create btree with order %d%s feats "DF_X64"\n",
			sk_order, inplace ? " inplace" : "", feats);
		if (inplace) {
			rc = dbtree_create_inplace(SK_TREE_CLASS, feats,
						   sk_order, sk_uma, sk_root,
						   &sk_toh);
		} else {
			rc = dbtree_create(SK_TREE_CLASS, feats, sk_order,
					   sk_uma, &sk_root_off, &sk_toh);
		}
	} else {
		D_PRINT("Open btree%s\n", inplace ? " inplace" : "");
		if (inplace)
			rc = dbtree_open_inplace(sk_root, sk_uma, &sk_toh);
		else
			rc = dbtree_open(sk_root_off, sk_uma, &sk_toh);
	}
	if (rc != 0) {
		sprintf(outbuf, "Tree %s failed: %d\n",
				create ? "create" : "open", rc);
		fail_msg("%s", outbuf);
	}
}

static void
sk_btr_close_destroy(void **state)
{
	int rc;
	bool	destroy;
	char	outbuf[64];

	destroy = tst_fn_val.input;

	if (daos_handle_is_inval(sk_toh)) {
		fail_msg("Invalid tree open handle\n");
	}

	if (destroy) {
		D_PRINT("Destroy btree\n");
		rc = dbtree_destroy(sk_toh, NULL);
	} else {
		D_PRINT("Close btree\n");
		rc = dbtree_close(sk_toh);
	}

	sk_toh = DAOS_HDL_INVAL;
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
	struct sk_rec		*srec;

	umm = utest_utx2umm(sk_utx);

	srec	  = umem_off2ptr(umm, *rec);

	if ((srec->sr_key_len != key->iov_len) ||
	    (memcmp(srec->sr_key, key->iov_buf, key->iov_len) != 0)) {
		D_ERROR("Preserved record mismatch while delete\n");
		return -1;
	}

	utest_free(sk_utx, srec->sr_val_off);
	utest_free(sk_utx, *rec);

	return 0;
}


static char *
btr_opc2str(enum sk_btr_opc opc)
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

static void
sk_btr_kv_operate(void **state)
{
	int		count = 0;
	umem_off_t	rec_off;
	int		rc;
	enum	sk_btr_opc	opc;
	char				*str;
	bool				verbose;
	char				outbuf[64];

	opc = tst_fn_val.opc;
	str = tst_fn_val.optval;
	verbose = tst_fn_val.input;

	if (daos_handle_is_inval(sk_toh)) {
		fail_msg("Can't find opened tree\n");
	}

	while (str != NULL && !isspace(*str) && *str != '\0') {
		char	   *val = NULL;
		char	   *key = str;
		d_iov_t  key_iov;
		d_iov_t  val_iov;

		if (opc == BTR_OPC_UPDATE) {
			val = strchr(str, SK_SEP_VAL);
			if (val == NULL) {
				sprintf(outbuf,
				"Invalid parameters %s (errno %d)\n",
					str, errno);
				fail_msg("%s", outbuf);
			}
			*val = 0;
			str = ++val;
		}

		str = strchr(str, SK_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}

		d_iov_set(&key_iov, key, strlen(key) + 1);
		switch (opc) {
		default:
			fail_msg("Invalid opcode\n");
			break;
		case BTR_OPC_UPDATE:
			d_iov_set(&val_iov, val, strlen(val) + 1);
			rc = dbtree_update(sk_toh, &key_iov, &val_iov);
			if (rc != 0) {
				sprintf(outbuf,
				"Failed to update %s:%s\n", key, val);
				fail_msg("%s", outbuf);
			}
			break;

		case BTR_OPC_DELETE:
			rc = dbtree_delete(sk_toh, BTR_PROBE_EQ,
					   &key_iov, NULL);
			if (rc != 0) {
				sprintf(outbuf, "Failed to delete %s\n", key);
				fail_msg("%s", outbuf);
			}
			if (verbose)
				D_PRINT("Deleted key %s\n", key);

			if (dbtree_is_empty(sk_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_DELETE_RETAIN:
			rc = dbtree_delete(sk_toh, BTR_PROBE_EQ,
					   &key_iov, &rec_off);
			if (rc != 0) {
				sprintf(outbuf, "Failed to delete %s\n", key);
				fail_msg("%s", outbuf);
			}

			/** Verify and delete rec_off here */
			rc = btr_rec_verify_delete(&rec_off, &key_iov);
			if (rc != 0) {
				fail_msg("Failed to verify and delete rec\n");
			}

			if (verbose)
				D_PRINT("Deleted key %s\n", key);
			if (dbtree_is_empty(sk_toh) && verbose)
				D_PRINT("Tree is empty now\n");
			break;

		case BTR_OPC_LOOKUP:
			D_DEBUG(DB_TEST, "Looking for %s\n", key);

			d_iov_set(&val_iov, NULL, 0); /* get address */
			rc = dbtree_lookup(sk_toh, &key_iov, &val_iov);
			if (rc != 0) {
				sprintf(outbuf, "Failed to lookup %s\n", key);
				fail_msg("%s", outbuf);
			}

			if (verbose) {
				D_PRINT("Found key %s, value %s\n",
					key, (char *)val_iov.iov_buf);
			}
			break;
		}
		count++;
	}
	if (verbose)
		D_PRINT("%s %d record(s)\n", btr_opc2str(opc), count);
}

static void
sk_btr_query(void **state)
{
	struct btr_attr		attr;
	struct btr_stat		stat;
	int			rc;
	char		outbuf[64];

	rc = dbtree_query(sk_toh, &attr, &stat);
	if (rc != 0) {
		sprintf(outbuf, "Failed to query btree: %d\n", rc);
		fail_msg("%s", outbuf);
	}

	D_PRINT("tree   [order=%d, depth=%d]\n", attr.ba_order, attr.ba_depth);
	D_PRINT("node   [total="DF_U64"]\n"
		"record [total="DF_U64"]\n"
		"key    [total="DF_U64", max="DF_U64"]\n"
		"val    [total="DF_U64", max="DF_U64"]\n",
		stat.bs_node_nr, stat.bs_rec_nr,
		stat.bs_key_sum, stat.bs_key_max,
		stat.bs_val_sum, stat.bs_val_max);

}

static void
sk_btr_iterate(void **state)
{
	daos_handle_t	ih;
	int		i;
	int		d;
	int		del;
	int		rc;
	int		opc;
	char		*start;
	char		*err;
	char		*arg;
	daos_anchor_t	anchor = {0};
	daos_key_t	anchor_key = {0};

	arg = tst_fn_val.optval;

	if (daos_handle_is_inval(sk_toh)) {
		fail_msg("Can't find opened tree\n");
	}

	rc = dbtree_iter_prepare(sk_toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		err = "Failed to initialize\n";
		goto failed;
	}

	if (arg[0] == 'b') {
		opc = BTR_PROBE_LAST;
	} else if (arg[0] == 'f') {
		opc = BTR_PROBE_FIRST;
	} else {
		opc = BTR_PROBE_FIRST;
	}

	if (arg[0] == 'd' && arg[1] == ':')
		del = atoi(&arg[2]);
	else
		del = 0;

	if (arg[0] == 's' && arg[1] == ':') {
		start = &arg[2];
		opc |= BTR_PROBE_SPEC;
	} else {
		start = "";
	}

	anchor_key.iov_buf = (void *)start;
	anchor_key.iov_len = anchor_key.iov_buf_len = strlen(start) + 1;
	sk_key_encode(NULL, &anchor_key, &anchor);
	anchor.da_type = DAOS_ANCHOR_TYPE_KEY;
	for (i = d = 0;; i++) {
		char		*key;
		d_iov_t	 key_iov;
		d_iov_t	 val_iov;

		if (i == 0 || (del != 0 && d <= del)) {
			rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT,
					       NULL, &anchor);
			if (rc == -DER_NONEXIST)
				break;

			if (rc != 0) {
				err = "Failed probe\n";
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
		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, &anchor);

		if (rc != 0) {
			err = "Failed: fetch\n";
			goto failed;
		}

		key = key_iov.iov_buf;

		if (d != 0) { /* delete */
			rc = dbtree_iter_delete(ih, NULL);
			if (rc != 0) {
				err = "Failed: delete\n";
				goto failed;
			}

		} else { /* iterate */
			D_PRINT("%s: %s\n", key, (char *)val_iov.iov_buf);

			if (opc == BTR_PROBE_LAST)
				rc = dbtree_iter_prev(ih);
			else
				rc = dbtree_iter_next(ih);

			if (rc == -DER_NONEXIST)
				break;

			if (rc != 0) {
				err = "Failed: move\n";
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
	D_PRINT("Test Passed\n");
}

struct kv_node {
	d_iov_t key;
	d_iov_t val;
};


/* Mix up the keys */
static void
sk_btr_mix_keys(struct kv_node *kv, unsigned int key_nr)
{
	int	nr;

	for (nr = key_nr; nr > 0; nr--) {
		struct kv_node	tmp;
		int		j;

		j = rand() % nr;
		if (j != nr - 1) {
			tmp = kv[j];
			kv[j] = kv[nr - 1];
			kv[nr - 1] = tmp;
		}
	}
}

static int
key_cmp(const void *k1, const void *k2)
{
	const d_iov_t	*key1 = k1;
	const d_iov_t	*key2 = k2;
	const char		*s1 = key1->iov_buf;
	const char		*s2 = key2->iov_buf;
	uint64_t		 len;
	int			 rc;

	len = min(key1->iov_len, key2->iov_len);

	rc = strncasecmp(s1, s2, len);

	if (rc != 0)
		return rc;

	return strncmp(s1, s2, len);
}

/* Sort the keys (for sanity check) */
static void
sk_btr_sort_keys(struct kv_node *kv, unsigned int key_nr)
{
	qsort(kv, key_nr, sizeof(*kv), key_cmp);
}

const char valid[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

#define INT_LEN 32
/**
 * An example for string key
 */
D_CASSERT(EMBEDDED_KEY_MAX > INT_LEN);
#define SK_MAX_KEY_LEN (EMBEDDED_KEY_MAX - INT_LEN)

/* fill in @kv with random string keys/values */
static void
sk_btr_gen_keys(struct kv_node *kv, unsigned int key_nr)
{
	char		*key;
	char		*value;
	int		len;
	int		i;
	int		j;

	for (i = 0; i < key_nr; i++) {
		len = rand() % SK_MAX_KEY_LEN;
		kv[i].val.iov_len = len + 4; /* space for KEY\0 */
		D_ALLOC(key, len + INT_LEN);
		kv[i].key.iov_buf = key;
		D_ALLOC(value, kv[i].val.iov_len);
		kv[i].val.iov_buf = value;
		for (j = 0; j < len; j++) {
			int letter = rand() % (sizeof(valid) - 1);

			key[j] = valid[letter];

			letter = (letter + 1) % (sizeof(valid) - 1);
			value[j] = valid[letter];
		}
		strcpy(&value[j], "VAL");
		j = snprintf(key + j, INT_LEN, "key%d", i);
		kv[i].key.iov_len = len + j + 1;
	}
}

static void
sk_btr_destroy_keys(struct kv_node *kv, unsigned int key_nr)
{
	int	 i;

	for (i = 0; i < key_nr; i++) {
		D_FREE(kv[i].key.iov_buf);
		D_FREE(kv[i].val.iov_buf);
	}
}

static int
sk_btr_check_order(struct kv_node *kv, unsigned int key_nr)
{
	char		*key1;
	char		*val1;
	char		*key2;
	char		*val2;
	char		*err;
	daos_handle_t	ih;
	int		i;
	int		rc;

	sk_btr_sort_keys(kv, key_nr);

	rc = dbtree_iter_prepare(sk_toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		err = "initialize";
		goto failed;
	}

	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_DEFAULT, NULL,
			       NULL);
	if (rc == -DER_NONEXIST) {
		err = "nonexist";
		goto failed;
	}

	D_PRINT("Checking %d records\n", key_nr);
	/* check the order */
	i = 0;
	for (;;) {
		d_iov_t	key_iov;
		d_iov_t	val_iov;

		d_iov_set(&key_iov, NULL, 0);
		d_iov_set(&val_iov, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, NULL);
		if (rc != 0) {
			err = "fetch";
			goto failed;
		}

		key1 = key_iov.iov_buf;
		val1 = val_iov.iov_buf;
		key2 = kv[i].key.iov_buf;
		val2 = kv[i].val.iov_buf;
		if (key_iov.iov_len != kv[i].key.iov_len) {
			err = "key length mismatch";
			D_PRINT("key: " DF_U64 " != " DF_U64 "\n",
				 key_iov.iov_len, kv[i].key.iov_len);
			D_PRINT("key: %s != %s\n", key1, key2);
			goto failed;
		}
		if (val_iov.iov_len != kv[i].val.iov_len) {
			err = "value length mismatch";
			D_PRINT("value: " DF_U64 " != " DF_U64 "\n",
				 val_iov.iov_len, kv[i].val.iov_len);
			D_PRINT("val: %s != %s\n", val1, val2);
			goto failed;
		}
		if (memcmp(key_iov.iov_buf, kv[i].key.iov_buf,
			   kv[i].key.iov_len)) {
			err = "key mismatch";
			D_PRINT("key: %s != %s\n", key1, key2);
			goto failed;
		}
		if (memcmp(val_iov.iov_buf, kv[i].val.iov_buf,
			   kv[i].val.iov_len)) {
			err = "value mismatch";
			D_PRINT("val: %s != %s\n", val1, val2);
			goto failed;
		}

		i++;
		if (i == key_nr)
			break;

		rc = dbtree_iter_next(ih);
		if (rc != 0) {
			D_PRINT("rc = %d\n", rc);
			err = "move";
			goto failed;
		}
	}

	D_PRINT("Order is ok\n");
	dbtree_iter_finish(ih);
	return 0;
 failed:
	D_PRINT("Unexpected ordering, error = %s\n", err);
	dbtree_iter_finish(ih);
	return -1;
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
sk_btr_batch_oper(void **state)
{
	struct kv_node	*kv;
	char		*key;
	char		*value;
	char		 buf[1024];
	int		 i;
	int		rc;
	unsigned int	key_nr;
	bool		 verbose;

	key_nr = atoi(tst_fn_val.optval);
	verbose = key_nr < 20;

	if (key_nr == 0 || key_nr > (1U << 28)) {
		D_PRINT("Invalid key number: %d\n", key_nr);
		fail();
	}

	D_ALLOC_ARRAY(kv, key_nr);
	if (kv == NULL)
		fail_msg("Array allocation failed");

	D_PRINT("Batch add %d records.\n", key_nr);
	sk_btr_gen_keys(kv, key_nr);
	for (i = 0; i < key_nr; i++) {
		key = kv[i].key.iov_buf;
		value = kv[i].val.iov_buf;
		sprintf(buf, "%s:%s", key, value);
		tst_fn_val.opc = BTR_OPC_UPDATE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = verbose;
		sk_btr_kv_operate(NULL);
	}

	sk_btr_query(NULL);

	rc = sk_btr_check_order(kv, key_nr);
	if (rc != 0)
		fail_msg("Failed: check order\n");

	/* lookup all rest records, delete 10000 of them, and repeat until
	 * deleting all records.
	 */
	sk_btr_mix_keys(kv, key_nr);
	for (i = 0; i < key_nr;) {
		int	j;

		D_PRINT("Batch lookup %d records.\n", key_nr - i);
		for (j = i; j < key_nr; j++) {
			key = kv[j].key.iov_buf;
			sprintf(buf, "%s", key);
			tst_fn_val.opc = BTR_OPC_LOOKUP;
			tst_fn_val.optval = buf;
			tst_fn_val.input = verbose;
			sk_btr_kv_operate(NULL);
		}

		D_PRINT("Batch delete %d records.\n",
			min(key_nr - i, DEL_BATCH));

		for (j = 0; i < key_nr && j < DEL_BATCH; i++, j++) {
			key = kv[i].key.iov_buf;
			sprintf(buf, "%s", key);
			tst_fn_val.opc = BTR_OPC_DELETE;
			tst_fn_val.optval = buf;
			tst_fn_val.input = verbose;
			sk_btr_kv_operate(NULL);
		}
	}
	sk_btr_query(NULL);
	sk_btr_destroy_keys(kv, key_nr);
	D_FREE(kv);
}

static void
sk_btr_perf(void **state)
{
	struct kv_node	*kv;
	char		*key;
	char		*value;
	char		 buf[1024];
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
		sk_order, key_nr);

	D_ALLOC_ARRAY(kv, key_nr);
	if (kv == NULL)
		fail_msg("Array allocation failed\n");

	/* step-1: Insert performance */
	sk_btr_gen_keys(kv, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		key = kv[i].key.iov_buf;
		value = kv[i].val.iov_buf;
		sprintf(buf, "%s:%s", key, value);
		tst_fn_val.opc = BTR_OPC_UPDATE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		sk_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("insert = %10.2f/sec\n", key_nr / (now - then));

	/* step-2: lookup performance */
	sk_btr_mix_keys(kv, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		key = kv[i].key.iov_buf;
		sprintf(buf, "%s", key);
		tst_fn_val.opc = BTR_OPC_LOOKUP;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		sk_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("lookup = %10.2f/sec\n", key_nr / (now - then));

	/* step-3: delete performance */
	sk_btr_mix_keys(kv, key_nr);
	then = dts_time_now();

	for (i = 0; i < key_nr; i++) {
		key = kv[i].key.iov_buf;
		sprintf(buf, "%s", key);
		tst_fn_val.opc = BTR_OPC_DELETE;
		tst_fn_val.optval = buf;
		tst_fn_val.input = false;
		sk_btr_kv_operate(NULL);
	}
	now = dts_time_now();
	D_PRINT("delete = %10.2f/sec\n", key_nr / (now - then));
	sk_btr_destroy_keys(kv, key_nr);
	D_FREE(kv);
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

static void
ts_group(void **state) {

	int	opt = 0;
	void	**st = NULL;

	D_PRINT("--------------------------------------\n");
	while ((opt = getopt_long(test_group_stop-test_group_start+1,
				  test_group_args+test_group_start,
				  "mC:Docqu:d:r:f:i:b:p:",
				  btr_ops,
				  NULL)) != -1) {
		tst_fn_val.optval = optarg;
		tst_fn_val.input = true;
		switch (opt) {
		case 'C':
			sk_btr_open_create(st);
			break;
		case 'D':
			sk_btr_close_destroy(st);
			break;
		case 'o':
			tst_fn_val.input = false;
			tst_fn_val.optval = NULL;
			sk_btr_open_create(st);
			break;
		case 'c':
			tst_fn_val.input = false;
			sk_btr_close_destroy(st);
			break;
		case 'q':
			sk_btr_query(st);
			break;
		case 'u':
			tst_fn_val.opc = BTR_OPC_UPDATE;
			sk_btr_kv_operate(st);
			break;
		case 'f':
			tst_fn_val.opc = BTR_OPC_LOOKUP;
			sk_btr_kv_operate(st);
			break;
		case 'd':
			tst_fn_val.opc = BTR_OPC_DELETE;
			sk_btr_kv_operate(st);
			break;
		case 'r':
			tst_fn_val.opc = BTR_OPC_DELETE_RETAIN;
			sk_btr_kv_operate(st);
			break;
		case 'i':
			sk_btr_iterate(st);
			break;
		case 'b':
			sk_btr_batch_oper(st);
			break;
		case 'p':
			sk_btr_perf(st);
			break;
		default:
			D_PRINT("Unsupported command %c\n", opt);
		case 'm':
			/* already handled */
			break;
		}
		D_PRINT("--------------------------------------\n");
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
	int		opt;
	int		rc;
	int		start_idx;
	char		*test_name;
	int		stop_idx;

	d_register_alt_assert(mock_assert);

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	sk_toh = DAOS_HDL_INVAL;
	sk_root_off = UMOFF_NULL;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = dbtree_class_register(SK_TREE_CLASS, BTR_FEAT_DIRECT_KEY, &sk_ops);
	D_ASSERT(rc == 0);

	stop_idx = argc-1;
	if (strcmp(argv[1], "--start-test") == 0) {
		start_idx = 2;
		test_name = argv[2];
		if (strcmp(argv[3], "-m") == 0) {
			D_PRINT("Using pmem\n");
			rc = utest_pmem_create(POOL_NAME, POOL_SIZE,
					       sizeof(*sk_root), NULL, &sk_utx);
			D_ASSERT(rc == 0);
		}
	} else {
		start_idx = 0;
		test_name = "Btree testing tool";
		optind = 0;
		/* Check for -m option first */
		while ((opt = getopt_long(argc, argv, "mC:Docqu:d:r:f:i:b:p:",
					  btr_ops, NULL)) != -1) {
			if (opt == 'm') {
				D_PRINT("Using pmem\n");
				rc = utest_pmem_create(POOL_NAME, POOL_SIZE,
						       sizeof(*sk_root), NULL,
						       &sk_utx);
				D_ASSERT(rc == 0);
				break;
			}
		}
	}

	if (sk_utx == NULL) {
		D_PRINT("Using vmem\n");
		rc = utest_vmem_create(sizeof(*sk_root), &sk_utx);
		D_ASSERT(rc == 0);
	}

	sk_root = utest_utx2root(sk_utx);
	sk_uma = utest_utx2uma(sk_utx);

	/* start over */
	optind = 0;
	rc = run_cmd_line_test(test_name, argv, start_idx, stop_idx);

	daos_debug_fini();
	rc += utest_utx_destroy(sk_utx);
	if (rc != 0)
		printf("Error: %d\n", rc);

	return rc;
}
