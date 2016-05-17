/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of the DAOS server. It implements the DAOS service
 * including:
 * - network setup
 * - start/stop service threads
 * - bind service threads to core/NUMA node
 */

#include <pthread.h>
#include <sched.h>
#include <libcgroup.h>
#include <numa.h>
#include <sys/sysinfo.h>

#include <daos_errno.h>
#include <daos/list.h>
#include "dss_internal.h"

/* The structure maintains Cgroup per NUMA node */
struct dss_cgroups {
	struct cgroup **dc_cgroup;	/* one cgroup per NUMA */
	int		dc_count;	/* cgroup count */
	pthread_mutex_t	dc_lock;	/* lock for operating dc_cgroup */
	int		dc_lock_init:1;
};

/* cgroup structure per physical node */
struct dss_cgroups *dcgroups;

/* manage structure per thread */
struct dss_thread {
	pthread_t	 dt_id;
	pthread_cond_t	 dt_start_cond;
	pthread_mutex_t	 dt_lock;
	struct cgroup	*dt_cgroup;
	daos_list_t	dt_list;
};

/* Manage the services threads */
struct dss_threads {
	daos_list_t		dt_threads_list;
	pthread_mutex_t		dt_lock;
	int			dt_lock_init:1;
};

struct dss_threads *dthreads;

/**
 * Cleanup Cgroup.
 *
 * Finialize Cgroup of the node, includes delete various
 * cgroup structure, destroy lock and free cgroup
 *
 */
static void
dss_cgroup_fini(void)
{
	int i;

	if (dcgroups == NULL)
		return;

	if (dcgroups->dc_lock_init)
		pthread_mutex_lock(&dcgroups->dc_lock);

	for (i = 0; i < dcgroups->dc_count; i++) {
		int rc;

		if (dcgroups->dc_cgroup[i] == NULL)
			continue;

		rc = cgroup_delete_cgroup_ext(dcgroups->dc_cgroup[i],
					      CGFLAG_DELETE_RECURSIVE);
		if (rc != 0)
			D_ERROR("Can not delete %dth cgroup: %s\n", i,
				cgroup_strerror(rc));

		cgroup_free(&dcgroups->dc_cgroup[i]);
	}
	if (dcgroups->dc_cgroup != NULL)
		D_FREE(dcgroups->dc_cgroup, sizeof(dcgroups->dc_cgroup[0]) *
					    dcgroups->dc_count);

	dcgroups->dc_count = 0;
	if (dcgroups->dc_lock_init) {
		pthread_mutex_unlock(&dcgroups->dc_lock);
		pthread_mutex_destroy(&dcgroups->dc_lock);
	}
}

/* Help to analyse the cpu core id for numa. */
struct numa_core_id {
	int	nci_current_start;
	int	nci_current_end;
	char	nci_cores[256];
};

static void
append_cores_to_cpuset_str(char *ptr, int start, int end)
{
	char value[16];

	if (start == end)
		snprintf(value, 16, "%d", start);
	else
		snprintf(value, 16, "%d-%d", start, end);

	if (strlen(ptr) == 0)
		sprintf(ptr, "%s", value);
	else
		sprintf(ptr + strlen(ptr), ",%s", value);
}

/**
 * Init cgroup
 *
 * Initialize cgroup per NUMA node.
 *
 * \param[in] set	CPU set of the service node.
 *
 * \retval	0 if initialization succeeds.
 * \retval	negative errno if initialization fails.
 */
static int
dss_cgroup_init(cpu_set_t *set)
{
	char			 srv_group_name[24];
	struct numa_core_id	*nc_ids = NULL;
	int			 numa_id;
	int			 rc;
	int			 i;

	if (numa_available() < 0) {
		D_ERROR("Do not support numa!\n");
		return -DER_NOSYS;
	}

	rc = cgroup_init();
	if (rc != 0) {
		D_ERROR("Can not initialize cgroup, check your kernel: %s.\n",
			cgroup_strerror(rc));
		return -DER_INVAL;
	}

	rc = pthread_mutex_init(&dcgroups->dc_lock, NULL);
	if (rc != 0) {
		D_ERROR("Can not initialize mutex: rc %d\n", errno);
		return -DER_INVAL;
	}
	dcgroups->dc_lock_init = 1;

	/* Allocate enough cgroup for numa node */

	D_ALLOC(dcgroups->dc_cgroup,
		sizeof(dcgroups->dc_cgroup[0]) * (numa_max_node() + 1));
	if (dcgroups->dc_cgroup == NULL) {
		D_ERROR("Can not allocate cgroup_array.\n");
		return -DER_NOMEM;
	}
	dcgroups->dc_count = numa_max_node() + 1;

	D_ALLOC(nc_ids, dcgroups->dc_count * sizeof(*nc_ids));
	if (nc_ids == NULL) {
		D_ERROR("Can not allocate nc_ids.\n");
		return -DER_NOMEM;
	}

	/* initialize the numa core id array */
	for (i = 0; i < dcgroups->dc_count; i++) {
		nc_ids[i].nci_current_start = -1;
		nc_ids[i].nci_current_end = -1;
	}

	/* formalize core ids string for each NUMA node */
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, set))
			continue;

		numa_id = numa_node_of_cpu(i);
		if (numa_id < 0) {
			D_ERROR("Can not get numa node\n");
			rc = -DER_INVAL;
			break;
		}
		D_ASSERTF(numa_id < dcgroups->dc_count, "numa_id %d count %d\n",
			  numa_id, dcgroups->dc_count);

		if (nc_ids[numa_id].nci_current_start == -1 &&
		   nc_ids[numa_id].nci_current_end == -1) {
			nc_ids[numa_id].nci_current_start = i;
			nc_ids[numa_id].nci_current_end = i;
		} else if (nc_ids[numa_id].nci_current_end + 1 == i) {
			nc_ids[numa_id].nci_current_end++;
		} else {
			append_cores_to_cpuset_str(nc_ids[numa_id].nci_cores,
					nc_ids[numa_id].nci_current_start,
					nc_ids[numa_id].nci_current_end);

			nc_ids[numa_id].nci_current_start = i;
			nc_ids[numa_id].nci_current_end = i;
		}
	}
	if (rc < 0)
		goto out;

	/* Walk through the array again to add left cores to cpuset string */
	for (i = 0; i < dcgroups->dc_count; i++) {
		if (nc_ids[i].nci_current_start == -1 ||
		    nc_ids[i].nci_current_end == -1)
			continue;
		append_cores_to_cpuset_str(nc_ids[i].nci_cores,
				nc_ids[i].nci_current_start,
				nc_ids[i].nci_current_end);
	}

	/* Create cgroup by these core ids per NUMA node */
	for (i = 0; i < dcgroups->dc_count; i++) {
		struct cgroup_controller	*cgc;
		char				 value[6];

		if (strlen(nc_ids[i].nci_cores) == 0)
			continue;

		sprintf(srv_group_name, "/daos_server/numa_%d", i);
		dcgroups->dc_cgroup[i] =
				cgroup_new_cgroup(srv_group_name);

		if (dcgroups->dc_cgroup[i] == NULL) {
			D_ERROR("Can not create new group\n");
			rc = -DER_INVAL;
			break;
		}

		cgc = cgroup_add_controller(dcgroups->dc_cgroup[i], "cpuset");
		if (cgc == NULL) {
			D_ERROR("Can not add cpuset\n");
			rc = -DER_INVAL;
			break;
		}

		sprintf(value, "%d", i);
		rc = cgroup_add_value_string(cgc, "cpuset.mems", value);
		if (rc) {
			D_ERROR("Can not add mems: %s\n", cgroup_strerror(rc));
			rc = -DER_INVAL;
			break;
		}

		D_DEBUG(DF_SERVER, "set numa %d core %s\n", i,
			nc_ids[i].nci_cores);
		rc = cgroup_add_value_string(cgc, "cpuset.cpus",
					     nc_ids[i].nci_cores);
		if (rc) {
			D_ERROR("Can not add cpus: %s\n", cgroup_strerror(rc));
			rc = -DER_INVAL;
			break;
		}

		rc = cgroup_create_cgroup(dcgroups->dc_cgroup[i], 0);
		if (rc) {
			D_ERROR("Can not create cgroup: %s\n",
				cgroup_strerror(rc));
			rc = -DER_INVAL;
			break;
		}
	}

out:
	if (nc_ids != NULL)
		D_FREE(nc_ids, dcgroups->dc_count * sizeof(*nc_ids));

	return rc;
}

void
dss_srv_handler_cleanup(void *param)
{
	struct dss_thread_local_storage	*dtc;
	struct dss_module_info		*dmi;
	int				rc;

	D_ERROR("cleanup in progress\n");
	dtc = param;
	dmi = (struct dss_module_info *)
	      dss_module_key_get(dtc, &daos_srv_modkey);
	D_ASSERT(dmi != NULL);
	rc = dtp_context_destroy(dmi->dmi_ctx, true);
	if (rc)
		D_ERROR("failed to destroy context: %d\n", rc);
}

int
dss_progress_cb(void *arg)
{
	/** cancellation point */
	pthread_testcancel();

	/** no cancel happened, continue running */
	return 0;
}

/**
 *
 The handling process would like
 *
 * 1. The service thread for each NUMA node will create a DTP context
 * by its own NUMA id.
 *
 * 2. Then polls the request from DTP context
 **/
static void *
dss_srv_handler(void *arg)
{
	struct dss_thread	*dthread = (struct dss_thread *)arg;
	struct cgroup		*cgroup = dthread->dt_cgroup;
	int			 oldState;
	struct dss_thread_local_storage *dtc;
	struct dss_module_info	*dmi;
	int			 rc;

	/* ignore the cancel request until after initialization */
	pthread_mutex_lock(&dthread->dt_lock);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

	/* attach to cgroup */
	rc = cgroup_attach_task(cgroup);
	if (rc != 0) {
		D_ERROR("attach task to cgroup fails: %s\n",
			strerror(errno));
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	/* initialize thread-local storage for this thread */
	dtc = dss_tls_init(DAOS_SERVER_TAG);
	if (dtc == NULL) {
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	/* create private transport context */
	rc = dtp_context_create(NULL, &dmi->dmi_ctx);
	if (rc != 0) {
		D_ERROR("Can not create dtp ctxt: rc = %d\n", rc);
		pthread_cond_signal(&dthread->dt_start_cond);
		pthread_mutex_unlock(&dthread->dt_lock);
		return NULL;
	}

	/* register clean-up routine called on cancellation point */
	pthread_cleanup_push(dss_srv_handler_cleanup, (void *)dtc);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldState);
	dthread->dt_id = pthread_self();

	/* wake up caller */
	pthread_cond_signal(&dthread->dt_start_cond);
	pthread_mutex_unlock(&dthread->dt_lock);

	/* main service loop processing incoming request */
	rc = dtp_progress(dmi->dmi_ctx, -1, dss_progress_cb, NULL);
	D_ERROR("service thread exited from progress with %d\n", rc);

	pthread_cleanup_pop(0);
	dtp_context_destroy(dmi->dmi_ctx, true);
	return NULL;
}

/**
 * Start threads by cgroup.
 *
 * Start number of threads, which will evenly distributed all
 * of cgroup.
 *
 * \param[in] number	number of threads to be created.
 *
 * \retval	= 0 if starting succeeds.
 * \retval	negative errno if starting fails.
 */
static int
dss_add_threads(int number)
{
	struct cgroup	*cgroup = NULL;
	int		 idx = 0;
	int		 rc = 0;
	int i;

	if (number == 0)
		return 0;

	pthread_mutex_lock(&dcgroups->dc_lock);

	/* find first avaible cgroup */
	for (idx = 0; idx < dcgroups->dc_count; idx++) {
		cgroup = dcgroups->dc_cgroup[idx];
		if (cgroup != NULL)
			break;
	}
	D_ASSERT(cgroup != NULL);

	pthread_mutex_lock(&dthreads->dt_lock);
	for (i = 0; i < number; i++) {
		struct dss_thread *dthread;
		pthread_t	  pid;

		D_ALLOC_PTR(dthread);
		if (dthread == NULL) {
			D_ERROR("Can not allocate dthread.\n");
			goto unlock;
		}
		pthread_cond_init(&dthread->dt_start_cond, NULL);
		pthread_mutex_init(&dthread->dt_lock, NULL);
		DAOS_INIT_LIST_HEAD(&dthread->dt_list);

		dthread->dt_cgroup = cgroup;
		pthread_mutex_lock(&dthread->dt_lock);
		rc = pthread_create(&pid, NULL, dss_srv_handler, dthread);
		if (rc != 0) {
			D_ERROR("Can not create thread%d: rc = %d\n",
				i, errno);
			pthread_mutex_unlock(&dthread->dt_lock);
			rc = -DER_INVAL;
			goto unlock;
		}

		pthread_cond_wait(&dthread->dt_start_cond, &dthread->dt_lock);
		/* If one threads does not get into the server loop
		 * (see dss_srv_handler()), let's not exit, dss_threads_fini()
		 * will take care this dthread */
		if (dthread->dt_id == 0) {
			D_ERROR("Can not start thread %d\n", i);
			pthread_mutex_unlock(&dthread->dt_lock);
			pthread_mutex_destroy(&dthread->dt_lock);
			pthread_cond_destroy(&dthread->dt_start_cond);
			D_FREE_PTR(dthread);
		} else {
			daos_list_add_tail(&dthread->dt_list,
					   &dthreads->dt_threads_list);
			pthread_mutex_unlock(&dthread->dt_lock);
		}

		/* get next avaible cgroup */
		cgroup = NULL;
		while (cgroup == NULL) {
			if (idx >= dcgroups->dc_count - 1)
				idx = 0;
			else
				idx++;
			cgroup = dcgroups->dc_cgroup[idx];
		}
		D_ASSERTF(cgroup != NULL, "i %d dc_count %d number %d idx %d\n",
			  i, dcgroups->dc_count, number, idx);
	}

unlock:
	pthread_mutex_unlock(&dthreads->dt_lock);
	pthread_mutex_unlock(&dcgroups->dc_lock);
	return rc;
}

static void
dss_threads_fini()
{
	struct dss_thread	*dthread;
	struct dss_thread	*tmp;
	int			 rc;

	if (dthreads == NULL)
		return;

	if (dthreads->dt_lock_init)
		pthread_mutex_lock(&dthreads->dt_lock);

	/* issue cancel and wait for each thread to complete */
	daos_list_for_each_entry_safe(dthread, tmp, &dthreads->dt_threads_list,
				      dt_list) {
		pthread_mutex_lock(&dthread->dt_lock);
		daos_list_del(&dthread->dt_list);

		if (dthread->dt_id != 0) {
			D_DEBUG(DF_SERVER, "stopping %ld\n", dthread->dt_id);
			rc = pthread_cancel(dthread->dt_id);
			D_DEBUG(DF_SERVER, "canceled %ld\n", dthread->dt_id);
			if (rc) {
				D_ERROR("Failed to kill %ld thread: rc = %d\n",
					dthread->dt_id, rc);
			} else {
				/* We need to wait for the thread to exit, then
				 * it is safe to delete the cgroup later */
				pthread_join(dthread->dt_id, NULL);
				dthread->dt_id = 0;
			}
		}

		pthread_mutex_unlock(&dthread->dt_lock);

		/* housekeeping ... */
		pthread_mutex_destroy(&dthread->dt_lock);
		pthread_cond_destroy(&dthread->dt_start_cond);
		D_FREE_PTR(dthread);
	}

	/* release thread-local storage */
	rc = pthread_key_delete(dss_tls_key);
	if (rc)
		D_ERROR("failed to delete dtc: %d\n", rc);

	if (dthreads->dt_lock_init) {
		pthread_mutex_unlock(&dthreads->dt_lock);
		pthread_mutex_destroy(&dthreads->dt_lock);
	}

	D_FREE_PTR(dthreads);
}

#define INIT_THREADS_PER_NUMA	1
static int
dss_threads_init()
{
	int	thread_count;
	int	rc = 0;

	/* Init dthreads structure */
	rc = pthread_mutex_init(&dthreads->dt_lock, NULL);
	if (rc != 0) {
		D_ERROR("Can not initialize mutex: rc %d\n", errno);
		return -DER_INVAL;
	}
	dthreads->dt_lock_init = 1;

	DAOS_INIT_LIST_HEAD(&dthreads->dt_threads_list);
	/* start one thread by core */
	thread_count = get_nprocs() * INIT_THREADS_PER_NUMA;

	/* initialize thread-local storage */
	rc = pthread_key_create(&dss_tls_key, dss_tls_fini);
	if (rc) {
		D_ERROR("failed to create dtc: %d\n", rc);
		return -DER_NOMEM;
	}

	/* start the threads on dthreads */
	rc = dss_add_threads(thread_count);
	return rc;
}

int
dss_srv_fini()
{
	dss_threads_fini();
	if (dthreads != NULL)
		D_FREE_PTR(dthreads);

	dss_cgroup_fini();
	if (dcgroups != NULL)
		D_FREE_PTR(dcgroups);

	return 0;
}

static void *
dss_module_info_init(const struct dss_thread_local_storage *dtls,
		     struct dss_module_key *key)
{
	struct dss_module_info *info;

	D_ALLOC_PTR(info);

	return info;
}

static void
dss_module_info_fini(const struct dss_thread_local_storage *dtls,
		     struct dss_module_key *key, void *data)
{
	struct dss_module_info *info = (struct dss_module_info *)data;

	D_FREE_PTR(info);
}

struct dss_module_key daos_srv_modkey = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dss_module_info_init,
	.dmk_fini = dss_module_info_fini,
};

static int
dss_load_cpuset(cpu_set_t *set)
{
	int rc;

	/* xxx sigh, hope there will be a more higher level
	 * library to do this */
	CPU_ZERO(set);
	rc = sched_getaffinity(0, sizeof(*set), set);
	return rc;
}

int
dss_srv_init()
{
	int		rc;
	cpu_set_t	mask;

	rc = dss_load_cpuset(&mask);
	if (rc != 0) {
		D_ERROR("load cpuset failed: rc = %d\n", errno);
		return -DER_INVAL;
	}

	if (dthreads != NULL)
		dss_threads_fini();

	if (dcgroups != NULL)
		dss_cgroup_fini();

	D_ALLOC_PTR(dcgroups);
	if (dcgroups == NULL) {
		D_ERROR("allocate dss_cgroup fails\n");
		return -DER_NOMEM;
	}

	rc = dss_cgroup_init(&mask);
	if (rc != 0) {
		D_ERROR("Can not initialize cgroup: rc %d\n", rc);
		goto out;
	}

	D_ALLOC_PTR(dthreads);
	if (dthreads == NULL) {
		D_ERROR("allocate dss_thread fails\n");
		goto out;
	}

	dss_register_key(&daos_srv_modkey);

	/* how many threads should be started here */
	rc = dss_threads_init();
	if (rc != 0) {
		D_ERROR("Can not start threads: rc = %d\n", rc);
		goto out;
	}

out:
	if (rc != 0)
		dss_srv_fini();

	return rc;
}
