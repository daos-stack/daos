/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont: Container Client Internal Declarations
 */

#ifndef __CONTAINER_CLIENT_INTERNAL_H__
#define __CONTAINER_CLIENT_INTERNAL_H__

/* Client container handle */
struct dc_cont {
	/** link chain in the global handle hash table */
	struct d_hlink		dc_hlink;
	/* list to pool */
	d_list_t		dc_po_list;
	/* object list for this container */
	d_list_t		dc_obj_list;
	/* lock for list of dc_obj_list */
	pthread_rwlock_t	dc_obj_list_lock;
	/* uuid for this container */
	uuid_t			dc_uuid;
	uuid_t			dc_cont_hdl;
	uint64_t		dc_capas;
	/* pool handler of the container */
	daos_handle_t		dc_pool_hdl;
	struct daos_csummer    *dc_csummer;
	struct cont_props	dc_props;
	/* minimal pmap version */
	uint32_t		dc_min_ver;
	uint32_t		dc_closing:1,
				dc_slave:1; /* generated via g2l */
};

/* Per thread handle cache */
#define DC_CONT_CACHE_NR		4
static __thread struct dc_cont *dc_cont_local[DC_CONT_CACHE_NR];
static __thread uint64_t	dc_cont_cookie[DC_CONT_CACHE_NR];

static inline struct dc_cont *
dc_hdl2cont(daos_handle_t coh)
{
	struct d_hlink *hlink;
	struct dc_cont *dc_cont;
	int i;
	int insert_i = 0;

	for (i = 0; i < DC_CONT_CACHE_NR; i++) {
		if (dc_cont_cookie[i] && coh.cookie == dc_cont_cookie[i]) {
			daos_hhash_link_getref(&dc_cont_local[i]->dc_hlink);

			return dc_cont_local[i];
		}
		if (dc_cont_cookie[i] == 0)
			insert_i = i;
	}

	hlink = daos_hhash_link_lookup(coh.cookie);
	if (hlink == NULL)
		return NULL;

	dc_cont = container_of(hlink, struct dc_cont, dc_hlink);
	/* if there is no free slot, we replace first one to cache now*/
	dc_cont_local[insert_i] = dc_cont;
	dc_cont_cookie[insert_i] = coh.cookie;

	return dc_cont;
}

static inline void
dc_cont2hdl(struct dc_cont *dc, daos_handle_t *hdl)
{
	daos_hhash_link_getref(&dc->dc_hlink);
	daos_hhash_link_key(&dc->dc_hlink, &hdl->cookie);
}

void
dc_cont_hdl_unlink(struct dc_cont *dc)
{
	int i;

	daos_hhash_link_delete(&dc->dc_hlink);
	for (i = 0; i < DC_CONT_CACHE_NR; i++) {
		if (dc == dc_cont_local[i]) {
			dc_cont_local[i] = NULL;
			dc_cont_cookie[i] = 0;
		}

	}
}

void dc_cont_hdl_link(struct dc_cont *dc);

struct dc_cont *dc_cont_alloc(const uuid_t uuid);
void dc_cont_free(struct dc_cont *);
void dc_cont_put(struct dc_cont *dc);

#endif /* __CONTAINER_CLIENT_INTERNAL_H__ */
