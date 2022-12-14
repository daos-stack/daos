/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <sys/stat.h>
#include <sys/types.h>
#include <daos_srv/vos.h>
#include <lmdb.h>

#define	SYS_DB_DIR		"daos_sys"
#define	SYS_DB_NAME		"sys_db"

#define SYS_DB_MD		"metadata"
#define SYS_DB_MD_VER		"version"

#define SYS_DB_VERSION_1	1
#define SYS_DB_VERSION		SYS_DB_VERSION_1

struct lmm_dbi_entry {
	d_list_t		 lde_link;
	char			*lde_table;
	MDB_dbi			 lde_dbi;
	int			 lde_ref;
};

/** private information of LMDB based system DB */
struct lmm_sys_db {
	/** exported part of VOS system DB */
	struct sys_db		 db_pub;
	/* LMDB environment handle */
	MDB_env			*db_env;
	MDB_txn			*db_txn;

	char			*db_file;
	char			*db_path;
	/* DB should be destroyed on exit */
	bool			 db_destroy_db;

	struct d_hash_table	 db_htable;
};

static struct lmm_sys_db	lmm_db;

static int
lmm_db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val);

static int
lmm_db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val);

static int
lmm_db_tx_begin(struct sys_db *db);

static int
lmm_db_tx_end(struct sys_db *db, int rc);

static struct lmm_sys_db *
db2lmm(struct sys_db *db)
{
	return container_of(db, struct lmm_sys_db, db_pub);
}

static void
lmm_db_unlink(struct sys_db *db)
{
	struct lmm_sys_db *ldb = db2lmm(db);

	unlink(ldb->db_file); /* ignore error code */
}

static int
mdb_error2daos_error(int rc)
{
	if (rc > 0)
		rc = -rc;

	switch (rc) {
	case 0:
		return 0;
	case MDB_VERSION_MISMATCH:
		return -DER_MISMATCH;
	case MDB_INVALID:
		return -DER_INVAL;
	case MDB_PANIC:
	case MDB_MAP_RESIZED:
		return -DER_SHUTDOWN;
	case MDB_READERS_FULL:
		return -DER_AGAIN;
	case MDB_NOTFOUND:
		return -DER_NONEXIST;
	case MDB_KEYEXIST:
		return -DER_EXIST;
	default:
		return daos_errno2der(-rc);
	}

}

/* Max number of DBs */
#define LMM_MAX_DBS		64
#define LMM_DB_TREE_ORDER	8
#define LMM_MAX_TABLE_LEN	32

struct lmm_dbi_entry *
lmm_link2dbi(d_list_t *link)
{
	return container_of(link, struct lmm_dbi_entry, lde_link);
}

static bool
lmm_hop_key_cmp(struct d_hash_table *htab, d_list_t *link,
	       const void *key, unsigned int ksize)
{
	struct lmm_dbi_entry	*lde = lmm_link2dbi(link);
	int			 len = strnlen(lde->lde_table, LMM_MAX_TABLE_LEN);

	D_ASSERT(ksize <= LMM_MAX_TABLE_LEN);
	if (len != ksize)
		return false;

	return !strncmp(lde->lde_table, key, ksize);
}

static void
lmm_hop_rec_addref(struct d_hash_table *htab, d_list_t *link)
{
	struct lmm_dbi_entry	*lde = lmm_link2dbi(link);

	lde->lde_ref++;
}

static bool
lmm_hop_rec_decref(struct d_hash_table *htab, d_list_t *link)
{
	struct lmm_dbi_entry	*lde = lmm_link2dbi(link);

	D_ASSERT(lde->lde_ref > 0);
	lde->lde_ref--;

	return lde->lde_ref == 0;
}

void
lmm_hop_rec_free(struct d_hash_table *htab, d_list_t *link)
{
	struct lmm_dbi_entry	*lde = lmm_link2dbi(link);

	D_ASSERT(lde->lde_ref == 0);

	mdb_dbi_close(lmm_db.db_env, lde->lde_dbi);
	D_FREE(lde);
}

static d_hash_table_ops_t lmm_hash_ops = {
	.hop_key_cmp            = lmm_hop_key_cmp,
	.hop_rec_addref         = lmm_hop_rec_addref,
	.hop_rec_decref         = lmm_hop_rec_decref,
	.hop_rec_free           = lmm_hop_rec_free,
};

static int
lmm_dbi_get(struct lmm_sys_db *ldb, char *table, bool create, MDB_dbi *dbi)
{
	int			 rc;
	d_list_t		*link;
	int			 table_len = strnlen(table, LMM_MAX_TABLE_LEN);
	struct lmm_dbi_entry	*lde = NULL;

	link = d_hash_rec_find(&ldb->db_htable, table, table_len);
	if (link) {
		lde = lmm_link2dbi(link);
		*dbi = lde->lde_dbi;
		d_hash_rec_decref(&ldb->db_htable, link);
		return 0;
	}

	if (create == false)
		return -DER_NONEXIST;

	D_ALLOC_PTR(lde);
	if (lde == NULL)
		return -DER_NOMEM;

	rc = mdb_dbi_open(ldb->db_txn, table, MDB_CREATE, dbi);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		goto out;
	}

	lde->lde_table = table;
	lde->lde_dbi = *dbi;
	D_INIT_LIST_HEAD(&lde->lde_link);
	rc = d_hash_rec_insert(&ldb->db_htable, table, table_len, &lde->lde_link, true);
	if (rc)
		goto close;

	return 0;
close:
	mdb_dbi_close(ldb->db_env, *dbi);
out:
	D_FREE(lde);
	return rc;
}

/* open or create system DB stored in external storage */
static int
lmm_db_open_create(struct sys_db *db, bool try_create)
{
	struct lmm_sys_db *ldb = db2lmm(db);
	d_iov_t		   key;
	d_iov_t		   val;
	uint32_t	   ver;
	int		   rc = 0;

	if (try_create) {
		rc = mkdir(ldb->db_path, 0777);
		if (rc < 0 && errno != EEXIST) {
			rc = daos_errno2der(errno);
			return rc;
		}
	} else if (access(ldb->db_file, F_OK) != 0) {
		D_DEBUG(DB_IO, "%s doesn't exist, bypassing lmdb open\n",
			ldb->db_file);
		rc = -DER_NONEXIST;
		return rc;
	} else if (access(ldb->db_file, R_OK | W_OK) != 0) {
		rc = -DER_NO_PERM;
		D_CRIT("No access to existing db file %s\n", ldb->db_file);
		return rc;
	}

	D_DEBUG(DB_IO, "Opening %s, try_create=%d\n", ldb->db_file, try_create);
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 4, NULL,
					 &lmm_hash_ops, &ldb->db_htable);
	if (rc) {
		D_CRIT("Failed to create hash table for sysdb: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = mdb_env_create(&ldb->db_env);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to create env handle for sysdb: "DF_RC"\n", DP_RC(rc));
		goto htable_destroy;
	}
	rc = mdb_env_set_maxdbs(ldb->db_env, LMM_MAX_DBS);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to set maxdbs for sysdb: "DF_RC"\n", DP_RC(rc));
		goto env_close;
	}

	rc = mdb_env_open(ldb->db_env, ldb->db_file, MDB_NOSUBDIR, 0664);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to open env handle for sysdb: "DF_RC"\n", DP_RC(rc));
		goto env_close;
	}

	rc = lmm_db_tx_begin(db);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to begin tx for sysdb: "DF_RC"\n", DP_RC(rc));
		goto env_close;
	}

	d_iov_set(&key, SYS_DB_MD_VER, strlen(SYS_DB_MD_VER));
	d_iov_set(&val, &ver, sizeof(ver));
	if (try_create) {
		ver = SYS_DB_VERSION;
		rc = lmm_db_upsert(db, SYS_DB_MD, &key, &val);
		if (rc) {
			D_CRIT("Failed to set version for sysdb: "DF_RC"\n",
			       DP_RC(rc));
			goto txn_abort;
		}
		rc = mdb_txn_commit(ldb->db_txn);
		if (rc) {
			D_CRIT("Failed to commit version for sysdb: "DF_RC"\n",
			       DP_RC(rc));
			goto env_close;
		}
	} else {
		rc = lmm_db_fetch(db, SYS_DB_MD, &key, &val);
		if (rc) {
			D_CRIT("Failed to read sysdb version: "DF_RC"\n",
			       DP_RC(rc));
			goto txn_abort;
		}

		if (ver < SYS_DB_VERSION_1 || ver > SYS_DB_VERSION) {
			rc = -DER_DF_INCOMPT;
			goto txn_abort;
		}
		mdb_txn_abort(ldb->db_txn);
	}
	ldb->db_txn = NULL;
	return 0;

txn_abort:
	mdb_txn_abort(ldb->db_txn);
env_close:
	mdb_env_close(ldb->db_env);
htable_destroy:
	d_hash_table_destroy_inplace(&ldb->db_htable, true);

	return rc;
}

static int
lmm_db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	MDB_val			 db_key, db_data;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	MDB_dbi			 dbi;
	bool			 end_tx = false;

	if (ldb->db_txn == NULL) {
		rc = mdb_txn_begin(ldb->db_env, NULL, MDB_RDONLY, &ldb->db_txn);
		if (rc)
			return mdb_error2daos_error(rc);
		end_tx = true;
	}

	rc = lmm_dbi_get(ldb, table, false, &dbi);
	if (rc)
		goto out;

	db_key.mv_size = key->iov_len;
	db_key.mv_data = key->iov_buf;
	rc = mdb_get(ldb->db_txn, dbi, &db_key, &db_data);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		goto out;
	}

	if (db_data.mv_size != val->iov_len) {
		D_ERROR("mismatch vale for table: %s, expected: %lu, got: %lu\n",
			table, val->iov_len, db_data.mv_size);
		rc = -DER_MISMATCH;
		goto out;
	}
	memcpy(val->iov_buf, db_data.mv_data, db_data.mv_size);

out:
	if (end_tx)
		rc = lmm_db_tx_end(db, rc);
	return rc;
}

static int
lmm_db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	MDB_val			 db_key, db_data;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	MDB_dbi			 dbi;
	bool			 end_tx = false;

	if (ldb->db_txn == NULL) {
		rc = lmm_db_tx_begin(db);
		if (rc)
			return rc;
		end_tx = true;
	}

	rc = lmm_dbi_get(ldb, table, true, &dbi);
	if (rc)
		goto out;

	db_key.mv_size = key->iov_len;
	db_key.mv_data = key->iov_buf;

	db_data.mv_size = val->iov_len;
	db_data.mv_data = val->iov_buf;

	rc = mdb_put(ldb->db_txn, dbi, &db_key, &db_data, 0);

out:
	rc = mdb_error2daos_error(rc);
	if (end_tx)
		lmm_db_tx_end(db, rc);
	return rc;
}

static int
lmm_db_delete(struct sys_db *db, char *table, d_iov_t *key)
{
	MDB_val			 db_key;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	MDB_dbi			 dbi;
	bool			 end_tx = false;

	if (ldb->db_txn == NULL) {
		rc = lmm_db_tx_begin(db);
		if (rc)
			return rc;
		end_tx = true;
	}

	rc = lmm_dbi_get(ldb, table, true, &dbi);
	if (rc)
		goto out;

	db_key.mv_size = key->iov_len;
	db_key.mv_data = key->iov_buf;
	rc = mdb_del(ldb->db_txn, dbi, &db_key, NULL);

out:
	rc = mdb_error2daos_error(rc);
	if (end_tx)
		lmm_db_tx_end(db, rc);
	return rc;
}

static int
lmm_db_traverse(struct sys_db *db, char *table, sys_db_trav_cb_t cb, void *args)
{
	struct lmm_sys_db	*ldb = db2lmm(db);
	MDB_cursor		*cursor;
	MDB_val			 db_key, db_data;
	int			 rc;
	d_iov_t			 key;
	MDB_dbi			 dbi;

	D_ASSERT(ldb->db_txn == NULL);
	rc = mdb_txn_begin(ldb->db_env, NULL, MDB_RDONLY, &ldb->db_txn);
	if (rc)
		return mdb_error2daos_error(rc);

	rc = lmm_dbi_get(ldb, table, false, &dbi);
	if (rc)
		goto tx_end;

	rc = mdb_cursor_open(ldb->db_txn, dbi, &cursor);
	if (rc)
		goto tx_end;

	while ((rc = mdb_cursor_get(cursor, &db_key, &db_data, MDB_NEXT)) == 0) {
		d_iov_set(&key, db_key.mv_data, db_key.mv_size);
		rc = cb(db, table, &key, args);
		if (rc)
			break;
	}
	/* reach end */
	if (rc == MDB_NOTFOUND)
		rc = 0;

	mdb_cursor_close(cursor);
tx_end:
	rc = lmm_db_tx_end(db, rc);
	return mdb_error2daos_error(rc);
}

static int
lmm_db_tx_begin(struct sys_db *db)
{
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;

	D_ASSERT(ldb->db_txn == NULL);
	rc = mdb_txn_begin(ldb->db_env, NULL, 0, &ldb->db_txn);
	if (rc)
		goto out;
out:
	return mdb_error2daos_error(rc);
}

static int
lmm_db_tx_end(struct sys_db *db, int rc)
{
	struct lmm_sys_db	*ldb = db2lmm(db);
	MDB_txn			*txn = ldb->db_txn;

	D_ASSERT(txn != NULL);
	ldb->db_txn = NULL;

	if (rc) {
		mdb_txn_abort(txn);
		return rc;
	}

	rc = mdb_txn_commit(txn);

	return mdb_error2daos_error(rc);
}

/** Finalize system DB of VOS */
void
lmm_db_fini(void)
{
	d_hash_table_destroy_inplace(&lmm_db.db_htable, true);
	if (lmm_db.db_file) {
		mdb_env_close(lmm_db.db_env);
		if (lmm_db.db_destroy_db)
			lmm_db_unlink(&lmm_db.db_pub);
		free(lmm_db.db_file);
	}

	free(lmm_db.db_path);
	memset(&lmm_db, 0, sizeof(lmm_db));
}
int
lmm_db_init_ex(const char *db_path, const char *db_name, bool force_create, bool destroy_db_on_fini)
{
	int	create;
	int	rc;

	D_ASSERT(db_path != NULL);

	memset(&lmm_db, 0, sizeof(lmm_db));
	lmm_db.db_destroy_db = destroy_db_on_fini;

	rc = asprintf(&lmm_db.db_path, "%s/%s", db_path, SYS_DB_DIR);
	if (rc < 0) {
		D_ERROR("Generate sysdb path failed. %d\n", rc);
		return -DER_NOMEM;
	}

	if (!db_name)
		db_name = SYS_DB_NAME;

	rc = asprintf(&lmm_db.db_file, "%s/%s", lmm_db.db_path, db_name);
	if (rc < 0) {
		D_ERROR("Generate sysdb filename failed. %d\n", rc);
		rc = -DER_NOMEM;
		goto failed;
	}

	strncpy(lmm_db.db_pub.sd_name, db_name, SYS_DB_NAME_SZ - 1);
	lmm_db.db_pub.sd_fetch	  = lmm_db_fetch;
	lmm_db.db_pub.sd_upsert	  = lmm_db_upsert;
	lmm_db.db_pub.sd_delete	  = lmm_db_delete;
	lmm_db.db_pub.sd_traverse = lmm_db_traverse;
	lmm_db.db_pub.sd_tx_begin = lmm_db_tx_begin;
	lmm_db.db_pub.sd_tx_end	  = lmm_db_tx_end;
	lmm_db.db_pub.sd_lock	  = NULL;
	lmm_db.db_pub.sd_unlock	  = NULL;

	if (force_create)
		lmm_db_unlink(&lmm_db.db_pub);

	for (create = 0; create <= 1; create++) {
		rc = lmm_db_open_create(&lmm_db.db_pub, !!create);
		if (rc == 0) {
			D_DEBUG(DB_IO, "successfully open system DB\n");
			break;
		}
		if (create || rc != -DER_NONEXIST) {
			D_ERROR("Failed to open/create(%d) sys DB: "DF_RC"\n",
				create, DP_RC(rc));
			goto failed;
		}
		D_DEBUG(DB_DF, "Try to create system DB\n");
	}
	return 0;
failed:
	lmm_db_fini();
	return rc;
}

/** Initialize system DB of VOS */
int
lmm_db_init(const char *db_path)
{
	return lmm_db_init_ex(db_path, NULL, false, false);
}


/** Export system DB of VOS */
struct sys_db *
lmm_db_get(void)
{
	return &lmm_db.db_pub;
}
