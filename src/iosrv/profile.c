/**
 * (C) Copyright 2020 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the DAOS server profile
 * API.
 */
#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos_errno.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <gurt/list.h>
#include "drpc_internal.h"
#include "srv_internal.h"

int
srv_profile_start(char *path, int avg)
{
	struct dss_module_info *dmi = dss_get_module_info();
	struct daos_profile **dp_p = &dmi->dmi_dp;
	int		   tgt_id = dmi->dmi_tgt_id;
	d_rank_t	   rank;
	int		   rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc) {
		D_ERROR("start dump ult failed: rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = daos_profile_init(dp_p, path, avg, (int)rank, tgt_id);
	if (rc) {
		D_ERROR("profile init failed: rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	return rc;
}

int
srv_profile_stop(void)
{
	struct dss_module_info	*dmi = dss_get_module_info(); 
	struct daos_profile *dp = dmi->dmi_dp;

	daos_profile_dump(dp);
	daos_profile_destroy(dp);
	dmi->dmi_dp = NULL;
	return 0;
}
