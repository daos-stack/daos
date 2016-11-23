/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

	if (crt_is_singleton()) {
		pmix_gdata->pg_univ_size = 1;
		pmix_gdata->pg_num_apps = 1;
		C_GOTO(bypass_pmix, rc);
	}

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

bypass_pmix:
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

	if (crt_is_singleton())
		goto bypass;

	rc = PMIx_Finalize(NULL, 0);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Finalize failed, rc: %d.\n",
			pmix_gdata->pg_proc.nspace, pmix_gdata->pg_proc.rank,
			rc);
		C_GOTO(out, rc = -CER_PMIX);
	}

bypass:
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

	myproc = &crt_gdata.cg_grp->gg_pmix->pg_proc;

	/* PMIx_Commit(); */

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
	/*
	pmix_persistence_t	 persistence;
	pmix_data_range_t	 range;
	*/
	pmix_proc_t		 proc;
	bool			 flag = true;
	int			 nkeys = 1;
	struct crt_rank_map	 *rank_map;
	int			 i, rc = 0;

	C_ASSERT(grp_priv != NULL);
	C_ASSERT(crt_gdata.cg_grp != NULL);
	pmix_gdata = crt_gdata.cg_grp->gg_pmix;
	myproc = &pmix_gdata->pg_proc;
	unpublish_key[0] = NULL;
	unpublish_key[1] = NULL;
	rank_map = grp_priv->gp_rank_map;
	C_ASSERT(rank_map != NULL);

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

		C_ASSERT(grp_priv->gp_size == pmix_gdata->pg_univ_size);
		for (i = 0; i < grp_priv->gp_size; i++) {
			rank_map[i].rm_rank = i;
			rank_map[i].rm_status = CRT_RANK_ALIVE;
		}

		C_GOTO(out, rc = 0);
	}

	/*
	 * every process publishes its own address string using
	 * (PMIx_rank, setname/addrString)
	 */
	PMIX_INFO_CREATE(info, nkeys);
	snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "%s-%d-psname",
		myproc->nspace, myproc->rank);
	unpublish_key[0] = strndup(info[0].key, PMIX_MAX_KEYLEN);
	if (!unpublish_key[0]) {
		PMIX_INFO_FREE(info, nkeys);
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

	/*
	persistence = PMIX_PERSIST_SESSION;
	range = PMIX_RANGE_GLOBAL;
	PMIX_INFO_LOAD(&info[1], PMIX_PERSISTENCE, &persistence,
			PMIX_UINT);
	PMIX_INFO_LOAD(&info[2], PMIX_RANGE, &range, PMIX_UINT);
	*/
	rc = PMIx_Publish(info, nkeys);
	if (rc != PMIX_SUCCESS) {
		C_ERROR("PMIx ns %s rank %d, PMIx_Publish failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		PMIX_INFO_FREE(info, nkeys);
		free(unpublish_key[0]);
		C_GOTO(out, rc = -CER_PMIX);
	}
	PMIX_INFO_FREE(info, nkeys);

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
			C_ERROR("PMIx ns %s rank %d, PMIx_Lookup %s failed, "
				"rc: %d.\n", myproc->nspace, myproc->rank,
				pdata[0].key, rc);
			PMIX_PDATA_FREE(pdata, 1);
			free(unpublish_key[0]);
			C_GOTO(out, rc = -CER_PMIX);
		}

		if (i == myproc->rank)
			grp_priv->gp_self = grp_priv->gp_size;

		if (strncmp(grp_priv->gp_pub.cg_grpid,
			    pdata[0].value.data.string,
			    CRT_GROUP_ID_MAX_LEN) == 0) {
			rank_map[i].rm_rank = grp_priv->gp_size;
			rank_map[i].rm_status = CRT_RANK_ALIVE;
			grp_priv->gp_size++;
		} else {
			rank_map[i].rm_status = CRT_RANK_NOENT;
		}
	}
	PMIX_PDATA_FREE(pdata, 1);

	/* call fence before unpublish */
	rc = crt_pmix_fence();
	if (rc != 0) {
		C_ERROR("PMIx ns %s rank %d, crt_pmix_fence failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		C_GOTO(out, rc);
	}

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
	snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "cart-%s-%d-uri",
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
		snprintf(info[0].key, PMIX_MAX_KEYLEN + 1, "cart-%s-size",
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
	snprintf(pdata[0].key, PMIX_MAX_NSLEN + 5, "cart-%s-%d-uri",
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
	struct crt_grp_gdata	*grp_gdata;
	pmix_pdata_t		*pdata = NULL;
	crt_rank_t		 myrank;
	int			 rc = 0;

	C_ASSERT(grp_priv != NULL);

	PMIX_PDATA_CREATE(pdata, 1);
	snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "cart-%s-size",
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

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	myrank = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_self :
				    grp_gdata->gg_cli_pri_grp->gp_self;
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

static void
crt_pmix_notify_fn(size_t registration_id, pmix_status_t status,
		   const pmix_proc_t *source, pmix_info_t info[], size_t ninfo,
		   pmix_info_t results[], size_t nresults,
		   pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	struct crt_grp_priv	*grp_priv;
	struct crt_rank_map	*rank_map;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(pmix_gdata != NULL && pmix_gdata->pg_univ_size > 0);

	C_DEBUG("got one PMIx notification, source->rank: %d.\n",
		source->rank);
	if (source->rank >= pmix_gdata->pg_univ_size) {
		C_ERROR("pmix rank %d out of range [0, %d].\n",
			source->rank, pmix_gdata->pg_univ_size - 1);
		goto out;
	}

	grp_priv = crt_is_service() ? grp_gdata->gg_srv_pri_grp :
				      grp_gdata->gg_cli_pri_grp;
	C_ASSERT(grp_priv != NULL);
	rank_map = &grp_priv->gp_rank_map[source->rank];
	if (rank_map->rm_status != CRT_RANK_NOENT) {
		if (rank_map->rm_status == CRT_RANK_ALIVE) {
			rank_map->rm_status = CRT_RANK_DEAD;
			C_WARN("group %s, mark rank %d as dead",
			       grp_priv->gp_pub.cg_grpid, rank_map->rm_rank);
		} else {
			C_ASSERT(rank_map->rm_status == CRT_RANK_DEAD);
			C_ERROR("group %s, rank %d already dead.\n",
				grp_priv->gp_pub.cg_grpid, rank_map->rm_rank);
		}
	} else {
		C_DEBUG("PMIx rank %d not belong to group %s, ignore it.\n",
			source->rank, grp_priv->gp_pub.cg_grpid);
	}

out:
	/** let the notifier know we are done */
	if (cbfunc)
		cbfunc(PMIX_SUCCESS, NULL, 0, NULL, NULL, cbdata);
}

static void
crt_pmix_errhdlr_reg_callbk(pmix_status_t status, size_t errhdlr_ref,
			    void *cbdata)
{
	struct crt_grp_priv	*grp_priv;

	grp_priv = (struct crt_grp_priv *) cbdata;
	C_ASSERT(grp_priv != NULL);

	C_DEBUG("crt_pmix_errhdlr_reg_callbk called with status %d, ref=%zu.\n",
		status, errhdlr_ref);

	grp_priv->gp_errhdlr_ref = errhdlr_ref;
}

void
crt_pmix_reg_event_hdlr(struct crt_grp_priv *grp_priv)
{
	C_ASSERT(grp_priv != NULL);

	PMIx_Register_event_handler(NULL, 0, NULL, 0, crt_pmix_notify_fn,
				    crt_pmix_errhdlr_reg_callbk, grp_priv);
}

static void
crt_pmix_dereg_cb(pmix_status_t status, void *cbdata)
{
	C_DEBUG("crt_pmix_dereg_cb with status %d", status);
}

void
crt_pmix_dereg_event_hdlr(struct crt_grp_priv *grp_priv)
{
	C_ASSERT(grp_priv != NULL);
	PMIx_Deregister_event_handler(grp_priv->gp_errhdlr_ref,
				      crt_pmix_dereg_cb, NULL);
}
