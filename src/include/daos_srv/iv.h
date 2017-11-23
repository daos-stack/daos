/**
 * (C) Copyright 2017 Intel Corporation.
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
 * daos iv tree defination.
 */
#ifndef __DAOS_SRV_IV_H__
#define __DAOS_SRV_IV_H__

#include <abt.h>

/* DS iv cache entry */
struct ds_iv_key {
	d_rank_t	rank;
	uint32_t	key_id;
};

struct ds_iv_entry;
typedef int (*ds_iv_ent_alloc_t)(struct ds_iv_key *iv_key, void *data,
				 d_sg_list_t *sgl);
typedef int (*ds_iv_ent_get_t)(d_sg_list_t *sgl, struct ds_iv_entry *ent);
typedef int (*ds_iv_ent_put_t)(d_sg_list_t *sgl, struct ds_iv_entry *ent);
typedef int (*ds_iv_ent_destroy_t)(d_sg_list_t *sgl);
typedef int (*ds_iv_ent_fetch_t)(d_sg_list_t *dst, d_sg_list_t *src);
typedef int (*ds_iv_ent_update_t)(d_sg_list_t *dst, d_sg_list_t *src);
typedef int (*ds_iv_ent_refresh_t)(d_sg_list_t *dst, d_sg_list_t *src);

/* cache management ops */
struct ds_iv_entry_ops {
	/* Allocate IV cache entry. Note: entry itself will be destoryed when
	 * destroying the namespace. this is mostly for allocating value
	 * (ds_iv_entry->value)
	 */
	ds_iv_ent_alloc_t	iv_ent_alloc;
	/* Hold the refcount of the IV ent */
	ds_iv_ent_get_t		iv_ent_get;
	/* Put the refcount of the IV ent */
	ds_iv_ent_put_t		iv_ent_put;
	/* Destroy the IV cache entry */
	ds_iv_ent_destroy_t	iv_ent_destroy;
	/* Retrieve the value from IV cache entry */
	ds_iv_ent_fetch_t	iv_ent_fetch;
	/* Update the value of IV cache entry */
	ds_iv_ent_update_t	iv_ent_update;
	/* Refresh the value of IV cache entry */
	ds_iv_ent_refresh_t	iv_ent_refresh;
};

/* structure to describe the cache management for
 * each type of iv_value.
 */
struct ds_iv_key_type {
	struct ds_iv_entry_ops *iv_key_ops;
	daos_list_t	 iv_key_list;
	char		 iv_key_name[32];
	unsigned int	 iv_key_id;
};

/**
 * Each IV cache entry will be represented by this structure on
 * each node.
 */
struct ds_iv_entry {
	/* Cache management ops for the key */
	struct ds_iv_entry_ops	*ent_ops;
	/* key of the IV entry */
	daos_key_t		key;
	/* value of the IV entry */
	d_sg_list_t		value;
	/* link to the namespace */
	daos_list_t		link;
	unsigned int		ref;
	unsigned int		valid:1;
};

/* IV namespace will be per pool. The namespace will be created
 * during pool connection, and destroyed during pool disconntion.
 */
struct ds_iv_ns {
	d_rank_t	iv_master_rank;
	/* Different pool will use different ns id */
	unsigned int	iv_ns_id;
	/* Link to global ns list (ds_iv_list) */
	daos_list_t	iv_ns_link;

	/* Protect to the key list */
	ABT_mutex	iv_lock;
	/* all of keys under the ns links here */
	daos_list_t	iv_key_list;
	/* Cart IV namespace */
	crt_iv_namespace_t	iv_ns;
};

int
ds_iv_key_type_unregister(unsigned int key_id);

int
ds_iv_key_type_register(unsigned int key_id, struct ds_iv_entry_ops *ops);

#endif /* __DAOS_SRV_IV_H__ */
