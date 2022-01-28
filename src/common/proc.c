/**
 * (C) Copyright 2019-2022 Intel Corporation.
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
#include <daos_prop.h>

int
crt_proc_struct_dtx_id(crt_proc_t proc, crt_proc_op_t proc_op,
		       struct dtx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, proc_op, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, proc_op, &dti->dti_hlc);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

int
crt_proc_struct_daos_acl(crt_proc_t proc, crt_proc_op_t proc_op,
			 struct daos_acl **data)
{
	d_iov_t		iov = {0};
	int		rc = 0;

	if (proc == NULL || data == NULL)
		return -DER_INVAL;

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
		rc = crt_proc_d_iov_t(proc, proc_op, &iov);
		if (!rc && DECODING(proc_op))
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
crt_proc_prop_entries(crt_proc_t proc, crt_proc_op_t proc_op, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	int			 i;
	int			 rc = 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		rc = crt_proc_uint32_t(proc, proc_op, &entry->dpe_type);
		if (rc)
			break;
		rc = crt_proc_uint16_t(proc, proc_op, &entry->dpe_flags);
		if (rc)
			break;
		rc = crt_proc_uint16_t(proc, proc_op, &entry->dpe_reserv);
		if (rc)
			break;

		if (entry->dpe_type == DAOS_PROP_PO_LABEL ||
		    entry->dpe_type == DAOS_PROP_CO_LABEL ||
		    entry->dpe_type == DAOS_PROP_PO_OWNER ||
		    entry->dpe_type == DAOS_PROP_CO_OWNER ||
		    entry->dpe_type == DAOS_PROP_PO_OWNER_GROUP ||
		    entry->dpe_type == DAOS_PROP_CO_OWNER_GROUP ||
		    entry->dpe_type == DAOS_PROP_PO_PERF_DOMAIN ||
		    entry->dpe_type == DAOS_PROP_PO_POLICY) {
			rc = crt_proc_d_string_t(proc, proc_op,
						 &entry->dpe_str);

		} else if (entry->dpe_type == DAOS_PROP_PO_ACL ||
			 entry->dpe_type == DAOS_PROP_CO_ACL) {
			rc = crt_proc_struct_daos_acl(proc, proc_op,
						      (struct daos_acl **)
						      &entry->dpe_val_ptr);

		} else if (entry->dpe_type == DAOS_PROP_PO_SVC_LIST) {
			rc = crt_proc_d_rank_list_t(proc, proc_op,
					(d_rank_list_t **)&entry->dpe_val_ptr);
		} else if (entry->dpe_type == DAOS_PROP_CO_ROOTS) {
			struct daos_prop_co_roots *roots;

			if (DECODING(proc_op)) {
				D_ALLOC(entry->dpe_val_ptr, sizeof(*roots));
				if (!entry->dpe_val_ptr) {
					rc = -DER_NOMEM;
					break;
				}
			}
			roots = entry->dpe_val_ptr;
			rc = crt_proc_memcpy(proc, proc_op,
					      roots, sizeof(*roots));

			if (FREEING(proc_op))
				D_FREE(entry->dpe_val_ptr);
		} else {
			rc = crt_proc_uint64_t(proc, proc_op, &entry->dpe_val);
		}
		if (rc)
			break;
	}
	return rc;
}

int
crt_proc_daos_prop_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_prop_t **data)
{
	daos_prop_t		*prop;
	uint32_t		 nr = 0, tmp = 0;
	int			 rc;

	if (proc == NULL || data == NULL)
		return -DER_INVAL;

	switch (proc_op) {
	case CRT_PROC_ENCODE:
		prop = *data;
		if (prop == NULL || prop->dpp_nr == 0 ||
		    prop->dpp_entries == NULL) {
			nr = 0;
			rc = crt_proc_uint32_t(proc, proc_op, &nr);
			return rc;
		}
		rc = crt_proc_uint32_t(proc, proc_op, &prop->dpp_nr);
		if (rc != 0)
			return rc;
		rc = crt_proc_uint32_t(proc, proc_op, &prop->dpp_reserv);
		if (rc != 0)
			return rc;
		rc = crt_proc_prop_entries(proc, proc_op, prop);
		return rc;
	case CRT_PROC_DECODE:
		rc = crt_proc_uint32_t(proc, proc_op, &nr);
		if (rc)
			return rc;
		if (nr == 0) {
			*data = NULL;
			return rc;
		}
		rc = crt_proc_uint32_t(proc, proc_op, &tmp);
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
		rc = crt_proc_prop_entries(proc, proc_op, prop);
		if (rc) {
			crt_proc_prop_entries(proc, CRT_PROC_FREE, prop);
			D_FREE(prop->dpp_entries);
			D_FREE(prop);
			return rc;
		}
		*data = prop;
		return rc;
	case CRT_PROC_FREE:
		prop = *data;
		if (prop == NULL)
			return 0;
		if (prop->dpp_nr == 0 || prop->dpp_entries == NULL) {
			D_FREE(prop);
			return 0;
		}
		crt_proc_prop_entries(proc, proc_op, prop);
		D_FREE(prop->dpp_entries);
		D_FREE(prop);
		return 0;
	default:
		D_ERROR("bad proc_op %d.\n", proc_op);
		return -DER_INVAL;
	}
}
