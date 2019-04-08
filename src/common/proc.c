/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos. It implements daos specific RPC input/output proc
 * functions that need to be shared by both client and server modules.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos_api.h>
#include <daos_security.h>
#include <cart/api.h>

static int
crt_proc_prop_daos_acl(crt_proc_t proc, struct daos_prop_entry *entry)
{
	int		rc;
	struct daos_acl	*acl;
	d_iov_t		iov;
	crt_proc_op_t	proc_op;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return rc;

	if (entry->dpe_val_ptr == NULL) {
		memset(&iov, 0, sizeof(iov));
	} else {
		acl = (struct daos_acl *)entry->dpe_val_ptr;
		d_iov_set(&iov, entry->dpe_val_ptr,
			daos_acl_get_size(acl));
	}

	rc = crt_proc_d_iov_t(proc, &iov);
	if (rc != 0)
		return rc;

	if (proc_op == CRT_PROC_DECODE)
		entry->dpe_val_ptr = iov.iov_buf;
	else if (proc_op == CRT_PROC_FREE)
		entry->dpe_val_ptr = NULL;

	return rc;
}

static int
crt_proc_prop_entries(crt_proc_t proc, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	int			 i;
	int			 rc = 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		rc = crt_proc_uint32_t(proc, &entry->dpe_type);
		if (rc)
			break;
		rc = crt_proc_uint32_t(proc, &entry->dpe_reserv);
		if (rc)
			break;
		if (entry->dpe_type == DAOS_PROP_PO_LABEL ||
		    entry->dpe_type == DAOS_PROP_CO_LABEL)
			rc = crt_proc_d_string_t(proc, &entry->dpe_str);
		else if (entry->dpe_type == DAOS_PROP_PO_ACL ||
			 entry->dpe_type == DAOS_PROP_CO_ACL)
			rc = crt_proc_prop_daos_acl(proc, entry);
		else
			rc = crt_proc_uint64_t(proc, &entry->dpe_val);
		if (rc)
			break;
	}
	return rc;
}

int
crt_proc_daos_prop_t(crt_proc_t proc, daos_prop_t **data)
{
	daos_prop_t		*prop;
	crt_proc_op_t		 proc_op;
	uint32_t		 nr, tmp;
	int			 rc;

	if (proc == NULL || data == NULL)
		return -DER_INVAL;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc)
		return rc;
	switch (proc_op) {
	case CRT_PROC_ENCODE:
		prop = *data;
		if (prop == NULL || prop->dpp_nr == 0 ||
		    prop->dpp_entries == NULL) {
			nr = 0;
			rc = crt_proc_uint32_t(proc, &nr);
			return rc;
		}
		rc = crt_proc_uint32_t(proc, &prop->dpp_nr);
		if (rc != 0)
			return rc;
		rc = crt_proc_uint32_t(proc, &prop->dpp_reserv);
		if (rc != 0)
			return rc;
		rc = crt_proc_prop_entries(proc, prop);
		return rc;
	case CRT_PROC_DECODE:
		rc = crt_proc_uint32_t(proc, &nr);
		if (rc)
			return rc;
		if (nr == 0) {
			*data = NULL;
			return rc;
		}
		rc = crt_proc_uint32_t(proc, &tmp);
		if (rc != 0)
			return rc;
		if (nr > DAOS_PROP_ENTRIES_MAX_NR) {
			D_ERROR("invalid entries nr %d (> %d).\n",
				nr, DAOS_PROP_ENTRIES_MAX_NR);
			return -DER_INVAL;
		}
		prop = daos_prop_alloc(nr);
		if (prop == NULL)
			return -DER_NOMEM;
		prop->dpp_reserv = tmp;
		rc = crt_proc_prop_entries(proc, prop);
		if (rc) {
			daos_prop_free(prop);
			return rc;
		}
		*data = prop;
		return rc;
	case CRT_PROC_FREE:
		daos_prop_free(*data);
		return 0;
	default:
		D_ERROR("bad proc_op %d.\n", proc_op);
		return -DER_INVAL;
	}
}
