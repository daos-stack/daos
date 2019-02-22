/**
 * (C) COPYRIGHT 2018-2019 INTEL CORPORATION.
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
/**
 * DAOS Server Persistent Metadata
 * NVMe Device Persistent Metadata Storage
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <sys/sysinfo.h>

#include <daos/common.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>
#include "smd_internal.h"

static	pthread_mutex_t	mutex		= PTHREAD_MUTEX_INITIALIZER;
static  pthread_mutex_t	create_mutex	= PTHREAD_MUTEX_INITIALIZER;
static	umem_class_id_t	md_mem_class	= UMEM_CLASS_PMEM;

struct  smd_params	*smd_params_obj;
struct	smd_store	*sm_obj;

char	pool_uuid[] = "b592b744-6f4a-436e-b7d5-dbb758a35502";

void
smd_lock(int table_type)
{
	D_ASSERT(smd_params_obj != NULL);

	switch (table_type) {
	case SMD_DTAB_LOCK:
		ABT_mutex_lock(smd_params_obj->smp_dtab_mutex);
		break;
	case SMD_PTAB_LOCK:
		ABT_mutex_lock(smd_params_obj->smp_ptab_mutex);
		break;
	case SMD_STAB_LOCK:
		ABT_mutex_lock(smd_params_obj->smp_stab_mutex);
		break;
	}
}

void
smd_unlock(int table_type)
{
	D_ASSERT(smd_params_obj != NULL);

	switch (table_type) {
	case SMD_DTAB_LOCK:
		ABT_mutex_unlock(smd_params_obj->smp_dtab_mutex);
		break;
	case SMD_PTAB_LOCK:
		ABT_mutex_unlock(smd_params_obj->smp_ptab_mutex);
		break;
	case SMD_STAB_LOCK:
		ABT_mutex_unlock(smd_params_obj->smp_stab_mutex);
		break;
	}
}


static int
smd_file_path_create(const char *path, const char *fname,
		     char **smp_path, char **smp_file)
{
	int	rc = 0;

	if (path == NULL) {
		D_ERROR("Path cannot be NULL\n");
		D_GOTO(err, rc = -DER_INVAL);
	}

	rc = asprintf(smp_path,
		      "%s/daos-srv-meta", path);
	if (rc < 0) {
		D_ERROR("Could not create the SRV-MD store dir: %d\n", rc);
		D_GOTO(err, rc = -DER_NOMEM);
	}

	if (fname == NULL)
		rc = asprintf(smp_file, "%s/%s", *smp_path, SRV_NVME_META);
	else
		rc = asprintf(smp_file, "%s/%s", *smp_path, fname);

	if (rc < 0) {
		D_ERROR("Could not create the SRV-MD store file: %d\n", rc);
		D_GOTO(free_md_path, rc = -DER_NOMEM);
	}
	return 0;

free_md_path:
	D_FREE(*smp_path);
err:
	return rc;
}

static int
smd_params_create(void)
{
	int	rc = 0;

	rc = ABT_mutex_create(&smd_params_obj->smp_dtab_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(err, rc);
	}

	rc = ABT_mutex_create(&smd_params_obj->smp_ptab_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(free_dtab_mutex, rc);
	}

	rc = ABT_mutex_create(&smd_params_obj->smp_stab_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(free_ptab_mutex, rc);
	}

	smd_params_obj->smp_pool_id = strdup(pool_uuid);
	if (smd_params_obj->smp_pool_id == NULL) {
		rc = -DER_NOMEM;
		D_GOTO(free_stab_mutex, rc);
	}
	smd_params_obj->smp_mem_class = UMEM_CLASS_PMEM;
	return 0;

free_stab_mutex:
	ABT_mutex_free(&smd_params_obj->smp_stab_mutex);
free_ptab_mutex:
	ABT_mutex_free(&smd_params_obj->smp_ptab_mutex);
free_dtab_mutex:
	ABT_mutex_free(&smd_params_obj->smp_dtab_mutex);
err:
	return rc;
}

static void
srv_ndms_params_destroy(void)
{
	if (smd_params_obj->smp_path)
		D_FREE(smd_params_obj->smp_path);
	if (smd_params_obj->smp_file)
		D_FREE(smd_params_obj->smp_file);
	D_FREE(smd_params_obj->smp_pool_id);
	ABT_mutex_free(&smd_params_obj->smp_ptab_mutex);
	ABT_mutex_free(&smd_params_obj->smp_dtab_mutex);
}

static inline void
smd_obj_destroy()
{
	if (sm_obj == NULL)
		return; /* Nothing to do */

	if (!daos_handle_is_inval(sm_obj->sms_dev_tab))
		dbtree_close(sm_obj->sms_dev_tab);

	if (!daos_handle_is_inval(sm_obj->sms_pool_tab))
		dbtree_close(sm_obj->sms_pool_tab);

	if (sm_obj->sms_uma.uma_pool)
		pmemobj_close(sm_obj->sms_uma.uma_pool);

	D_FREE(sm_obj);
}

static int
smd_nvme_obj_create(PMEMobjpool *ph, struct umem_attr *uma,
		    struct smd_df *smd_df)
{
	int			rc = 0;
	struct umem_attr	*l_uma;
	struct smd_df		*l_smd_df;

	if (sm_obj != NULL)
		return 0;

	D_ALLOC_PTR(sm_obj);
	if (sm_obj == NULL)
		return -DER_NOMEM;
	if (uma == NULL) {
		l_uma = &sm_obj->sms_uma;
		l_uma->uma_id = md_mem_class;
	} else {
		l_uma = uma;
		sm_obj->sms_uma = *l_uma;
	}

	if (ph == NULL) {
		l_uma->uma_pool =
			pmemobj_open(smd_params_obj->smp_file,
				     POBJ_LAYOUT_NAME(smd_md_layout));
			if (l_uma->uma_pool == NULL) {
				D_ERROR("Error in opening the pool: %s\n",
					pmemobj_errormsg());
				D_GOTO(err, rc == -DER_NO_HDL);
			}
	} else {
		l_uma->uma_pool = ph;
	}

	rc = umem_class_init(l_uma, &sm_obj->sms_umm);
	if (rc != 0) {
		D_ERROR("Failed to instantiate umem: %d\n", rc);
		D_GOTO(err, rc);
	}

	if (smd_df == NULL)
		l_smd_df = smd_store_ptr2df(sm_obj);
	else
		l_smd_df = smd_df;

	rc = dbtree_open_inplace(&l_smd_df->smd_dev_tab_df.ndt_btr,
				 &sm_obj->sms_uma,
				 &sm_obj->sms_dev_tab);
	if (rc) {
		D_ERROR("Device Tree open failed: %d\n", rc);
		D_GOTO(err, rc);
	}

	rc = dbtree_open_inplace(&l_smd_df->smd_pool_tab_df.npt_btr,
				 &sm_obj->sms_uma,
				 &sm_obj->sms_pool_tab);
	if (rc) {
		D_ERROR("Pool Tree open failed: %d\n", rc);
		D_GOTO(err, rc);
	}

	rc = dbtree_open_inplace(&l_smd_df->smd_stream_tab_df.nst_btr,
				 &sm_obj->sms_uma,
				 &sm_obj->sms_stream_tab);
	if (rc) {
		D_ERROR("Stream tree open failed: %d\n", rc);
		D_GOTO(err, rc);
	}
	D_DEBUG(DB_MGMT, "Created SMD DRAM object: %p\n", sm_obj);
	return 0;
err:
	smd_obj_destroy();
	return rc;
}

/** DAOS per-server metadata dir for PMEM file(s) */
static int
mgmt_smd_create_dir(void)
{
	mode_t	stored_mode, mode;
	int	rc;

	stored_mode = umask(0);
	mode = S_IRWXU | S_IRWXG | S_IRWXO;

	/** create daos-meta directory if it does not exist already */
	rc = mkdir(smd_params_obj->smp_path, mode);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create daos-meta dir: %s, %s\n",
			smd_params_obj->smp_path, strerror(errno));
		umask(stored_mode);
		D_GOTO(err, rc = daos_errno2der(errno));
	}
	umask(stored_mode);
	return 0;
err:
	return rc;
}

static int
mgmt_smd_create_file(daos_size_t size)
{
	int	rc = 0;
	int	fd = -1;
	mode_t	mode;

	if (access(smd_params_obj->smp_file, F_OK) != -1) {
		D_DEBUG(DB_MGMT, "File already exists, no need to allocate");
		return -DER_EXIST;
	}

	mode = S_IRUSR | S_IWUSR;
	fd = open(smd_params_obj->smp_file, O_WRONLY | O_CREAT | O_APPEND,
		  mode);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR("Failed to create srv metadata file %s: %d\n",
			smd_params_obj->smp_file, rc);
		D_GOTO(err, rc);
	}

	rc = fallocate(fd, 0, 0, size);
	if (rc) {
		D_ERROR("Error allocate srv md file %s:"DF_U64", rc: %d\n",
			smd_params_obj->smp_file, size, rc);
		rc = daos_errno2der(errno);
	}
	close(fd);
err:
	return rc;
}

void
smd_fini(void)
{
	D_MUTEX_LOCK(&mutex);
	if (smd_params_obj) {
		srv_ndms_params_destroy();
		D_FREE(smd_params_obj);
	}
	ABT_finalize();
	smd_obj_destroy();
	D_MUTEX_UNLOCK(&mutex);
}

struct smd_store *
get_smd_store()
{
	return sm_obj;
}

/** DAOS per-server metadata pool detroy for PMEM metadata */
void
smd_remove(const char *path, const char *fname)
{

	char *l_fname;

	if (sm_obj)
		smd_obj_destroy();

	if (smd_params_obj == NULL) {
		char	*l_path;
		int	rc;

		rc = smd_file_path_create(path, fname, &l_path, &l_fname);
		if (rc) {
			D_ERROR("Error creating file name: %d\n", rc);
			return;
		}
		D_FREE(l_path);
	} else {
		l_fname = smd_params_obj->smp_file;
	}

	if (remove(l_fname))
		D_ERROR("While deleting file from PMEM: %s\n",
			strerror(errno));

	if (smd_params_obj != NULL)
		smd_fini();
	else
		D_FREE(l_fname);
}

/** DAOS per-server metadata pool creation for PMEM metadata */
int
smd_nvme_create_md_store(const char *path, const char *fname,
			 daos_size_t size)
{
	int			rc = 0;
	static int		md_store_created;
	PMEMobjpool		*ph = NULL;
	struct smd_df		*smd_df = NULL;
	struct umem_attr	*l_uma = NULL;
	struct umem_attr	uma;


	if (md_store_created) {
		D_DEBUG(DB_MGMT, "SRV MD store file %s, already exists\n",
			smd_params_obj->smp_file);
		return 0;
	}

	D_MUTEX_LOCK(&create_mutex);

	if (md_store_created)
		D_GOTO(exit, rc);

	if (smd_params_obj == NULL) {
		D_ERROR("SMD store has not be init, call smd_init\n");
		D_GOTO(exit, rc);
	}

	rc = smd_file_path_create(path, fname, &smd_params_obj->smp_path,
				  &smd_params_obj->smp_file);
	if (rc < 0) {
		D_ERROR("File path creation failed: %d\n", rc);
		D_GOTO(exit, rc);
	}

	rc = mgmt_smd_create_dir();
	if (rc == -DER_EXIST)
		D_GOTO(err_obj_create, rc = 0);
	if (rc < 0)
		D_GOTO(exit, rc);

	if (size == (daos_size_t)-1)
		size = SMD_FILE_SIZE;

	rc = mgmt_smd_create_file(size);
	if (rc == -DER_EXIST)
		D_GOTO(err_obj_create, rc = 0);
	if (rc < 0)
		D_GOTO(exit, rc);

	ph = pmemobj_create(smd_params_obj->smp_file,
			    POBJ_LAYOUT_NAME(smd_md_layout), 0, 0666);
	if (!ph) {
		rc = daos_errno2der(errno);
		D_ERROR("Failed to create pool %s size="DF_U64", errno:%d\n",
			smd_params_obj->smp_file, size, rc);
		D_GOTO(err_pool_destroy, rc);
	}

	TX_BEGIN(ph) {
		smd_df  = pmempool_pop2df(ph);
		pmemobj_tx_add_range_direct(smd_df, sizeof(*smd_df));
		memset(smd_df, 0, sizeof(*smd_df));

		memset(&uma, 0, sizeof(uma));
		uma.uma_id = md_mem_class;
		uma.uma_pool = ph;
		l_uma = &uma;

		uuid_parse(pool_uuid, smd_df->smd_id);
		rc = smd_nvme_md_dtab_create(&uma, &smd_df->smd_dev_tab_df);
		if (rc != 0)
			pmemobj_tx_abort(EFAULT);
		rc = smd_nvme_md_ptab_create(&uma,
					     &smd_df->smd_pool_tab_df);
		if (rc != 0)
			pmemobj_tx_abort(EFAULT);
		rc = smd_nvme_md_stab_create(&uma,
					     &smd_df->smd_stream_tab_df);
		if (rc != 0)
			pmemobj_tx_abort(EFAULT);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Initialize md pool root error:%d\n", rc);
	} TX_END

err_obj_create:
	rc = smd_nvme_obj_create(ph, l_uma, smd_df);
	if (rc != 0) {
		D_ERROR("Failed to create sm_obj: %p\n", sm_obj);
		D_GOTO(exit, rc);
	}
	md_store_created = 1;
	D_GOTO(exit, rc);

err_pool_destroy:
	smd_remove(path, fname);
exit:
	D_MUTEX_UNLOCK(&create_mutex);
	return rc;
}

int
smd_create_initialize(const char *path, const char *fname, daos_size_t size)
{
	int		rc  = 0;
	static	int	is_smd_init;

	if (is_smd_init) {
		D_DEBUG(DB_MGMT, "SRV MD store already init\n");
		return rc;
	}
	D_MUTEX_LOCK(&mutex);

	if (is_smd_init)
		D_GOTO(exit, rc);

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_MUTEX_UNLOCK(&mutex);
		return rc;
	}

	D_ALLOC_PTR(smd_params_obj);
	if (smd_params_obj == NULL)
		D_GOTO(exit, rc);

	rc = smd_params_create();
	if (rc) {
		D_ERROR("Failure: Creating in-memory data structs: %d\n", rc);
		D_GOTO(exit, rc);
	}

	rc = smd_nvme_md_tables_register();
	if (rc) {
		D_ERROR("registering tables failure\n");
		D_GOTO(exit, rc);
	}

	rc = smd_nvme_create_md_store(path, fname, size);
	if (rc) {
		D_ERROR("creating an MD store\n");
		D_GOTO(exit, rc);
	}
	is_smd_init = 1;
exit:
	D_MUTEX_UNLOCK(&mutex);
	if (rc) {
		D_DEBUG(DB_MGMT, "Error initializing smd store\n");
		smd_fini();
	} else {
		D_DEBUG(DB_MGMT, "Finished initializing smd store\n");
	}
	return rc;
}
