/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Registered memory regions of the DAOS API. They are CaRT registered regions (see
 * crt_mem_register()); this file only maps the DAOS types to them.
 */
#define D_LOGFAC DD_FAC(client)

#include <daos/common.h>
#include <daos/object.h>
#include <daos_mem.h>
#include <cart/api.h>

int
daos_mem_register(void *buf, size_t len, const daos_mem_attr_t *attr, daos_mem_handle_t *mh)
{
	d_mem_dev_t mdev = {0};
	int         rc;

	if (attr != NULL) {
		if (attr->ma_handle != NULL) {
			D_ERROR("ma_handle must be empty\n");
			return -DER_INVAL;
		}
		rc = dc_obj_mem_attrs_valid(attr, NULL, 1);
		if (rc != 0)
			return rc;
		mdev.md_type      = (d_mem_type_t)attr->ma_mem_type;
		mdev.md_device_id = attr->ma_device_id;
	}

	return crt_mem_register(buf, len, &mdev, (crt_mem_handle_t *)mh);
}

int
daos_mem_bind(daos_mem_handle_t mh, daos_net_domain_t domain)
{
	if (mh == NULL || domain == NULL)
		return -DER_INVAL;

	/* a network domain is the CaRT context of an event queue today */
	return crt_mem_bind((crt_mem_handle_t)mh, (crt_context_t)domain);
}

int
daos_mem_deregister(daos_mem_handle_t mh)
{
	return crt_mem_deregister((crt_mem_handle_t)mh);
}
