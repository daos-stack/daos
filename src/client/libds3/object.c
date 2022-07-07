/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_obj_create(const char *key, struct ds3_object_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_close(ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_get_info(struct ds3_object_info *info, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_set_info(struct ds3_object_info *info, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_destroy(const char *key, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_write(const void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}
