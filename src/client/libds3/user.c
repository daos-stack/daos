/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */


#include "ds3_internal.h"

int
ds3_user_set(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_remove(const char *name, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get_by_email(const char *email, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get_by_key(const char *key, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}
