/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(common)

#include <sys/stat.h>
#include <sys/types.h>
#include <abt.h>
#include <lmdb.h>
#include <daos/sys_db.h>
#include <daos/common.h>
#include <daos_types.h>

#define	SYS_DB_NAME		"sys_db"

#define SYS_DB_MD		"metadata"
#define SYS_DB_MD_VER		"version"

#define SYS_DB_VERSION_1	1
#define SYS_DB_VERSION		SYS_DB_VERSION_1
#define SYS_DB_MAX_MAP_SIZE	(1024 * 1024 *32)

/** private information of LMDB based system DB */
struct lmm_sys_db {
	/** exported part of VOS system DB */
	struct sys_db		 db_pub;
	/* LMDB environment handle */
	MDB_env			*db_env;
	MDB_txn			*db_txn;
	/* Address where the new MDB_dbi handle will be stored */
	MDB_dbi			 db_dbi;
	/* If MDB_dbi handle is valid or not */
	bool			 db_dbi_valid;
	char			*db_file;
	char			*db_path;
	/* DB should be destroyed on exit */
	bool			 db_destroy_db;
	ABT_mutex		 db_lock;
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
	} else if (access(ldb->db_file, R_OK | W_OK) != 0) {
		rc = -DER_NO_PERM;
		D_CRIT("No access to existing db file %s\n", ldb->db_file);
		return rc;
	}

	D_DEBUG(DB_IO, "Opening %s, try_create=%d\n", ldb->db_file, try_create);
	rc = mdb_env_create(&ldb->db_env);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to create env handle for sysdb: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = mdb_env_set_mapsize(ldb->db_env, SYS_DB_MAX_MAP_SIZE);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to set env map size: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = mdb_env_open(ldb->db_env, ldb->db_file, MDB_NOSUBDIR, 0664);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to open env handle for sysdb: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = mdb_txn_begin(ldb->db_env, NULL, 0, &ldb->db_txn);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to begin tx for sysdb: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = mdb_dbi_open(ldb->db_txn, NULL, 0, &ldb->db_dbi);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		D_CRIT("Failed to open sysdb: "DF_RC"\n", DP_RC(rc));
		goto txn_abort;
	}
	ldb->db_dbi_valid = true;

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
		if (rc)
			D_CRIT("Failed to commit version for sysdb: "DF_RC"\n",
			       DP_RC(rc));
		goto out;
	} else {
		/* make lock assertion happen */
		ABT_mutex_lock(db2lmm(db)->db_lock);
		rc = lmm_db_fetch(db, SYS_DB_MD, &key, &val);
		ABT_mutex_unlock(db2lmm(db)->db_lock);
		if (rc) {
			D_CRIT("Failed to read sysdb version: "DF_RC"\n",
			       DP_RC(rc));
			rc = -DER_INVAL;
		}

		if (ver < SYS_DB_VERSION_1 || ver > SYS_DB_VERSION)
			rc = -DER_DF_INCOMPT;
	}
txn_abort:
	mdb_txn_abort(ldb->db_txn);
out:
	ldb->db_txn = NULL;
	return rc;
}

#define MAX_SMD_TABLE_LEN	32
static int
lmm_db_generate_key(char *table, d_iov_t *key, MDB_val *db_key)
{
	char	*new_key;
	int	 table_len;

	table_len = strnlen(table, MAX_SMD_TABLE_LEN + 1);
	if (table_len > MAX_SMD_TABLE_LEN)
		return -DER_INVAL;

	db_key->mv_size = key->iov_len + table_len;
	D_ALLOC(new_key, db_key->mv_size);
	if (new_key == NULL)
		return -DER_NOMEM;

	memcpy(new_key, table, table_len);
	memcpy(new_key + table_len, (char *)key->iov_buf, key->iov_len);
	db_key->mv_data = new_key;

	return 0;
}

static int
lmm_db_unpack_key(char *table, d_iov_t *key, MDB_val *db_key)
{
	int	 table_len = strnlen(table, MAX_SMD_TABLE_LEN + 1);
	char	*buf;
	int	 len;

	if (table_len > MAX_SMD_TABLE_LEN)
		return -DER_INVAL;

	if (db_key->mv_size < table_len)
		return -DER_INVAL;

	len = db_key->mv_size - table_len;
	D_ALLOC(buf, len);
	if (buf == NULL)
		return -DER_NOMEM;

	memcpy(buf, db_key->mv_data + table_len, len);
	d_iov_set(key, buf, len);

	return 0;
}

static int
lmm_db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	MDB_val			 db_key, db_data;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	bool			 end_tx = false;

	D_ASSERT(ABT_mutex_trylock(db2lmm(db)->db_lock) == ABT_ERR_MUTEX_LOCKED);
	if (ldb->db_txn == NULL) {
		rc = mdb_txn_begin(ldb->db_env, NULL, MDB_RDONLY, &ldb->db_txn);
		if (rc)
			return mdb_error2daos_error(rc);
		end_tx = true;
	}

	rc = lmm_db_generate_key(table, key, &db_key);
	if (rc)
		goto out;

	rc = mdb_get(ldb->db_txn, ldb->db_dbi, &db_key, &db_data);
	D_FREE(db_key.mv_data);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		goto out;
	}

	if (db_data.mv_size != val->iov_len) {
		D_ERROR("mismatch value for table: %s, expected: %lu, got: %lu\n",
			table, val->iov_len, db_data.mv_size);
		rc = -DER_MISMATCH;
		goto out;
	}
	memcpy(val->iov_buf, db_data.mv_data, db_data.mv_size);

out:
	if (end_tx) {
		mdb_txn_abort(ldb->db_txn);
		ldb->db_txn = NULL;
	}
	return rc;
}

static int
lmm_db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	MDB_val			 db_key, db_data;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	bool			 end_tx = false;

	if (ldb->db_txn == NULL) {
		rc = lmm_db_tx_begin(db);
		if (rc)
			return rc;
		end_tx = true;
	}

	rc = lmm_db_generate_key(table, key, &db_key);
	if (rc)
		goto out;

	db_data.mv_size = val->iov_len;
	db_data.mv_data = val->iov_buf;

	rc = mdb_put(ldb->db_txn, ldb->db_dbi, &db_key, &db_data, 0);
	if (rc)
		D_ERROR("Failed to put in mdb: %d\n", rc);
	D_FREE(db_key.mv_data);

out:
	rc = mdb_error2daos_error(rc);
	if (end_tx)
		rc = lmm_db_tx_end(db, rc);
	return rc;
}

static int
lmm_db_delete(struct sys_db *db, char *table, d_iov_t *key)
{
	MDB_val			 db_key;
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;
	bool			 end_tx = false;

	if (ldb->db_txn == NULL) {
		rc = lmm_db_tx_begin(db);
		if (rc)
			return rc;
		end_tx = true;
	}

	rc = lmm_db_generate_key(table, key, &db_key);
	if (rc)
		goto out;

	rc = mdb_del(ldb->db_txn, ldb->db_dbi, &db_key, NULL);
	if (rc)
		D_ERROR("Failed to delete in mdb: %d\n", rc);
	D_FREE(db_key.mv_data);

out:
	rc = mdb_error2daos_error(rc);
	if (end_tx)
		rc = lmm_db_tx_end(db, rc);
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
	int			 table_len = strnlen(table, MAX_SMD_TABLE_LEN);

	D_ASSERT(ldb->db_txn == NULL);
	rc = mdb_txn_begin(ldb->db_env, NULL, MDB_RDONLY, &ldb->db_txn);
	if (rc)
		return mdb_error2daos_error(rc);

	rc = mdb_cursor_open(ldb->db_txn, ldb->db_dbi, &cursor);
	if (rc) {
		rc = mdb_error2daos_error(rc);
		goto tx_end;
	}

	while ((rc = mdb_cursor_get(cursor, &db_key, &db_data, MDB_NEXT)) == 0) {
		if (strncmp(db_key.mv_data, table, table_len) != 0)
			continue;

		rc = lmm_db_unpack_key(table, &key, &db_key);
		if (rc)
			goto close;

		rc = cb(db, table, &key, args);
		D_FREE(key.iov_buf);
		if (rc)
			goto close;
	}
	/* reach end */
	if (rc == MDB_NOTFOUND)
		rc = 0;
	rc = mdb_error2daos_error(rc);
close:
	mdb_cursor_close(cursor);
tx_end:
	mdb_txn_abort(ldb->db_txn);
	ldb->db_txn = NULL;

	return rc;
}

static int
lmm_db_tx_begin(struct sys_db *db)
{
	struct lmm_sys_db	*ldb = db2lmm(db);
	int			 rc;

	D_ASSERT(ldb->db_txn == NULL);
	rc = mdb_txn_begin(ldb->db_env, NULL, 0, &ldb->db_txn);

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
	if (rc)
		D_ERROR("Failed to commit txn in mdb: %d\n", rc);

	return mdb_error2daos_error(rc);
}

static void
lmm_db_lock(struct sys_db *db)
{
	ABT_mutex_lock(db2lmm(db)->db_lock);
}

static void
lmm_db_unlock(struct sys_db *db)
{
	ABT_mutex_unlock(db2lmm(db)->db_lock);
}

/** Finalize system DB of VOS */
void
lmm_db_fini(void)
{
	if (lmm_db.db_lock)
		ABT_mutex_free(&lmm_db.db_lock);
	if (lmm_db.db_file) {
		if (lmm_db.db_destroy_db)
			lmm_db_unlink(&lmm_db.db_pub);
		if (lmm_db.db_env) {
			if (lmm_db.db_dbi_valid)
				mdb_dbi_close(lmm_db.db_env, lmm_db.db_dbi);
			mdb_env_close(lmm_db.db_env);
		}
		D_FREE(lmm_db.db_file);
	}

	D_FREE(lmm_db.db_path);
	memset(&lmm_db, 0, sizeof(lmm_db));
}

int
lmm_db_init_ex(const char *db_path, const char *db_name, bool force_create, bool destroy_db_on_fini)
{
	int	rc;

	D_ASSERT(db_path != NULL);

	memset(&lmm_db, 0, sizeof(lmm_db));
	lmm_db.db_destroy_db = destroy_db_on_fini;

	rc = ABT_mutex_create(&lmm_db.db_lock);
	if (rc != ABT_SUCCESS)
		return -DER_NOMEM;

	D_ASPRINTF(lmm_db.db_path, "%s", db_path);
	if (lmm_db.db_path == NULL) {
		D_ERROR("Generate sysdb path failed. %d\n", rc);
		rc = -DER_NOMEM;
		goto failed;
	}

	if (!db_name)
		db_name = SYS_DB_NAME;

	D_ASPRINTF(lmm_db.db_file, "%s/%s", lmm_db.db_path, db_name);
	if (lmm_db.db_file == NULL) {
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
	lmm_db.db_pub.sd_lock	  = lmm_db_lock;
	lmm_db.db_pub.sd_unlock	  = lmm_db_unlock;

	if (force_create)
		lmm_db_unlink(&lmm_db.db_pub);

	rc = access(lmm_db.db_file, F_OK);
	if (rc == 0) {
		rc = lmm_db_open_create(&lmm_db.db_pub, false);
		if (rc) {
			D_ERROR("Failed to open sys DB: "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
		D_DEBUG(DB_IO, "successfully open system DB\n");
	} else {
		rc = lmm_db_open_create(&lmm_db.db_pub, true);
		if (rc) {
			D_ERROR("Failed to create sys DB: "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
		D_DEBUG(DB_IO, "successfully create system DB\n");
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
