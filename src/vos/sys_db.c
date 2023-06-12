/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * vos/sys_db.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <sys/stat.h>
#include <daos_srv/vos.h>
#include <daos/sys_db.h>
#include "vos_internal.h"

/* Reserved system pool and container UUIDs
 * TODO: check and reject pool/container creation with reserved IDs
 */
#define SYS_DB_POOL		"00000000-DA05-C001-CAFE-000020200101"
#define SYS_DB_CONT		"00000000-DA05-C001-CAFE-000020191231"

#define SYS_DB_DIR		"daos_sys"
#define	SYS_DB_NAME		"sys_db"

#define SYS_DB_MD		"metadata"
#define SYS_DB_MD_VER		"version"

#define SYS_DB_VERSION_1	1
#define SYS_DB_VERSION		SYS_DB_VERSION_1

#define SYS_DB_SIZE		(128UL << 20)	/* 128MB */
#define SYS_DB_EPC		1

/** private information of VOS system DB (pool & container) */
struct vos_sys_db {
	/** exported part of VOS system DB */
	struct sys_db		 db_pub;
	char			*db_file;
	char			*db_path;
	struct umem_instance	*db_umm;
	/* DB should be destroyed on exit */
	bool			 db_destroy_db;
	ABT_mutex		 db_lock;
	uuid_t			 db_pool;
	uuid_t			 db_cont;
	daos_handle_t		 db_poh;
	daos_handle_t		 db_coh;
	daos_unit_oid_t		 db_obj;
};

static struct vos_sys_db	vos_db;

/** data structure for VOS I/O */
struct sys_db_io {
	d_iov_t			io_key;
	daos_iod_t		io_iod;
	d_sg_list_t		io_sgl;
};

static int
db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val);

static int
db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val);

struct vos_sys_db *
db2vos(struct sys_db *db)
{
	return container_of(db, struct vos_sys_db, db_pub);
}

uuid_t *
vos_db_pool_uuid()
{
	return &vos_db.db_pool;
}

static void
db_close(struct sys_db *db)
{
	struct vos_sys_db *vdb = db2vos(db);

	if (!daos_handle_is_inval(vdb->db_coh)) {
		vos_cont_close(vdb->db_coh);
		vdb->db_coh = DAOS_HDL_INVAL;
	}

	if (!daos_handle_is_inval(vdb->db_poh)) {
		vos_pool_close(vdb->db_poh);
		vdb->db_poh = DAOS_HDL_INVAL;
	}
}

static void
db_unlink(struct sys_db *db)
{
	struct vos_sys_db *vdb = db2vos(db);

	unlink(vdb->db_file); /* ignore error code */
}

/* open or create system DB stored in pmemfile */
static int
db_open_create(struct sys_db *db, bool try_create)
{
	struct vos_sys_db *vdb = db2vos(db);
	d_iov_t		   key;
	d_iov_t		   val;
	uint32_t	   ver;
	int		   rc;

	if (try_create) {
		rc = mkdir(vdb->db_path, 0777);
		if (rc < 0 && errno != EEXIST) {
			rc = daos_errno2der(errno);
			goto failed;
		}
	} else if (access(vdb->db_file, F_OK) != 0) {
		D_DEBUG(DB_IO, "%s doesn't exist, bypassing vos_pool_open\n",
			vdb->db_file);
		rc = -DER_NONEXIST;
		goto failed;
	} else if (access(vdb->db_file, R_OK | W_OK) != 0) {
		rc = -DER_NO_PERM;
		D_CRIT("No access to existing db file %s\n", vdb->db_file);
		goto failed;
	}
	D_DEBUG(DB_IO, "Opening %s, try_create=%d\n", vdb->db_file, try_create);
	if (try_create) {
		rc = vos_pool_create(vdb->db_file, vdb->db_pool, SYS_DB_SIZE, 0,
				     VOS_POF_SYSDB, &vdb->db_poh);
		if (rc) {
			D_CRIT("sys pool create error: "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
	} else {
		rc = vos_pool_open(vdb->db_file, vdb->db_pool, VOS_POF_SYSDB, &vdb->db_poh);
		if (rc) {
			/**
			 * The access checks above should ensure the file
			 * exists.
			 */
			D_CRIT("sys pool open error: "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
	}

	if (try_create) {
		rc = vos_cont_create(vdb->db_poh, vdb->db_cont);
		if (rc) {
			D_CRIT("sys cont create error: "DF_RC"\n", DP_RC(rc));
			goto failed;
		}
	}
	rc = vos_cont_open(vdb->db_poh, vdb->db_cont, &vdb->db_coh);
	if (rc) {
		D_CRIT("sys cont open error: "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	vdb->db_umm = vos_pool2umm(vos_hdl2pool(vdb->db_poh));
	d_iov_set(&key, SYS_DB_MD_VER, strlen(SYS_DB_MD_VER));
	d_iov_set(&val, &ver, sizeof(ver));
	if (try_create) {
		ver = SYS_DB_VERSION;
		rc = db_upsert(db, SYS_DB_MD, &key, &val);
		if (rc) {
			D_CRIT("Failed to set version for sysdb: "DF_RC"\n",
			       DP_RC(rc));
			goto failed;
		}
	} else {
		rc = db_fetch(db, SYS_DB_MD, &key, &val);
		if (rc) {
			D_CRIT("Failed to read sysdb version: "DF_RC"\n",
			       DP_RC(rc));
			goto failed;
		}

		if (ver < SYS_DB_VERSION_1 || ver > SYS_DB_VERSION) {
			vos_report_layout_incompat("SMD", ver,
						   SYS_DB_VERSION_1,
						   SYS_DB_VERSION,
						   &vdb->db_pool);
			rc = -DER_DF_INCOMPT;
			goto failed;
		}
	}
	return 0;
failed:
	db_close(db);
	return rc;
}

static void
db_io_init(struct sys_db_io *io, char *table, d_iov_t *key, d_iov_t *val)
{
	memset(io, 0, sizeof(*io));

	d_iov_set(&io->io_key, table, strlen(table));
	io->io_iod.iod_type = DAOS_IOD_SINGLE;
	io->io_iod.iod_name = *key;
	io->io_iod.iod_nr   = 1;
	if (val) {
		io->io_iod.iod_size = val->iov_len;
		io->io_sgl.sg_iovs  = val;
		io->io_sgl.sg_nr    = 1;
	}
}

static int
db_fetch(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	struct vos_sys_db *vdb = db2vos(db);
	struct sys_db_io   io;
	int		   rc;

	D_ASSERT(!daos_handle_is_inval(vdb->db_coh));

	db_io_init(&io, table, key, val);
	rc = vos_obj_fetch(vdb->db_coh, vdb->db_obj, SYS_DB_EPC, 0,
			   &io.io_key, 1, &io.io_iod, &io.io_sgl);
	/* NB: VOS returns zero for empty key */
	if (rc == 0 && val->iov_len == 0)
		rc = -DER_NONEXIST;

	return rc;
}

static int
db_upsert(struct sys_db *db, char *table, d_iov_t *key, d_iov_t *val)
{
	struct vos_sys_db *vdb = db2vos(db);
	struct sys_db_io   io;
	int		   rc;

	D_ASSERT(!daos_handle_is_inval(vdb->db_coh));

	db_io_init(&io, table, key, val);
	rc = vos_obj_update(vdb->db_coh, vdb->db_obj, SYS_DB_EPC, 0, 0,
			    &io.io_key, 1, &io.io_iod, NULL, &io.io_sgl);
	return rc;
}

static int
db_delete(struct sys_db *db, char *table, d_iov_t *key)
{
	struct vos_sys_db *vdb = db2vos(db);
	struct sys_db_io   io;
	int		   rc;

	D_ASSERT(!daos_handle_is_inval(vdb->db_coh));

	db_io_init(&io, table, key, NULL);
	rc = vos_obj_del_key(vdb->db_coh, vdb->db_obj, &io.io_key,
			     &io.io_iod.iod_name);
	if (rc == 0) {
		int creds = 100;
		/* vos_obj_del_key() wouldn't free space */
		vos_gc_pool_tight(vdb->db_poh, &creds);
	}
	return rc;
}

struct db_trav_args {
	struct sys_db		*ta_db;
	char			*ta_table;
	void			*ta_cb_args;
	sys_db_trav_cb_t	 ta_cb;
};

/* private iterator callback that ignores those unused parameters for user */
static int
db_trav_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	   vos_iter_param_t *iter_param, void *data, unsigned *acts)
{
	struct db_trav_args	*ta = data;

	return ta->ta_cb(ta->ta_db, ta->ta_table, &entry->ie_key,
			 ta->ta_cb_args);
}

static int
db_traverse(struct sys_db *db, char *table, sys_db_trav_cb_t cb, void *args)
{
	struct vos_sys_db	*vdb = db2vos(db);
	struct vos_iter_anchors  anchors = { 0 };
	struct db_trav_args	 ta;
	vos_iter_param_t	 ip;
	int			 rc;

	D_ASSERT(!daos_handle_is_inval(vdb->db_coh));

	memset(&ip, 0, sizeof(ip));
	d_iov_set(&ip.ip_dkey, table, strlen(table));
	ip.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	ip.ip_hdl	 = vdb->db_coh;
	ip.ip_oid	 = vdb->db_obj;

	ta.ta_db	 = db;
	ta.ta_table	 = table;
	ta.ta_cb_args	 = args;
	ta.ta_cb	 = cb;
	rc = vos_iterate(&ip, VOS_ITER_AKEY, false, &anchors,
			 db_trav_cb, NULL, &ta, NULL);
	return rc;
}

int
db_tx_begin(struct sys_db *db)
{
	struct vos_sys_db *vdb = db2vos(db);

	/* NB: it's OK to start nested PMDK transaction */
	D_ASSERT(vdb->db_umm);
	return umem_tx_begin(vdb->db_umm, NULL);
}

int
db_tx_end(struct sys_db *db, int rc)
{
	struct vos_sys_db *vdb = db2vos(db);

	D_ASSERT(vdb->db_umm);
	return umem_tx_end(vdb->db_umm, rc);
}

void
db_lock(struct sys_db *db)
{
	ABT_mutex_lock(db2vos(db)->db_lock);
}

void
db_unlock(struct sys_db *db)
{
	ABT_mutex_unlock(db2vos(db)->db_lock);
}

/** Initialize system DB of VOS */
int
vos_db_init(const char *db_path)
{
	return vos_db_init_ex(db_path, NULL, false, false);
}

int
vos_db_init_ex(const char *db_path, const char *db_name, bool force_create, bool destroy_db_on_fini)
{
	int	create;
	int	rc;

	D_ASSERT(db_path != NULL);

	memset(&vos_db, 0, sizeof(vos_db));
	vos_db.db_destroy_db = destroy_db_on_fini;

	rc = asprintf(&vos_db.db_path, "%s/%s", db_path, SYS_DB_DIR);
	if (rc < 0) {
		D_ERROR("Generate sysdb path failed. %d\n", rc);
		return -DER_NOMEM;
	}

	if (!db_name)
		db_name = SYS_DB_NAME;

	rc = asprintf(&vos_db.db_file, "%s/%s", vos_db.db_path, db_name);
	if (rc < 0) {
		D_ERROR("Generate sysdb filename failed. %d\n", rc);
		rc = -DER_NOMEM;
		goto failed;
	}

	rc = ABT_mutex_create(&vos_db.db_lock);
	if (rc != ABT_SUCCESS) {
		rc = -DER_NOMEM;
		goto failed;
	}

	vos_db.db_poh = DAOS_HDL_INVAL;
	vos_db.db_coh = DAOS_HDL_INVAL;

	strncpy(vos_db.db_pub.sd_name, db_name, SYS_DB_NAME_SZ - 1);
	vos_db.db_pub.sd_fetch	  = db_fetch;
	vos_db.db_pub.sd_upsert	  = db_upsert;
	vos_db.db_pub.sd_delete	  = db_delete;
	vos_db.db_pub.sd_traverse = db_traverse;
	vos_db.db_pub.sd_tx_begin = db_tx_begin;
	vos_db.db_pub.sd_tx_end	  = db_tx_end;
	vos_db.db_pub.sd_lock	  = db_lock;
	vos_db.db_pub.sd_unlock	  = db_unlock;

	rc = uuid_parse(SYS_DB_POOL, vos_db.db_pool);
	D_ASSERTF(rc == 0, "Failed to parse sys pool uuid: %s\n", SYS_DB_POOL);

	rc = uuid_parse(SYS_DB_CONT, vos_db.db_cont);
	D_ASSERTF(rc == 0, "Failed to parse sys cont uuid: %s\n", SYS_DB_CONT);

	if (force_create)
		db_unlink(&vos_db.db_pub);

	for (create = 0; create <= 1; create++) {
		rc = db_open_create(&vos_db.db_pub, !!create);
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
	vos_db_fini();
	return rc;
}

/** Finalize system DB of VOS */
void
vos_db_fini(void)
{
	db_close(&vos_db.db_pub);
	if (vos_db.db_lock)
		ABT_mutex_free(&vos_db.db_lock);

	if (vos_db.db_file) {
		if (vos_db.db_destroy_db) {
			int rc;

			rc = vos_pool_destroy_ex(vos_db.db_file, vos_db.db_pool, 0);
			if (rc != 0)
				D_ERROR(DF_UUID": failed to destroy %s: %d\n",
					DP_UUID(vos_db.db_pool), vos_db.db_file, rc);
		}
		free(vos_db.db_file);
	}

	if (vos_db.db_path)
		free(vos_db.db_path);

	memset(&vos_db, 0, sizeof(vos_db));
}

/** Export system DB of VOS */
struct sys_db *
vos_db_get(void)
{
	return &vos_db.db_pub;
}
