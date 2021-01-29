/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

int
crt_proc_struct_dtx_id(crt_proc_t proc, struct dtx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dti->dti_hlc);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

int
crt_proc_struct_daos_acl(crt_proc_t proc, struct daos_acl **data)
{
	int		rc;
	d_iov_t		iov;
	crt_proc_op_t	proc_op;

	if (proc == NULL || data == NULL)
		return -DER_INVAL;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return rc;

	switch (proc_op) {
	case CRT_PROC_ENCODE:
		if (*data != NULL) {
			d_iov_set(&iov, (void *)*data,
				  daos_acl_get_size(*data));
		} else {
			iov.iov_buf = NULL;
			iov.iov_buf_len = 0;
			iov.iov_len = 0;
		}
		/* fall through to copy it */
	case CRT_PROC_DECODE:
		rc = crt_proc_d_iov_t(proc, &iov);
		if (!rc && proc_op == CRT_PROC_DECODE)
			*data = (struct daos_acl *)iov.iov_buf;
		break;
	case CRT_PROC_FREE:
		*data = NULL;
		break;
	default:
		D_ERROR("bad proc_op %d.\n", proc_op);
		return -DER_INVAL;
	}

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
		    entry->dpe_type == DAOS_PROP_CO_LABEL ||
		    entry->dpe_type == DAOS_PROP_PO_OWNER ||
		    entry->dpe_type == DAOS_PROP_CO_OWNER ||
		    entry->dpe_type == DAOS_PROP_PO_OWNER_GROUP ||
		    entry->dpe_type == DAOS_PROP_CO_OWNER_GROUP)
			rc = crt_proc_d_string_t(proc, &entry->dpe_str);
		else if (entry->dpe_type == DAOS_PROP_PO_ACL ||
			 entry->dpe_type == DAOS_PROP_CO_ACL)
			rc = crt_proc_struct_daos_acl(proc,
						      (struct daos_acl **)
						      &entry->dpe_val_ptr);
		else if (entry->dpe_type == DAOS_PROP_PO_SVC_LIST)
			rc = crt_proc_d_rank_list_t(proc,
					(d_rank_list_t **)&entry->dpe_val_ptr);
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
	uint32_t		 nr = 0, tmp = 0;
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
		prop = *data;
		if (prop == NULL)
			return 0;
		if (prop->dpp_nr == 0 || prop->dpp_entries == NULL) {
			D_FREE_PTR(prop);
			return 0;
		}
		crt_proc_prop_entries(proc, prop);
		D_FREE(prop->dpp_entries);
		D_FREE_PTR(prop);
		return 0;
	default:
		D_ERROR("bad proc_op %d.\n", proc_op);
		return -DER_INVAL;
	}
}
