/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <libgen.h>

#include <daos/common.h>
#include <daos/btree_class.h>
#include "smd_internal.h"

struct smd_store	smd_store;

#define SMD_STORE_DIR	"daos-srv_meta"
#define	SMD_STORE_FILE	"nvme-meta"

static int
smd_store_gen_fname(const char *path, char **store_fname)
{
	D_ASSERT(path != NULL);
	D_ASSERT(store_fname != NULL);

	D_ASPRINTF(*store_fname, "%s/%s/%s", path, SMD_STORE_DIR,
		   SMD_STORE_FILE);
	if (!*store_fname) {
		return -DER_NOMEM;
	}

	return 0;
}

int
smd_store_destroy(const char *path)
{
	char	*fname;
	int	 rc;

	rc = smd_store_gen_fname(path, &fname);
	if (rc)
		return rc;

	rc = unlink(fname);
	if (rc < 0 && errno != ENOENT) {
		D_ERROR("Unlink SMD store file %s failed. %s\n",
			fname, strerror(errno));
		rc = daos_errno2der(errno);
		goto out;
	}

	rc = rmdir(dirname(fname));
	if (rc < 0 && errno != ENOENT) {
		D_ERROR("Unlink SMD store dir %s failed. %s\n",
			fname, strerror(errno));
		rc = daos_errno2der(errno);
		goto out;
	}
	rc = 0;
out:
	D_FREE(fname);
	return rc;
}

static int
smd_store_check(char *fname, bool *existing)
{
	int	rc;

	*existing = false;
	rc = access(fname, R_OK | W_OK);
	if (rc && errno == ENOENT)
		rc = 0;
	else if (rc)
		rc = daos_errno2der(errno);
	else
		*existing = true;

	return rc;
}

#define SMD_FILE_SIZE	(128UL << 20)	/* 128MB */
#define SMD_TREE_ODR	32

static int
smd_store_create(char *fname)
{
	struct umem_attr	 uma = {0};
	struct umem_instance	 umm = {0};
	char			*dir;
	struct smd_df		*smd_df;
	PMEMobjpool		*ph;
	daos_handle_t		 btr_hdl;
	int			 fd, rc;

	dir = strdup(fname);
	if (dir == NULL)
		return -DER_NOMEM;
	dir = dirname(dir);

	rc = mkdir(dir, 0700);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("Create SMD dir %s failed. %s\n",
			dir, strerror(errno));
		free(dir);
		return daos_errno2der(errno);
	}
	free(dir);

	fd = open(fname, O_WRONLY | O_CREAT, 0600);
	if (fd < 0) {
		D_ERROR("Create SMD file %s failed. %s\n",
			fname, strerror(errno));
		return daos_errno2der(errno);
	}

	rc = fallocate(fd, 0, 0, SMD_FILE_SIZE);
	if (rc) {
		D_ERROR("Pre-alloc SMD file size:"DF_U64" failed. %s\n",
			SMD_FILE_SIZE, strerror(errno));
		rc = daos_errno2der(errno);
		close(fd);
		return rc;
	}
	close(fd);

	ph = pmemobj_create(fname, POBJ_LAYOUT_NAME(smd_md_layout), 0, 0600);
	if (!ph) {
		D_ERROR("Create SMD pmemobj pool %s failed. %s\n",
			fname, pmemobj_errormsg());
		return -DER_INVAL;
	}

	smd_df = smd_pop2df(ph);
	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_pool = ph;

	rc = umem_class_init(&uma, &umm);
	if (rc != 0)
		goto close;

	rc = umem_tx_begin(&umm, NULL);
	if (rc != 0)
		goto close;

	rc = pmemobj_tx_add_range_direct(smd_df, sizeof(*smd_df));
	if (rc != 0)
		goto tx_end;

	memset(smd_df, 0, sizeof(*smd_df));
	smd_df->smd_magic = SMD_DF_MAGIC;
	if (DAOS_FAIL_CHECK(FLC_SMD_DF_VER))
		smd_df->smd_version = 0;
	else
		smd_df->smd_version = SMD_DF_VERSION;

	/* Create device table */
	rc = dbtree_create_inplace(DBTREE_CLASS_UV, 0, SMD_TREE_ODR, &uma,
				   &smd_df->smd_dev_tab, &btr_hdl);
	if (rc) {
		D_ERROR("Create SMD device table failed: "DF_RC"\n", DP_RC(rc));
		goto tx_end;
	}
	dbtree_close(btr_hdl);

	/* Create pool table */
	rc = dbtree_create_inplace(DBTREE_CLASS_UV, 0, SMD_TREE_ODR, &uma,
				   &smd_df->smd_pool_tab, &btr_hdl);
	if (rc) {
		D_ERROR("Create SMD pool table failed: "DF_RC"\n", DP_RC(rc));
		goto tx_end;
	}
	dbtree_close(btr_hdl);

	/* Create target table */
	rc = dbtree_create_inplace(DBTREE_CLASS_KV, 0, SMD_TREE_ODR, &uma,
				   &smd_df->smd_tgt_tab, &btr_hdl);
	if (rc) {
		D_ERROR("Create SMD target table failed: "DF_RC"\n", DP_RC(rc));
		goto tx_end;
	}
	dbtree_close(btr_hdl);

tx_end:
	if (rc == 0)
		rc = umem_tx_commit(&umm);
	else
		rc = umem_tx_abort(&umm, rc);
close:
	pmemobj_close(ph);
	return rc;
}

static void
smd_store_close(void)
{
	if (!daos_handle_is_inval(smd_store.ss_dev_hdl)) {
		dbtree_close(smd_store.ss_dev_hdl);
		smd_store.ss_dev_hdl = DAOS_HDL_INVAL;
	}

	if (!daos_handle_is_inval(smd_store.ss_pool_hdl)) {
		dbtree_close(smd_store.ss_pool_hdl);
		smd_store.ss_pool_hdl = DAOS_HDL_INVAL;
	}

	if (!daos_handle_is_inval(smd_store.ss_tgt_hdl)) {
		dbtree_close(smd_store.ss_tgt_hdl);
		smd_store.ss_tgt_hdl = DAOS_HDL_INVAL;
	}

	if (smd_store.ss_umm.umm_pool != NULL) {
		pmemobj_close(smd_store.ss_umm.umm_pool);
		smd_store.ss_umm.umm_pool = NULL;
	}
}

static int
smd_store_open(char *fname)
{
	struct umem_attr	 uma = {0};
	struct smd_df		*smd_df;
	PMEMobjpool		*ph;
	int			 rc;

	ph = pmemobj_open(fname, POBJ_LAYOUT_NAME(smd_md_layout));
	if (ph == NULL) {
		D_ERROR("Open SMD pmemobj pool %s failed: %s\n",
			fname, pmemobj_errormsg());
		return -DER_INVAL;
	}

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_pool = ph;

	rc = umem_class_init(&uma, &smd_store.ss_umm);
	if (rc != 0) {
		pmemobj_close(ph);
		smd_store.ss_umm.umm_pool = NULL;
		return rc;
	}

	smd_df = smd_pop2df(ph);
	if (smd_df->smd_magic != SMD_DF_MAGIC) {
		D_CRIT("Unknown DF magic %x\n", smd_df->smd_magic);
		rc = -DER_DF_INVAL;
		goto error;
	}

	if (smd_df->smd_version > SMD_DF_VERSION ||
	    smd_df->smd_version < SMD_DF_VER_1) {
		D_ERROR("Unsupported DF version %d\n", smd_df->smd_version);
		rc = -DER_DF_INCOMPT;
		goto error;
	}

	/* Open device table */
	rc = dbtree_open_inplace(&smd_df->smd_dev_tab, &uma,
				 &smd_store.ss_dev_hdl);
	if (rc) {
		D_ERROR("Open SMD device table failed: "DF_RC"\n", DP_RC(rc));
		goto error;
	}

	/* Open pool table */
	rc = dbtree_open_inplace(&smd_df->smd_pool_tab, &uma,
				 &smd_store.ss_pool_hdl);
	if (rc) {
		D_ERROR("Open SMD pool table failed: "DF_RC"\n", DP_RC(rc));
		goto error;
	}

	/* Open target table */
	rc = dbtree_open_inplace(&smd_df->smd_tgt_tab, &uma,
				 &smd_store.ss_tgt_hdl);
	if (rc) {
		D_ERROR("Open SMD target table failed: "DF_RC"\n", DP_RC(rc));
		goto error;
	}

	return 0;
error:
	smd_store_close();
	return rc;
}

void
smd_fini(void)
{
	smd_lock(&smd_store);
	smd_store_close();
	smd_unlock(&smd_store);
	ABT_mutex_free(&smd_store.ss_mutex);
}

int
smd_init(const char *path)
{
	char	*fname;
	bool	 existing;
	int	 rc;

	rc = dbtree_class_register(DBTREE_CLASS_KV, 0, &dbtree_kv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		return rc;

	rc = dbtree_class_register(DBTREE_CLASS_UV, 0, &dbtree_uv_ops);
	if (rc != 0 && rc != -DER_EXIST)
		return rc;

	rc = smd_store_gen_fname(path, &fname);
	if (rc)
		return rc;

	memset(&smd_store, 0, sizeof(smd_store));
	smd_store.ss_dev_hdl = DAOS_HDL_INVAL;
	smd_store.ss_pool_hdl = DAOS_HDL_INVAL;
	smd_store.ss_tgt_hdl = DAOS_HDL_INVAL;

	rc = ABT_mutex_create(&smd_store.ss_mutex);
	if (rc != ABT_SUCCESS) {
		rc = -DER_NOMEM;
		D_FREE(fname);
		return rc;
	}

	smd_lock(&smd_store);

	rc = smd_store_check(fname, &existing);
	if (rc) {
		D_ERROR("Check SMD store %s failed. "DF_RC"\n", fname,
			DP_RC(rc));
		goto out;
	}

	/* Create the SMD store if it's not existing */
	if (!existing) {
		rc = smd_store_create(fname);
		if (rc) {
			D_ERROR("Create SMD store failed. "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}

	rc = smd_store_open(fname);
	if (rc) {
		D_ERROR("Open SMD store failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_DEBUG(DB_MGMT, "SMD store initialized\n");
out:
	smd_unlock(&smd_store);
	D_FREE(fname);

	if (rc)
		smd_fini();
	return rc;
}
