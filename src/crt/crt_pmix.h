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
 * This file is part of CaRT. It gives out the data types and function
 * declarations related with PMIx.
 */

#ifndef __CRT_PMIX_H__
#define __CRT_PMIX_H__

#include "pmix.h"

/* pmix layer global data, be included in struct crt_grp_gdata */
struct crt_pmix_gdata {
	/* PMIx proc object */
	pmix_proc_t		pg_proc;
	/* universe size */
	uint32_t		pg_univ_size;
	/* #apps in this job */
	uint32_t		pg_num_apps;
};


int crt_pmix_init(void);
int crt_pmix_fini(void);
int crt_pmix_fence(void);
int crt_pmix_assign_rank(struct crt_grp_priv *grp_priv);
int crt_pmix_publish_self(struct crt_grp_priv *grp_priv);
int crt_pmix_uri_lookup(crt_group_id_t srv_grpid, crt_rank_t rank, char **uri);
int crt_pmix_attach(struct crt_grp_priv *grp_priv);

#endif /* __CRT_PMIX_H__ */
