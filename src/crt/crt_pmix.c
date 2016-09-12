/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of CaRT. It implements the interface with PMIx.
 */

#include <crt_internal.h>

int
crt_pmix_init(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	pmix_value_t		*val;
	pmix_proc_t		 proc;
	pmix_info_t		*info;
	bool			 flag = true;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	C_ASSERT(grp_gdata->gg_pmix_inited == 0);
	C_ASSERT(grp_gdata->gg_pmix == NULL);

	C_ALLOC_PTR(pmix_gdata);
	if (pmix_gdata == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	rc = PMIx_Init(&pmix_gdata->pg_proc, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx_Init failed, rc: %d.\n", rc);
		C_GOTO(out, rc = -CER_PMIX);
	}

	/* get universe size */
	PMIX_PROC_CONSTRUCT(&proc);
	strncpy(proc.nspace, pmix_gdata->pg_proc.nspace, PMIX_MAX_NSLEN);
	proc.rank = PMIX_RANK_WILDCARD;
	PMIX_INFO_CREATE(info, 1);
	PMIX_INFO_LOAD(&info[0], PMIX_IMMEDIATE, &flag, PMIX_BOOL);
	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, info, 1, &val);
	if (rc != PMIX_SUCCESS) {
		PMIX_INFO_FREE(info, 1);
		PMIX_PROC_DESTRUCT(&proc);
		C_ERROR("PMIx ns %s rank %d, PMIx_Get universe size failed, "
			"rc: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, rc);
		C_GOTO(out, rc = -CER_PMIX);
	}
	if (val->type != PMIX_UINT32) {
		PMIX_INFO_FREE(info, 1);
		PMIX_PROC_DESTRUCT(&proc);
		C_ERROR("PMIx ns %s rank %d, PMIx_Get universe size returned "
			"wrong type: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, val->type);
		C_GOTO(out, rc = -CER_PMIX);
	}
	pmix_gdata->pg_univ_size = val->data.uint32;
	PMIX_VALUE_RELEASE(val);

	/* get the number of apps in this job */
	rc = PMIx_Get(&proc, PMIX_JOB_NUM_APPS, info, 1, &val);
	PMIX_INFO_FREE(info, 1);
	PMIX_PROC_DESTRUCT(&proc);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d: PMIx_Get num_apps failed, "
			"rc: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, rc);
		C_GOTO(out, rc = -CER_PMIX);
	}
	if (val->type != PMIX_UINT32) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Get num_apps returned wrong "
			"type: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, val->type);
		C_GOTO(out, rc = -CER_PMIX);
	}
	pmix_gdata->pg_num_apps = val->data.uint32;
	PMIX_VALUE_RELEASE(val);

	grp_gdata->gg_pmix = pmix_gdata;
	grp_gdata->gg_pmix_inited = 1;

out:
	if (rc != 0) {
		C_ERROR("crt_pmix_init failed, rc: %d.\n", rc);
		if (pmix_gdata != NULL)
			C_FREE_PTR(pmix_gdata);
	}
	return rc;
}

int
crt_pmix_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(grp_gdata->gg_pmix != NULL);

	pmix_gdata = grp_gdata->gg_pmix;

	rc = PMIx_Finalize(NULL, 0);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Finalize failed, rc: %d.\n",
			pmix_gdata->pg_proc.nspace, pmix_gdata->pg_proc.rank,
			rc);
		C_GOTO(out, rc = -CER_PMIX);
	}

	C_FREE_PTR(pmix_gdata);
	grp_gdata->gg_pmix_inited = 0;
	grp_gdata->gg_pmix = NULL;

out:
	return rc;
}

int
crt_pmix_fence(void)
{
	pmix_proc_t	*myproc;
	pmix_proc_t	proc;
	pmix_info_t	*info = NULL;
	bool		flag;
	int		rc = 0;

	/*
	if (!crt_gdata.cg_grp_inited) {
		C_ERROR("crt group un-initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	*/
	myproc = &crt_gdata.cg_grp->gg_pmix->pg_proc;

	PMIX_PROC_CONSTRUCT(&proc);
	strncpy(proc.nspace, myproc->nspace, PMIX_MAX_NSLEN);
	proc.rank = PMIX_RANK_WILDCARD;
	PMIX_INFO_CREATE(info, 1);
	if (info == NULL) {
		C_ERROR("PMIX_INFO_CREATE failed.\n");
		C_GOTO(out, rc = -CER_PMIX);
	}
	flag = true;
	PMIX_INFO_LOAD(info, PMIX_COLLECT_DATA, &flag, PMIX_BOOL);

	rc = PMIx_Fence(&proc, 1, info, 1);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Fence failed, rc: %d.\n",
			      myproc->nspace, myproc->rank, rc);
		C_GOTO(out, rc = -CER_PMIX);
	}

out:
	if (info != NULL)
		PMIX_INFO_FREE(info, 1);
	return rc;
}

int
crt_pmix_assign_rank(struct crt_grp_priv *grp_priv)
{
	pmix_proc_t		*myproc = NULL;
	struct crt_pmix_gdata	*pmix_gdata;
	pmix_info_t		*info;
	pmix_pdata_t		*pdata;
	pmix_value_t		*val;
	char			*unpublish_key[2];
	pmix_persistence_t	 persistence;
	pmix_data_range_t	 range;
	pmix_proc_t		 proc;
	bool			 flag = true;
	int			 i, rc = 0;

	C_ASSERT(grp_priv != NULL);
	/*
	if (!crt_gdata.cg_grp_inited) {
		C_ERROR("crt group un-initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	*/

	C_ASSERT(crt_gdata.cg_grp != NULL);
	pmix_gdata = crt_gdata.cg_grp->gg_pmix;
	myproc = &pmix_gdata->pg_proc;
	unpublish_key[0] = NULL;
	unpublish_key[1] = NULL;

	/* get incorrect result (grp_priv->gp_self = -1), so disable it */
	if (/* pmix_gdata->pg_num_apps == 1 */ 0) {
		PMIX_PROC_CONSTRUCT(&proc);
		strncpy(proc.nspace, myproc->nspace, PMIX_MAX_NSLEN);
		proc.rank = PMIX_RANK_WILDCARD;
		PMIX_INFO_CREATE(info, 1);
		PMIX_INFO_LOAD(&info[0], PMIX_IMMEDIATE, &flag, PMIX_BOOL);
		rc = PMIx_Get(&proc, PMIX_APP_SIZE, info, 1, &val);
		if (rc != PMIX_SUCCESS) {
			PMIX_INFO_FREE(info, 1);
			PMIX_PROC_DESTRUCT(&proc);
			C_ERROR("PMIx ns %s rank %d, PMIx_Get failed,rc: %d.\n",
				myproc->nspace, myproc->rank, rc);
			C_GOTO(out, rc = -CER_PMIX);
		}
		grp_priv->gp_size = val->data.uint32;
		PMIX_VALUE_RELEASE(val);

		rc = PMIx_Get(myproc, PMIX_APP_RANK, info, 1, &val);
		PMIX_INFO_FREE(info, 1);
		PMIX_PROC_DESTRUCT(&proc);
		if (rc != PMIX_SUCCESS) {
			C_ERROR("PMIx ns %s rank %d, PMIx_Get failed,rc: %d.\n",
				myproc->nspace, myproc->rank, rc);
			C_GOTO(out, rc = -CER_PMIX);
		}
		grp_priv->gp_self = val->data.uint32;
		PMIX_VALUE_RELEASE(val);
		C_GOTO(out, rc = 0);
	}

	/*
	 * every process publishes its own address string using
	 * (PMIx_rank, setname/addrString)
	 */
	PMIX_INFO_CREATE(info, 3);
	snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "%s-%d-psname",
		myproc->nspace, myproc->rank);
	unpublish_key[0] = strndup(info[0].key, PMIX_MAX_KEYLEN);
	if (!unpublish_key[0]) {
		PMIX_INFO_FREE(info, 3);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	info[0].value.type = PMIX_STRING;
	info[0].value.data.string = strndup(grp_priv->gp_pub.cg_grpid,
					    CRT_GROUP_ID_MAX_LEN);
	if (info[0].value.data.string == NULL) {
		PMIX_INFO_FREE(info, 1);
		free(unpublish_key[0]);
		C_GOTO(out, rc = -CER_NOMEM);
	}

	persistence = PMIX_PERSIST_PROC;
	range = PMIX_RANGE_NAMESPACE;
	PMIX_INFO_LOAD(&info[1], PMIX_PERSISTENCE, &persistence,
			PMIX_UINT);
	PMIX_INFO_LOAD(&info[2], PMIX_RANGE, &range, PMIX_UINT);
	rc = PMIx_Publish(info, 3);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Publish failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		PMIX_INFO_FREE(info, 3);
		free(unpublish_key[0]);
		C_GOTO(out, rc = -CER_PMIX);
	}
	PMIX_INFO_FREE(info, 3);

	/* call fence to ensure the data is received */
	rc = crt_pmix_fence();
	if (rc != 0) {
		C_ERROR("PMIx ns %s rank %d, crt_pmix_fence failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		C_GOTO(out, rc);
	}

	/* loop over universe size, parse address string and set size */
	PMIX_PDATA_CREATE(pdata, 1);
	for (i = 0; i < pmix_gdata->pg_univ_size; i++) {
		/* generate the key to query my process set name */
		snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "%s-%d-psname",
			 myproc->nspace, i);

		rc = PMIx_Lookup(pdata, 1, NULL, 0);
		if (rc != PMIX_SUCCESS) {
			C_ERROR("PMIx ns %s rank %d, PMIx_Lookup failed, "
				"rc: %d.\n", myproc->nspace, myproc->rank, rc);
			PMIX_PDATA_FREE(pdata, 1);
			free(unpublish_key[0]);
			C_GOTO(out, rc = -CER_PMIX);
		}

		if (i == myproc->rank)
			grp_priv->gp_self = grp_priv->gp_size;

		if (strncmp(grp_priv->gp_pub.cg_grpid,
			    pdata[0].value.data.string,
			    CRT_GROUP_ID_MAX_LEN) == 0) {
			grp_priv->gp_size++;
		}
	}
	PMIX_PDATA_FREE(pdata, 1);
	rc = PMIx_Unpublish(unpublish_key, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Unpublish failed, rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		C_GOTO(out, rc = -CER_PMIX);
	}
	free(unpublish_key[0]);

out:
	if (rc == 0)
		C_DEBUG("crt_pmix_assign_rank get size %d, self %d.\n",
			grp_priv->gp_size, grp_priv->gp_self);
	else if (myproc != NULL)
		C_ERROR("PMIx ns %s rank %d, crt_pmix_assign_rank failed, "
			"rc: %d.\n", myproc->nspace, myproc->rank, rc);
	return rc;
}

/*
 * Publish data to PMIx about the local process set.  Only publish if the local
 * process set is a service process set, all processes publish their own URI
 * and then process[0] also publishes the size.  Process sets attempting to
 * attach can then read the size to detect if the process set exists.
 */
int
crt_pmix_publish_self(struct crt_grp_priv *grp_priv)
{
	pmix_proc_t		*myproc = NULL;
	struct crt_pmix_gdata	*pmix_gdata;
	pmix_info_t		*info;
	/*
	pmix_persistence_t	persistence;
	pmix_data_range_t	range;
	*/
	int			nkeys = 1;
	int			rc;

	C_ASSERT(grp_priv != NULL);
	/*
	if (!crt_gdata.cg_grp_inited) {
		C_ERROR("crt group un-initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	*/

	C_ASSERT(crt_gdata.cg_grp != NULL);
	pmix_gdata = crt_gdata.cg_grp->gg_pmix;
	myproc = &pmix_gdata->pg_proc;

	if (!grp_priv->gp_local) {
		C_ERROR("cannot publish self on non-local group.\n");
		C_GOTO(out, rc = -CER_NO_PERM);
	}
	if (!grp_priv->gp_service) {
		C_DEBUG("ignore publish self on non-service group.\n");
		C_GOTO(out, rc = 0);
	}

	PMIX_INFO_CREATE(info, nkeys);
	if (!info) {
		C_ERROR("PMIX_INFO_CREATE failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	/*
	persistence = PMIX_PERSIST_PROC;
	range = PMIX_RANGE_SESSION;
	PMIX_INFO_LOAD(&info[1], PMIX_PERSISTENCE, &persistence, PMIX_UINT);
	PMIX_INFO_LOAD(&info[2], PMIX_RANGE, &range, PMIX_UINT);
	*/
	snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "pmix-%s-%d-uri",
		 grp_priv->gp_pub.cg_grpid, grp_priv->gp_self);
	info[0].value.type = PMIX_STRING;
	info[0].value.data.string = strndup(crt_gdata.cg_addr,
					    CRT_ADDR_STR_MAX_LEN);
	if (info[0].value.data.string == NULL) {
		PMIX_INFO_FREE(info, nkeys);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	rc = PMIx_Publish(info, nkeys);
	if (rc != PMIX_SUCCESS) {
		PMIX_INFO_FREE(info, nkeys);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	/* C_DEBUG("PMIx_Publish %s, rc: %d.\n", info[0].key, rc); */
	PMIX_INFO_FREE(info, nkeys);

	if (grp_priv->gp_self == 0) {
		PMIX_INFO_CREATE(info, nkeys);
		if (!info)
			return -CER_NOMEM;
		/*
		persistence = PMIX_PERSIST_PROC;
		range = PMIX_RANGE_SESSION;
		PMIX_INFO_LOAD(&info[1], PMIX_PERSISTENCE, &persistence,
				PMIX_UINT);
		PMIX_INFO_LOAD(&info[2], PMIX_RANGE, &range, PMIX_UINT);
		*/
		snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "pmix-%s-size",
			 grp_priv->gp_pub.cg_grpid);
		info[0].value.type = PMIX_UINT32;
		info[0].value.data.uint32 = grp_priv->gp_size;
		rc = PMIx_Publish(info, nkeys);
		if (rc != PMIX_SUCCESS) {
			PMIX_INFO_FREE(info, nkeys);
			C_GOTO(out, rc = -CER_NOMEM);
		}
		PMIX_INFO_FREE(info, nkeys);
	}

out:
	if (rc != 0 && myproc != NULL)
		C_ERROR("PMIx ns %s rank %d, crt_pmix_publish_self failed, "
			"rc: %d.\n", myproc->nspace, myproc->rank, rc);
	return 0;
}

/** lookup the URI of a rank in the primary service group through PMIX */
int
crt_pmix_uri_lookup(crt_group_id_t srv_grpid, crt_rank_t rank, char **uri)
{
	pmix_pdata_t	*pdata = NULL;
	size_t		 len;
	int		 rc = 0;

	if (srv_grpid == NULL || uri == NULL)
		C_GOTO(out, rc = -CER_INVAL);
	len = strlen(srv_grpid);
	if (len == 0 || len > CRT_GROUP_ID_MAX_LEN)
		C_GOTO(out, rc = -CER_INVAL);

	PMIX_PDATA_CREATE(pdata, 1);
	snprintf(pdata[0].key, PMIX_MAX_NSLEN + 5, "pmix-%s-%d-uri",
		 srv_grpid, rank);
	rc = PMIx_Lookup(&pdata[0], 1, NULL, 0);
	if (rc != PMIX_SUCCESS || pdata[0].value.type != PMIX_STRING) {
		C_ERROR("PMIx_Lookup %s failed, rc %d, value type: %d.\n",
			pdata[0].key, rc, pdata[0].value.type);
		C_GOTO(out, rc = -CER_PMIX);
	}
	*uri = strndup(pdata[0].value.data.string, CRT_ADDR_STR_MAX_LEN);
	if (!*uri)
		C_GOTO(out, rc = -CER_NOMEM);

	if (strlen(*uri) > CRT_ADDR_STR_MAX_LEN) {
		C_ERROR("got bad uri %s (len %zu).\n", *uri, strlen(*uri));
		free(*uri);
		rc = -CER_INVAL;
	}

out:
	if (pdata != NULL)
		PMIX_PDATA_FREE(pdata, 1);
	if (rc != 0)
		C_ERROR("crt_pmix_uri_lookup failed, rc: %d.\n", rc);
	return rc;
}

/* PMIx attach to a primary group */
int
crt_pmix_attach(struct crt_grp_priv *grp_priv)
{
	pmix_pdata_t	*pdata = NULL;
	crt_rank_t	 myrank;
	int		 rc = 0;

	C_ASSERT(grp_priv != NULL);

	PMIX_PDATA_CREATE(pdata, 1);
	snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "pmix-%s-size",
		 grp_priv->gp_pub.cg_grpid);
	rc = PMIx_Lookup(pdata, 1, NULL, 0);
	if (rc == PMIX_SUCCESS && pdata[0].value.type == PMIX_UINT32) {
		grp_priv->gp_size = pdata[0].value.data.uint32;
	} else {
		C_ERROR("PMIx_Lookup group %s failed, rc: %d, value.type %d.\n",
			grp_priv->gp_pub.cg_grpid, rc, pdata[0].value.type);
		C_GOTO(out, rc = -CER_PMIX);
	}
	if (grp_priv->gp_size == 0) {
		C_ERROR("group %s got zero size.\n", grp_priv->gp_pub.cg_grpid);
		C_GOTO(out, rc = -CER_PMIX);
	}

	rc = crt_group_rank(NULL, &myrank);
	C_ASSERT(rc == 0);
	grp_priv->gp_psr_rank = myrank % grp_priv->gp_size;
	rc = crt_pmix_uri_lookup(grp_priv->gp_pub.cg_grpid,
				 grp_priv->gp_psr_rank,
				 &grp_priv->gp_psr_phy_addr);
	if (rc != 0)
		C_ERROR("crt_pmix_uri_lookup(grpid: %s, rank %d) failed, "
			"rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			grp_priv->gp_psr_rank, rc);

out:
	if (pdata != NULL)
		PMIX_PDATA_FREE(pdata, 1);
	if (rc != 0)
		C_ERROR("crt_pmix_attach group %s failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);
	return rc;
}
