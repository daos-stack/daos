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

/**
 * An example for integer key btree.
 */

TMMID_DECLARE(struct ik_rec, 0);

/** integer key record */
struct ik_rec {
	uint64_t	ir_key;
	uint32_t	ir_val_size;
	uint32_t	ir_val_msize;
	umem_id_t	ir_val_mmid;
};

#define IK_TREE_CLASS	100

static struct umem_attr	ik_uma = {
	/* XXX pmem */
	.uma_id		= UMEM_CLASS_VMEM,
};

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
ik_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	struct umem_instance *umm = &tins->ti_umm;
	TMMID(struct ik_rec) irec_mmid;
	struct ik_rec *irec;

	irec_mmid = umem_id_u2t(rec->rec_mmid, struct ik_rec);
	irec = umem_id2ptr_typed(umm, irec_mmid);

	umem_free(umm, irec->ir_val_mmid);
	umem_free_typed(umm, irec_mmid);

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

static btr_ops_t ik_ops = {
	.to_hkey_size	= ik_hkey_size,
	.to_hkey_gen	= ik_hkey_gen,
	.to_rec_alloc	= ik_rec_alloc,
	.to_rec_free	= ik_rec_free,
	.to_rec_fetch	= ik_rec_fetch,
	.to_rec_update	= ik_rec_update,
	.to_rec_string	= ik_rec_string,
};

#define IK_ORDER_DEF	16

static int ik_order = IK_ORDER_DEF;

static TMMID(struct btr_root)	ik_root_mmid;
static struct btr_root		ik_root;
static daos_handle_t		ik_toh;

#define IK_SEP		','
#define IK_SEP_VAL	':'

static int
ik_btr_open_create(bool create, char *args)
{
	bool	inplace = false;
	int	rc;

	if (!daos_handle_is_inval(ik_toh)) {
		D_ERROR("Tree has been opened\n");
		return -1;
	}

	if (create && args != NULL) {
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
		inplace = (ik_root.tr_class != 0);
		if (TMMID_IS_NULL(ik_root_mmid) && !inplace) {
			D_ERROR("Please create tree first\n");
			return -1;
		}
	}

	if (create) {
		D_PRINT("Create btree with order %d%s\n",
			ik_order, inplace ? " inplace" : "");
		if (inplace) {
			rc = dbtree_create_inplace(IK_TREE_CLASS, 0, ik_order,
						   &ik_uma, &ik_root, &ik_toh);
		} else {
			rc = dbtree_create(IK_TREE_CLASS, 0, ik_order, &ik_uma,
					   &ik_root_mmid, &ik_toh);
		}
	} else {
		D_PRINT("Open btree%s\n", inplace ? " inplace" : "");
		if (inplace) {
			rc = dbtree_open_inplace(&ik_root, &ik_uma, &ik_toh);
		} else {
			rc = dbtree_open(ik_root_mmid, &ik_uma, &ik_toh);
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
ik_btr_find_or_update(bool update, char *str)
{
	int	count = 0;
	int	rc;

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

		if (update) {
			val = strchr(str, IK_SEP_VAL);
			if (val == NULL) {
				D_ERROR("Invalid parameters %s\n", str);
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
		if (update) {
			daos_iov_set(&val_iov, val, strlen(val) + 1);
			rc = dbtree_update(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				D_ERROR("Failed to update "DF_U64":%s\n",
					key, val);
				return -1;
			}
		} else {
			D_DEBUG(DF_MISC, "Looking for "DF_U64"\n", key);

			daos_iov_set(&val_iov, NULL, 0); /* get address */
			rc = dbtree_lookup(ik_toh, &key_iov, &val_iov);
			if (rc != 0) {
				D_ERROR("Failed to lookup "DF_U64"\n", key);
				return -1;
			}
			D_PRINT("Found key "DF_U64", value %s\n",
				key, (char *)val_iov.iov_buf);
		}
		count++;
	}
	D_PRINT("%s %d record(s)\n", update ? "Updated" : "Found", count);
	return 0;
}

static int
ik_btr_iterate(char *args)
{
	daos_handle_t	ih;
	int		i;
	int		rc;
	int		opc;

	if (daos_handle_is_inval(ik_toh)) {
		D_ERROR("Can't find opened tree\n");
		return -1;
	}

	rc = dbtree_iter_prepare(ik_toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		D_ERROR("can't intialise iterator\n");
		return -1;
	}

	if (args[0] == 'b')
		opc = BTR_PROBE_LAST;
	else
		opc = BTR_PROBE_FIRST;

	rc = dbtree_iter_probe(ih, opc, NULL, NULL);
	for (i = 0;; i++) {
		daos_iov_t	key_iov;
		daos_iov_t	val_iov;
		uint64_t	key;

		daos_iov_set(&key_iov, NULL, 0);
		daos_iov_set(&val_iov, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, NULL);
		if (rc != 0)
			break;

		D_ASSERT(key_iov.iov_len == sizeof(key));
		memcpy(&key, key_iov.iov_buf, sizeof(key));

		D_PRINT(DF_U64": %s\n", key, (char *)val_iov.iov_buf);

		if (opc == BTR_PROBE_LAST)
			dbtree_iter_prev(ih);
		else
			dbtree_iter_next(ih);
	}

	D_PRINT("%s iterator: total %d record(s)\n",
		opc == BTR_PROBE_FIRST ? "forward" : "backward", i);
	dbtree_iter_finish(ih);
	return 0;
}

static struct option btr_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "update",	required_argument,	NULL,	'u'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "iterate",	required_argument,	NULL,	'i'	},
	{ NULL,		0,			NULL,	0	},
};

int
main(int argc, char **argv)
{
	int	rc;

	ik_toh = DAOS_HDL_INVAL;
	ik_root_mmid = TMMID_NULL(struct btr_root);
	ik_root.tr_class = 0;

	rc = dbtree_class_register(IK_TREE_CLASS, 0, &ik_ops);
	D_ASSERT(rc == 0);

	optind = 0;
	while ((rc = getopt_long(argc, argv, "C:Docu:d:f:i:",
				 btr_ops, NULL)) != -1) {
		switch (rc) {
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
		case 'u':
			rc = ik_btr_find_or_update(true, optarg);
			break;
		case 'f':
			rc = ik_btr_find_or_update(false, optarg);
			break;
		case 'i':
			rc = ik_btr_iterate(optarg);
			break;
		case 'd': /* TODO */
		default:
			D_PRINT("Unsupported command %c\n", rc);
			break;
		}
	}
	return 0;
}
