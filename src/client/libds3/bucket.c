/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, daos_prop_t *props, ds3_t *ds3,
		  daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_destroy(const char *name, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_close(ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_get_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_list_obj(daos_size_t *nobj, struct ds3_object_info *buf,
		    struct ds3_object_list_params *params, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}
