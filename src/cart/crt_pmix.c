/* Copyright (C) 2016-2019 Intel Corporation
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
#define D_LOGFAC	DD_FAC(pmix)

#include "crt_internal.h"
#include "semaphore.h"

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

	D_ASSERT(CRT_PMIX_ENABLED());

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	D_ASSERT(grp_gdata->gg_pmix_inited == 0);
	D_ASSERT(grp_gdata->gg_pmix == NULL);

	D_ALLOC_PTR(pmix_gdata);
	if (pmix_gdata == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (crt_is_singleton()) {
		pmix_gdata->pg_univ_size = 1;
		pmix_gdata->pg_num_apps = 1;
		D_GOTO(bypass_pmix, rc);
	}

	rc = PMIx_Init(&pmix_gdata->pg_proc, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		D_ERROR("PMIx_Init failed, rc: %d.\n", rc);
		D_GOTO(out, rc = -DER_PMIX);
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
		D_ERROR("PMIx ns %s rank %d, PMIx_Get universe size failed, "
			"rc: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, rc);
		D_GOTO(out, rc = -DER_PMIX);
	}
	if (val->type != PMIX_UINT32) {
		PMIX_INFO_FREE(info, 1);
		PMIX_PROC_DESTRUCT(&proc);
		D_ERROR("PMIx ns %s rank %d, PMIx_Get universe size returned "
			"wrong type: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, val->type);
		D_GOTO(out, rc = -DER_PMIX);
	}
	pmix_gdata->pg_univ_size = val->data.uint32;
	PMIX_VALUE_RELEASE(val);

	/* get the number of apps in this job */
	rc = PMIx_Get(&proc, PMIX_JOB_NUM_APPS, info, 1, &val);
	PMIX_INFO_FREE(info, 1);
	PMIX_PROC_DESTRUCT(&proc);
	if (rc != PMIX_SUCCESS) {
		D_ERROR("PMIx ns %s rank %d: PMIx_Get num_apps failed, "
			"rc: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, rc);
		D_GOTO(out, rc = -DER_PMIX);
	}
	if (val->type != PMIX_UINT32) {
		D_ERROR("PMIx ns %s rank %d, PMIx_Get num_apps returned wrong "
			"type: %d.\n", pmix_gdata->pg_proc.nspace,
			pmix_gdata->pg_proc.rank, val->type);
		D_GOTO(out, rc = -DER_PMIX);
	}
	pmix_gdata->pg_num_apps = val->data.uint32;
	PMIX_VALUE_RELEASE(val);

bypass_pmix:
	grp_gdata->gg_pmix = pmix_gdata;
	grp_gdata->gg_pmix_inited = 1;

out:
	if (rc != 0) {
		D_ERROR("crt_pmix_init failed, rc: %d.\n", rc);
		if (pmix_gdata != NULL)
			D_FREE_PTR(pmix_gdata);
	}
	return rc;
}

int
crt_pmix_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	D_ASSERT(CRT_PMIX_ENABLED());

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	D_ASSERT(grp_gdata->gg_pmix_inited == 1);
	D_ASSERT(grp_gdata->gg_pmix != NULL);

	pmix_gdata = grp_gdata->gg_pmix;

	if (crt_is_singleton())
		goto bypass;

	rc = PMIx_Finalize(NULL, 0);
	if (rc != PMIX_SUCCESS) {
		D_ERROR("PMIx ns %s rank %d, PMIx_Finalize failed, rc: %d.\n",
			pmix_gdata->pg_proc.nspace, pmix_gdata->pg_proc.rank,
			rc);
		D_GOTO(out, rc = -DER_PMIX);
	}

bypass:
	D_FREE_PTR(pmix_gdata);
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

	D_ASSERT(CRT_PMIX_ENABLED());
	myproc = &crt_gdata.cg_grp->gg_pmix->pg_proc;

	/* PMIx_Commit(); */

	PMIX_PROC_CONSTRUCT(&proc);
	/* nspace has PMIX_MAX_NSLEN + 1 chars.  See pmix_common.h */
	strncpy(proc.nspace, myproc->nspace, PMIX_MAX_NSLEN + 1);
	proc.rank = PMIX_RANK_WILDCARD;
	PMIX_INFO_CREATE(info, 1);
	if (info == NULL) {
		D_ERROR("PMIX_INFO_CREATE failed.\n");
		D_GOTO(out, rc = -DER_PMIX);
	}
	flag = true;
	PMIX_INFO_LOAD(info, PMIX_COLLECT_DATA, &flag, PMIX_BOOL);

	rc = PMIx_Fence(&proc, 1, info, 1);
	if (rc != PMIX_SUCCESS) {
		D_ERROR("PMIx ns %s rank %d, PMIx_Fence failed, rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		D_GOTO(out, rc = -DER_PMIX);
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

	D_ASSERT(CRT_PMIX_ENABLED());
	D_ASSERT(grp_priv != NULL);
	D_ASSERT(crt_gdata.cg_grp != NULL);
	pmix_gdata = crt_gdata.cg_grp->gg_pmix;
	myproc = &pmix_gdata->pg_proc;
	unpublish_key[0] = NULL;
	unpublish_key[1] = NULL;
	rank_map = grp_priv->gp_pmix_rank_map;
	D_ASSERT(rank_map != NULL);

	/* get incorrect result (grp_priv->gp_self = -1), so disable it */
	if (/* pmix_gdata->pg_num_apps == 1 */ 0) {
		PMIX_PROC_CONSTRUCT(&proc);
		/* nspace has PMIX_MAX_NSLEN + 1 chars.  See pmix_common.h */
		strncpy(proc.nspace, myproc->nspace, PMIX_MAX_NSLEN + 1);
		proc.rank = PMIX_RANK_WILDCARD;
		PMIX_INFO_CREATE(info, 1);
		PMIX_INFO_LOAD(&info[0], PMIX_IMMEDIATE, &flag, PMIX_BOOL);
		rc = PMIx_Get(&proc, PMIX_APP_SIZE, info, 1, &val);
		if (rc != PMIX_SUCCESS) {
			PMIX_INFO_FREE(info, 1);
			PMIX_PROC_DESTRUCT(&proc);
			D_ERROR("PMIx ns %s rank %d, PMIx_Get failed,rc: %d.\n",
				myproc->nspace, myproc->rank, rc);
			D_GOTO(out, rc = -DER_PMIX);
		}
		grp_priv->gp_size = val->data.uint32;
		PMIX_VALUE_RELEASE(val);

		rc = PMIx_Get(myproc, PMIX_APP_RANK, info, 1, &val);
		PMIX_INFO_FREE(info, 1);
		PMIX_PROC_DESTRUCT(&proc);
		if (rc != PMIX_SUCCESS) {
			D_ERROR("PMIx ns %s rank %d, PMIx_Get failed,rc: %d.\n",
				myproc->nspace, myproc->rank, rc);
			D_GOTO(out, rc = -DER_PMIX);
		}
		grp_priv->gp_self = val->data.uint32;
		PMIX_VALUE_RELEASE(val);

		D_ASSERT(grp_priv->gp_size == pmix_gdata->pg_univ_size);
		for (i = 0; i < grp_priv->gp_size; i++) {
			rank_map[i].rm_rank = i;
			rank_map[i].rm_status = CRT_RANK_ALIVE;
		}

		D_GOTO(out, rc = 0);
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
		D_GOTO(out, rc = -DER_NOMEM);
	}
	info[0].value.type = PMIX_STRING;
	info[0].value.data.string = strndup(grp_priv->gp_pub.cg_grpid,
					    CRT_GROUP_ID_MAX_LEN);
	if (info[0].value.data.string == NULL) {
		PMIX_INFO_FREE(info, 1);
		free(unpublish_key[0]);
		D_GOTO(out, rc = -DER_NOMEM);
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
		D_ERROR("PMIx ns %s rank %d, PMIx_Publish failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		PMIX_INFO_FREE(info, nkeys);
		free(unpublish_key[0]);
		D_GOTO(out, rc = -DER_PMIX);
	}
	PMIX_INFO_FREE(info, nkeys);

	/* call fence to ensure the data is received */
	rc = crt_pmix_fence();
	if (rc != 0) {
		D_ERROR("PMIx ns %s rank %d, crt_pmix_fence failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		D_GOTO(out, rc);
	}

	/* loop over universe size, parse address string and set size */
	PMIX_PDATA_CREATE(pdata, 1);
	for (i = 0; i < pmix_gdata->pg_univ_size; i++) {
		/* generate the key to query my process set name */
		snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "%s-%d-psname",
			 myproc->nspace, i);

		rc = PMIx_Lookup(pdata, 1, NULL, 0);
		if (rc != PMIX_SUCCESS) {
			D_ERROR("PMIx ns %s rank %d, PMIx_Lookup %s failed, "
				"rc: %d.\n", myproc->nspace, myproc->rank,
				pdata[0].key, rc);
			PMIX_PDATA_FREE(pdata, 1);
			free(unpublish_key[0]);
			D_GOTO(out, rc = -DER_PMIX);
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
		D_ERROR("PMIx ns %s rank %d, crt_pmix_fence failed,rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		D_GOTO(out, rc);
	}

	rc = PMIx_Unpublish(unpublish_key, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		D_ERROR("PMIx ns %s rank %d, PMIx_Unpublish failed, rc: %d.\n",
			myproc->nspace, myproc->rank, rc);
		free(unpublish_key[0]);
		D_GOTO(out, rc = -DER_PMIX);
	}
	free(unpublish_key[0]);

out:
	if (rc == 0)
		D_DEBUG(DB_TRACE, "crt_pmix_assign_rank get size %d, "
			"self %d.\n", grp_priv->gp_size, grp_priv->gp_self);
	else if (myproc != NULL)
		D_ERROR("PMIx ns %s rank %d, crt_pmix_assign_rank failed, "
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

	D_ASSERT(CRT_PMIX_ENABLED());
	D_ASSERT(grp_priv != NULL);

	D_ASSERT(crt_gdata.cg_grp != NULL);
	pmix_gdata = crt_gdata.cg_grp->gg_pmix;
	myproc = &pmix_gdata->pg_proc;

	if (!grp_priv->gp_local) {
		D_ERROR("cannot publish self on non-local group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (!grp_priv->gp_service) {
		D_DEBUG(DB_TRACE,
			"ignore publish self on non-service group.\n");
		D_GOTO(out, rc = 0);
	}

	PMIX_INFO_CREATE(info, nkeys);
	if (!info) {
		D_ERROR("PMIX_INFO_CREATE failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
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
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = PMIx_Publish(info, nkeys);
	if (rc != PMIX_SUCCESS) {
		PMIX_INFO_FREE(info, nkeys);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	/* D_DEBUG("PMIx_Publish %s, rc: %d.\n", info[0].key, rc); */
	PMIX_INFO_FREE(info, nkeys);

	if (grp_priv->gp_self == 0) {
		PMIX_INFO_CREATE(info, nkeys);
		if (!info)
			return -DER_NOMEM;
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
			D_GOTO(out, rc = -DER_NOMEM);
		}
		PMIX_INFO_FREE(info, nkeys);
	}

out:
	if (rc != 0 && myproc != NULL)
		D_ERROR("PMIx ns %s rank %d, crt_pmix_publish_self failed, "
			"rc: %d.\n", myproc->nspace, myproc->rank, rc);
	return 0;
}

/** lookup the URI of a rank in the primary service group through PMIX */
int
crt_pmix_uri_lookup(crt_group_id_t srv_grpid, d_rank_t rank, char **uri)
{
	pmix_pdata_t	*pdata = NULL;
	size_t		 len;
	int		 rc = 0;

	D_ASSERT(CRT_PMIX_ENABLED());
	if (srv_grpid == NULL || uri == NULL)
		D_GOTO(out, rc = -DER_INVAL);
	len = strlen(srv_grpid);
	if (len == 0 || len > CRT_GROUP_ID_MAX_LEN)
		D_GOTO(out, rc = -DER_INVAL);

	PMIX_PDATA_CREATE(pdata, 1);
	if (pdata == NULL) {
		D_ERROR("PMIX_PDATA_CREATE returned NULL\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "cart-%s-%d-uri",
		 srv_grpid, rank);
	rc = PMIx_Lookup(&pdata[0], 1, NULL, 0);
	if (rc != PMIX_SUCCESS || pdata[0].value.type != PMIX_STRING) {
		D_ERROR("PMIx_Lookup %s failed, rc %d, value type: %d.\n",
			pdata[0].key, rc, pdata[0].value.type);
		D_GOTO(out, rc = -DER_PMIX);
	}
	*uri = strndup(pdata[0].value.data.string, CRT_ADDR_STR_MAX_LEN);
	if (!*uri)
		D_GOTO(out, rc = -DER_NOMEM);

	if (strlen(*uri) > CRT_ADDR_STR_MAX_LEN) {
		D_ERROR("got bad uri %s (len %zu).\n", *uri, strlen(*uri));
		free(*uri);
		rc = -DER_INVAL;
	}

out:
	if (pdata != NULL)
		PMIX_PDATA_FREE(pdata, 1);
	if (rc != 0)
		D_ERROR("crt_pmix_uri_lookup failed, rc: %d.\n", rc);
	return rc;
}

int
crt_pmix_psr_load(struct crt_grp_priv *grp_priv, d_rank_t psr_rank)
{
	crt_phy_addr_t	uri = NULL;
	int		rc;

	D_ASSERT(CRT_PMIX_ENABLED());
	D_ASSERT(grp_priv != NULL);
	D_ASSERT(psr_rank < grp_priv->gp_size);

	rc = crt_pmix_uri_lookup(grp_priv->gp_pub.cg_grpid,
				 psr_rank, &uri);
	if (rc == 0) {
		crt_grp_psr_set(grp_priv, psr_rank, uri);
		free(uri);
	} else
		D_ERROR("crt_pmix_uri_lookup(grpid: %s, rank %d) failed, "
			"rc: %d.\n", grp_priv->gp_pub.cg_grpid, psr_rank, rc);

	return rc;
}

/* PMIx attach to a primary group */
int
crt_pmix_attach(struct crt_grp_priv *grp_priv)
{
	struct crt_grp_gdata	*grp_gdata;
	pmix_pdata_t		*pdata = NULL;
	d_rank_t		 myrank;
	int			 rc = 0;

	D_ASSERT(CRT_PMIX_ENABLED());
	D_ASSERT(grp_priv != NULL);

	PMIX_PDATA_CREATE(pdata, 1);
	if (pdata == NULL) {
		D_ERROR("PMIX_PDATA_CREATE returned NULL\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	snprintf(pdata[0].key, PMIX_MAX_KEYLEN + 1, "cart-%s-size",
		 grp_priv->gp_pub.cg_grpid);
	rc = PMIx_Lookup(pdata, 1, NULL, 0);
	if (rc == PMIX_SUCCESS && pdata[0].value.type == PMIX_UINT32) {
		grp_priv->gp_size = pdata[0].value.data.uint32;
	} else {
		D_ERROR("PMIx_Lookup group %s failed, rc: %d, value.type %d.\n",
			grp_priv->gp_pub.cg_grpid, rc, pdata[0].value.type);
		D_GOTO(out, rc = -DER_PMIX);
	}
	if (grp_priv->gp_size == 0) {
		D_ERROR("group %s got zero size.\n", grp_priv->gp_pub.cg_grpid);
		D_GOTO(out, rc = -DER_PMIX);
	}

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	myrank = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_self :
				    grp_gdata->gg_cli_pri_grp->gp_self;
	rc = crt_pmix_psr_load(grp_priv, myrank % grp_priv->gp_size);
	if (rc != 0)
		D_ERROR("crt_pmix_attach (grpid: %s) failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);

out:
	if (pdata != NULL)
		PMIX_PDATA_FREE(pdata, 1);
	if (rc != 0)
		D_ERROR("crt_pmix_attach group %s failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);
	return rc;
}

static void
crt_plugin_event_handler_core(size_t evhdlr_registration_id,
			      pmix_status_t status,
			      const pmix_proc_t *source,
			      pmix_info_t info[], size_t ninfo,
			      pmix_info_t *results, size_t nresults,
			      pmix_event_notification_cbfunc_fn_t cbfunc,
			      void *cbdata)
{
	/**
	 * convert source->rank from pmix rank to cart rank
	 * walk the global list to execute the user callbacks
	 */
	struct crt_grp_gdata		*grp_gdata;
	struct crt_pmix_gdata		*pmix_gdata;
	struct crt_grp_priv		*grp_priv;
	crt_event_cb			 cb_func;
	d_rank_t			 crt_rank;
	struct crt_event_cb_priv	*event_cb_priv;
	void				*arg;

	D_ASSERT(CRT_PMIX_ENABLED());

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	D_ASSERT(grp_gdata->gg_pmix_inited == 1);
	D_ASSERT(grp_gdata->gg_pmix != NULL);
	D_ASSERT(grp_gdata->gg_inited == 1);
	D_ASSERT(grp_gdata->gg_srv_pri_grp != NULL);

	pmix_gdata = grp_gdata->gg_pmix;
	grp_priv = grp_gdata->gg_srv_pri_grp;

	/* filter source->namespace */
	if (strncmp(source->nspace, pmix_gdata->pg_proc.nspace,
		    PMIX_MAX_NSLEN)) {
		D_DEBUG(DB_TRACE, "PMIx event not relevant to my namespace.\n");
		D_GOTO(out, 0);
	}

	/* This seems to often happen immediatly after the PROC_ABORTED event
	 * so simply log it and move on.
	 */
	if (status == PMIX_ERR_UNREACH) {
		D_DEBUG(DB_TRACE, "PMIx event is PMIX_ERR_UNREACH %d\n",
			source->rank);
		D_GOTO(out, 0);
	}

	if (status != PMIX_ERR_PROC_ABORTED) {
		D_DEBUG(DB_TRACE, "PMIx event is %d not PMIX_ERR_PROC_ABORTED."
			"\n", status);
		D_GOTO(out, 0);
	}

	if (source->rank >= pmix_gdata->pg_univ_size) {
		D_ERROR("pmix rank %d out of range [0, %d].\n",
			source->rank, pmix_gdata->pg_univ_size - 1);
		D_GOTO(out, 0);
	}

	if (grp_priv->gp_pmix_rank_map[source->rank].rm_status ==
		CRT_RANK_NOENT) {
		D_DEBUG(DB_TRACE, "PMIx event not relevant to cart group: "
			"%s.\n", grp_priv->gp_pub.cg_grpid);
		D_GOTO(out, 0);
	}

	/* convert source->rank from pmix rank to cart rank */
	crt_rank = grp_priv->gp_pmix_rank_map[source->rank].rm_rank;
	D_DEBUG(DB_TRACE, "received pmix notification about rank %d.\n",
		crt_rank);
	/* walk the global list to execute the user callbacks */
	D_RWLOCK_RDLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	d_list_for_each_entry(event_cb_priv,
			      &crt_plugin_gdata.cpg_event_cbs, cecp_link) {
		cb_func = event_cb_priv->cecp_func;
		arg = event_cb_priv->cecp_args;
		cb_func(crt_rank, CRT_EVS_PMIX, CRT_EVT_DEAD, arg);
	}
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);

out:
	if (cbfunc)
		cbfunc(PMIX_SUCCESS, NULL, 0, NULL, NULL, cbdata);
}

static void
crt_plugin_pmix_errhdlr_reg_cb(pmix_status_t status, size_t errhdlr_ref,
			       void *arg)
{
	sem_t *token_to_proceed = arg;
	int rc;

	D_ASSERT(CRT_PMIX_ENABLED());
	D_DEBUG(DB_TRACE, "crt_plugin_pmix_errhdlr_reg_cb() called with status"
		" %d, ref=%zu.\n", status, errhdlr_ref);
	if (status != 0)
		D_ERROR("crt_plugin_pmix_errhdlr_reg_cb() called with "
			"status %d\n", status);
	crt_plugin_gdata.cpg_pmix_errhdlr_ref = errhdlr_ref;

	rc = sem_post(token_to_proceed);
	if (rc != 0)
		D_ERROR("sem_post failed, rc: %d.\n", rc);

}

int
crt_plugin_pmix_init(void)
{
	sem_t	token_to_proceed;
	int	rc;

	D_ASSERT(CRT_PMIX_ENABLED());
	if (!crt_is_service() || crt_is_singleton())
		return -DER_INVAL;

	D_RWLOCK_WRLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	if (crt_plugin_gdata.cpg_pmix_errhdlr_inited == 1) {
		D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
		return -DER_SUCCESS;
	}

	rc = sem_init(&token_to_proceed, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init failed, rc: %d.\n", rc);
		return -DER_MISC;
	}

	PMIx_Register_event_handler(NULL, 0, NULL, 0,
				    crt_plugin_event_handler_core,
				    crt_plugin_pmix_errhdlr_reg_cb,
				    &token_to_proceed);

	rc = sem_wait(&token_to_proceed);
	if (rc != 0) {
		D_ERROR("sem_wait failed, rc: %d.\n", rc);
		D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
		sem_destroy(&token_to_proceed);
		return -DER_MISC;
	}

	crt_plugin_gdata.cpg_pmix_errhdlr_inited = 1;
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	sem_destroy(&token_to_proceed);
	return -DER_SUCCESS;
}

static void
crt_plugin_pmix_errhdlr_dereg_cb(pmix_status_t status, void *arg)
{
	sem_t	*token_to_proceed = arg;
	int	 rc;

	D_ASSERT(CRT_PMIX_ENABLED());
	D_DEBUG(DB_TRACE, "crt_plugin_pmix_errhdlr_dereg_cb() called with "
		"status %d", status);

	rc = sem_post(token_to_proceed);
	if (rc != 0)
		D_ERROR("sem_post failed, rc: %d.\n", rc);
}

void
crt_plugin_pmix_fini(void)
{
	sem_t	token_to_proceed;
	int	rc;

	D_ASSERT(CRT_PMIX_ENABLED());
	if (!crt_is_service() || crt_is_singleton())
		return;

	rc = sem_init(&token_to_proceed, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init failed, rc: %d.\n", rc);
		return;
	}

	D_RWLOCK_WRLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	if (!crt_plugin_gdata.cpg_pmix_errhdlr_inited) {
		D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
		sem_destroy(&token_to_proceed);
		return;
	}

	PMIx_Deregister_event_handler(crt_plugin_gdata.cpg_pmix_errhdlr_ref,
				      crt_plugin_pmix_errhdlr_dereg_cb,
				      &token_to_proceed);

	D_DEBUG(DB_TRACE, "calling sem_wait on sem_t %p.\n", &token_to_proceed);
	rc = sem_wait(&token_to_proceed);
	if (rc != 0) {
		D_ERROR("sem_wait failed, rc: %d.\n", rc);
		D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
		sem_destroy(&token_to_proceed);
		return;
	}
	crt_plugin_gdata.cpg_pmix_errhdlr_inited = 0;
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);

	rc = sem_destroy(&token_to_proceed);
	if (rc != 0)
		D_ERROR("sem_destroy failed, rc: %d.\n", rc);
}
