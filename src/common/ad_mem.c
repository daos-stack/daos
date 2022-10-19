/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(common)

#include "ad_mem.h"

#define AD_MEM_DEBUG	0

static struct ad_arena *alloc_arena(struct ad_blob *blob, bool force);
static void free_arena(struct ad_arena *arena, bool force);
static struct ad_group *alloc_group(struct ad_arena *arena, bool force);
static void free_group(struct ad_group *grp, bool force);

static int blob_fini(struct ad_blob *blob);
static int blob_register_arena(struct ad_blob *blob, unsigned int arena_type,
			       struct ad_group_spec *specs, unsigned int specs_nr,
			       struct ad_tx *tx);
static int arena_reserve(struct ad_blob *blob, unsigned int type,
			 struct umem_store *store_extern, struct ad_arena **arena_p);
static int arena_tx_publish(struct ad_arena *arena, struct ad_tx *tx);
static void arena_debug_sorter(struct ad_arena *arena);
static inline int group_unit_avail(const struct ad_group_df *gd);

/* default group specs */
static struct ad_group_spec grp_specs_def[] = {
	{
		.gs_unit	= 64,
		.gs_count	= 512,
	},	/* group size = 32K */
	{
		.gs_unit	= 128,
		.gs_count	= 512,
	},	/* group size = 64K */
	{
		.gs_unit	= 256,
		.gs_count	= 512,
	},	/* group size = 128K */
	{
		.gs_unit	= 512,
		.gs_count	= 512,
	},	/* group size = 256K */
	{
		.gs_unit	= 1024,
		.gs_count	= 256,
	},	/* group size = 256K */
	{
		.gs_unit	= 2048,
		.gs_count	= 128,
	},	/* group size = 256K */
	{
		.gs_unit	= 4096,
		.gs_count	= 64,
	},	/* group size = 256K */
};

static struct ad_blob	*dummy_blob;

static inline void setbits64(uint64_t *bmap, int at, int bits)
{
	setbit_range((uint8_t *)bmap, at, at + bits - 1);
}

#define setbit64(bm, at)	setbits64(bm, at, 1)

static inline void clrbits64(uint64_t *bmap, int at, int bits)
{
	clrbit_range((uint8_t *)bmap, at, at + bits - 1);
}

#define clrbit64(bm, at)	clrbits64(bm, at, 1)

static inline bool isset64(uint64_t *bmap, int at)
{
	return isset((uint8_t *)bmap, at);
}

static struct ad_group *
group_df2ptr(const struct ad_group_df *gd)
{
	return (struct ad_group *)(unsigned long)gd->gd_back_ptr;
}

static struct ad_arena *
arena_df2ptr(const struct ad_arena_df *ad)
{
	return (struct ad_arena *)(unsigned long)ad->ad_back_ptr;
}

static void
group_addref(struct ad_group *grp)
{
	grp->gp_ref++;
}

static void
group_decref(struct ad_group *grp)
{
	D_ASSERT(grp->gp_ref > 0);
	grp->gp_ref--;
	if (grp->gp_ref == 0)
		free_group(grp, false);
}

static void
arena_addref(struct ad_arena *arena)
{
	arena->ar_ref++;
}

static void
arena_decref(struct ad_arena *arena)
{
	D_ASSERT(arena->ar_ref > 0);
	arena->ar_ref--;
	if (arena->ar_ref == 0)
		free_arena(arena, false);
}

void
blob_addref(struct ad_blob *blob)
{
	blob->bb_ref++;
}

void
blob_decref(struct ad_blob *blob)
{
	D_ASSERT(blob->bb_ref > 0);
	blob->bb_ref--;
	if (blob->bb_ref == 0) {
		if (blob->bb_dummy)
			dummy_blob = NULL;

		blob_fini(blob);
		D_FREE(blob);
	}
}

static void
blob_handle_release(struct ad_blob_handle bh)
{
	if (bh.bh_blob)
		blob_decref(bh.bh_blob);
}

static int
blob_bmap_size(struct ad_blob *blob)
{
	return (blob->bb_pgs_nr + 7) >> 3;
}

#define GROUP_LRU_SIZE	4096
#define ARENA_LRU_SIZE	1024

static int
blob_init(struct ad_blob *blob)
{
	char	*buf = NULL;
	int	 i;

	D_ASSERT(blob->bb_pgs_nr > 0);

	D_INIT_LIST_HEAD(&blob->bb_ars_lru);
	D_INIT_LIST_HEAD(&blob->bb_ars_rsv);
	D_INIT_LIST_HEAD(&blob->bb_gps_lru);
	D_INIT_LIST_HEAD(&blob->bb_gps_rsv);
	D_INIT_LIST_HEAD(&blob->bb_pgs_ckpt);
	D_INIT_LIST_HEAD(&blob->bb_pgs_extern);

	D_ALLOC(blob->bb_pages, blob->bb_pgs_nr * sizeof(*blob->bb_pages));
	if (!blob->bb_pages)
		goto failed;

	if (blob->bb_fd < 0) { /* Test only */
		/* NB: buffer must align with arena size, because ptr2addr() depends on
		 * this to find address of ad_arena.
		 */
		D_ALIGNED_ALLOC(buf, ARENA_SIZE, blob->bb_pgs_nr << ARENA_SIZE_BITS);
		if (!buf)
			goto failed;

		blob->bb_mmap = buf;
		for (i = 0; i < blob->bb_pgs_nr; i++) {
			blob->bb_pages[i].pa_rpg = buf + (i << ARENA_SIZE_BITS);
			blob->bb_pages[i].pa_cpg = NULL; /* reserved for future use */
		}
	} else {
		D_ASSERT(0); /* XXX remove this while integrating with other components. */
	}

	/* NB: ad_blob_df (superblock) is stored right after header of arena[0],
	 * so it does not require any special code to handle checkpoint of superblock.
	 */
	blob->bb_df = (struct ad_blob_df *)&blob->bb_pages[0].pa_rpg[ARENA_HDR_SIZE];

	/* bitmap for reserving arena */
	D_ALLOC(blob->bb_bmap_rsv, blob_bmap_size(blob));
	if (!blob->bb_bmap_rsv)
		goto failed;

	for (i = 0; i < ARENA_LRU_SIZE; i++) {
		struct ad_arena *arena;

		arena = alloc_arena(NULL, true);
		if (!arena)
			goto failed;

		blob->bb_ars_lru_size++;
		d_list_add(&arena->ar_link, &blob->bb_ars_lru);
	}

	for (i = 0; i < GROUP_LRU_SIZE; i++) {
		struct ad_group *group;

		group = alloc_group(NULL, true);
		if (!group)
			goto failed;

		blob->bb_gps_lru_size++;
		d_list_add(&group->gp_link, &blob->bb_gps_lru);
	}
	return 0;
failed:
	blob_fini(blob);
	return -DER_NOMEM;
}

static int
blob_fini(struct ad_blob *blob)
{
	struct ad_arena *arena;
	struct ad_group *group;

	D_DEBUG(DB_TRACE, "Finalizing blob\n");
	while ((group = d_list_pop_entry(&blob->bb_gps_rsv, struct ad_group, gp_link))) {
		D_ASSERT(group->gp_unpub);
		group_decref(group);
	}
	while ((arena = d_list_pop_entry(&blob->bb_ars_rsv, struct ad_arena, ar_link))) {
		D_ASSERT(arena->ar_unpub);
		arena_decref(arena);
	}

	while ((group = d_list_pop_entry(&blob->bb_gps_lru, struct ad_group, gp_link)))
		free_group(group, true);
	while ((arena = d_list_pop_entry(&blob->bb_ars_lru, struct ad_arena, ar_link)))
		free_arena(arena, true);

	if (blob->bb_bmap_rsv)
		D_FREE(blob->bb_bmap_rsv);
	if (blob->bb_pages)
		D_FREE(blob->bb_pages);
	if (blob->bb_mmap)
		D_FREE(blob->bb_mmap);
	return 0;
}

static int
blob_load(struct ad_blob *blob)
{
	struct ad_blob_df *bd = blob->bb_df;
	int		   i;
	int		   rc = 0;

	for (i = 0; i < blob->bb_pgs_nr; i++) {
		struct ad_page		*page;
		struct umem_store_iod	 iod;
		d_sg_list_t		 sgl;
		d_iov_t			 iov;

		page = &blob->bb_pages[i];
		ad_iod_set(&iod, blob_addr(blob) + ARENA_SIZE * i, ARENA_SIZE);
		ad_sgl_set(&sgl, &iov, page->pa_rpg, ARENA_SIZE);

		/* XXX: submit multiple pages, otherwise it's too slow */
		rc = blob->bb_store.stor_ops->so_read(&blob->bb_store, &iod, &sgl);
		if (rc) {
			D_ERROR("Failed to load storage contents: %d\n", rc);
			goto out;
		}
	}

	/* NB: @bd points to the first page, its content has been brought in by the above read.*/
	for (i = 0; i < bd->bd_asp_nr; i++)
		blob->bb_arena_last[i] = bd->bd_asp[i].as_arena_last;
out:
	return rc;
}

static uint64_t
blob_incarnation(struct ad_blob *blob)
{
	return blob->bb_df->bd_incarnation;
}

static void
blob_set_opened(struct ad_blob *blob)
{
	struct ad_blob_df *bd = blob->bb_df;

	bd->bd_incarnation = d_timeus_secdiff(0);
	bd->bd_back_ptr	   = (unsigned long)blob;

	blob->bb_opened = 1;
	if (blob->bb_dummy) {
		D_ASSERT(!dummy_blob);
		dummy_blob = blob;
	}
}

/**
 * API design is a little bit confusing for now, the process is like:
 * - VOS calls umem_pool_create()->blob_prep_create()
 * - VOS initialize BIO context
 * - VOS calls blob_post_create() to read blob (needs BIO context)
 */
int
ad_blob_prep_create(char *path, daos_size_t size, struct ad_blob_handle *bh)
{
	struct ad_blob	*blob;
	bool		 is_dummy = false;

	if (!strcmp(path, DUMMY_BLOB)) {
		if (dummy_blob)
			return -DER_EXIST;
		is_dummy = true;
	}
	D_ASSERT(is_dummy); /* XXX the only thing can be supported now */

	D_ALLOC_PTR(blob);
	if (!blob)
		return -DER_NOMEM;

	blob->bb_store.stor_size = size;
	blob->bb_fd  = -1; /* XXX, create file, falloc */
	blob->bb_ref = 1;
	blob->bb_dummy = is_dummy;

	bh->bh_blob = blob;
	return 0;
}

/**
 * Format superbock of the blob, create the first arena, write these metadata to storage.
 * NB: superblock is stored in the first arena.
 */
int
ad_blob_post_create(struct ad_blob_handle bh)
{
	struct ad_blob		*blob = bh.bh_blob;
	struct umem_store	*store = &blob->bb_store;
	struct ad_blob_df	*bd;
	struct ad_arena		*arena;
	struct umem_store_iod	 iod;
	d_sg_list_t		 sgl;
	d_iov_t			 iov;
	int			 rc;

	/* NB: store::stor_size can be changed by upper level stack */
	blob->bb_pgs_nr = ((blob_size(blob) + ARENA_SIZE_MASK) >> ARENA_SIZE_BITS);

	rc = blob_init(blob);
	if (rc)
		return rc;

	bd = blob->bb_df;
	bd->bd_magic		= BLOB_MAGIC;
	bd->bd_version		= AD_MEM_VERSION;
	bd->bd_addr		= store->stor_addr;
	bd->bd_size		= blob_size(blob);
	bd->bd_arena_size	= ARENA_SIZE;

	/* register the default arena */
	rc = blob_register_arena(blob, 0, grp_specs_def, ARRAY_SIZE(grp_specs_def), NULL);
	D_ASSERT(rc == 0); /* no reason to fail */

	/* create arena 0 (ad_blob_df is stored in the first 32K of it) */
	rc = arena_reserve(blob, 0, NULL, &arena);
	D_ASSERT(rc == 0);
	D_ASSERT(arena->ar_df);
	D_ASSERT(arena->ar_df->ad_id == 0);

	/* NB: no transaction, write arena[0] and super block to storage straight away */
	rc = arena_tx_publish(arena, NULL);
	if (rc)
		goto failed;

	arena->ar_unpub = false;

	blob->bb_arena_last[0]	= bd->bd_asp[0].as_arena_last;
	/* already published arena[0], clear the reserved bit */
	clrbit64(blob->bb_bmap_rsv, arena->ar_df->ad_id);

	ad_iod_set(&iod, blob_ptr2addr(blob, arena->ar_df), ARENA_HDR_SIZE + BLOB_HDR_SIZE);
	ad_sgl_set(&sgl, &iov, arena->ar_df, ARENA_HDR_SIZE + BLOB_HDR_SIZE);

	D_ASSERT(store->stor_ops);
	rc = store->stor_ops->so_write(store, &iod, &sgl);
	if (rc) {
		D_ERROR("Failed to write ad_mem superblock\n");
		goto failed;
	}
	arena_decref(arena);

	D_DEBUG(DB_TRACE, "Ad-hoc memory blob created\n");
	blob_set_opened(blob);
	return 0;
failed:
	if (arena)
		arena_decref(arena);
	blob_handle_release(bh);
	return rc;
}

int
ad_blob_prep_open(char *path, struct ad_blob_handle *bh)
{
	struct ad_blob	*blob;
	bool		 is_dummy = false;

	if (!strcmp(path, DUMMY_BLOB))
		is_dummy = true;

	if (!is_dummy) {
		/* XXX dummy is the only thing can be supported now */
		D_ASSERT(0);
	} else {
		if (dummy_blob) {
			blob = dummy_blob;
			blob_addref(blob);
		} else {
			D_ALLOC_PTR(blob);
			if (!blob)
				return -DER_NOMEM;

			blob->bb_fd = -1;
			blob->bb_ref = 1;
			blob->bb_dummy = true;
		}
	}
	bh->bh_blob = blob;
	return 0;
}

int
ad_blob_post_open(struct ad_blob_handle bh)
{
	struct ad_blob		*blob  = bh.bh_blob;
	struct umem_store	*store = &blob->bb_store;
	struct ad_blob_df	*bd;
	struct umem_store_iod	 iod;
	d_sg_list_t		 sgl;
	d_iov_t			 iov;
	int			 rc;

	if (blob->bb_opened)
		return 0;

	D_ALLOC_PTR(bd);
	if (!bd)
		return -DER_NOMEM;

	/* blob header is stored right after header of arena[0] */
	ad_iod_set(&iod, blob->bb_store.stor_addr + ARENA_HDR_SIZE, sizeof(*bd));
	ad_sgl_set(&sgl, &iov, bd, sizeof(*bd));

	/* read super block to temporary buffer */
	D_ASSERT(store->stor_ops);
	rc = store->stor_ops->so_read(store, &iod, &sgl);
	if (rc) {
		D_ERROR("Failed to read superblock of ad_mem\n");
		goto failed;
	}

	if (bd->bd_magic != BLOB_MAGIC || bd->bd_version == 0) {
		D_ERROR("Invalid superblock: magic=%x, version=%d\n",
			bd->bd_magic, bd->bd_version);
		goto failed;
	}

	store->stor_size = bd->bd_size;
	store->stor_addr = bd->bd_addr;
	blob->bb_pgs_nr = ((blob_size(blob) + ARENA_SIZE_MASK) >> ARENA_SIZE_BITS);

	rc = blob_init(blob);
	if (rc)
		goto failed;

	rc = blob_load(blob);
	if (rc)
		goto failed;

	blob_set_opened(blob);
	D_FREE(bd);
	return 0;
failed:
	blob_handle_release(bh);
	if (bd)
		D_FREE(bd);
	return rc;
}

int
ad_blob_close(struct ad_blob_handle bh)
{
	blob_decref(bh.bh_blob);
	return 0;
}

int
ad_blob_destroy(struct ad_blob_handle bh)
{
	/* XXX: remove the file */
	ad_blob_close(bh);
	return 0;
}

struct umem_store *
ad_blob_hdl2store(struct ad_blob_handle bh)
{
	D_ASSERT(bh.bh_blob);
	return &bh.bh_blob->bb_store;
}

void *
blob_addr2ptr(struct ad_blob *blob, daos_off_t addr)
{
	struct ad_page	*pg;
	daos_off_t	 off;

	off  = addr & ARENA_SIZE_MASK;
	addr = addr - blob_addr(blob);
	pg   = &blob->bb_pages[addr >> ARENA_SIZE_BITS];
	return &pg->pa_rpg[off];
}

/** convert storage address to mapped memory address */
void *
ad_addr2ptr(struct ad_blob_handle bh, daos_off_t addr)
{
	return blob_addr2ptr(bh.bh_blob, addr);
}

daos_off_t
blob_ptr2addr(struct ad_blob *blob, void *ptr)
{
	struct ad_arena_df *ad;
	daos_off_t	    off;

	off = (unsigned long)ptr & ARENA_SIZE_MASK;
	ad = (struct ad_arena_df *)((unsigned long)ptr & ~ARENA_SIZE_MASK);
	return ad->ad_addr + off;
}

/** convert mapped memory address to storage address */
daos_off_t
ad_ptr2addr(struct ad_blob_handle bh, void *ptr)
{
	return blob_ptr2addr(bh.bh_blob, ptr);
}

static int
group_size_cmp(const void *p1, const void *p2)
{
	const struct ad_group_df *gd1 = p1;
	const struct ad_group_df *gd2 = p2;
	int			  f1;
	int			  f2;

	if (gd1->gd_unit < gd2->gd_unit)
		return -1;
	if (gd1->gd_unit > gd2->gd_unit)
		return 1;

	f1 = group_unit_avail(gd1);
	f2 = group_unit_avail(gd2);
	if (f1 < f2)
		return -1;
	if (f1 > f2)
		return 1;

	/* use address to identify a specified group */
	if (gd1->gd_addr < gd2->gd_addr)
		return -1;
	if (gd1->gd_addr > gd2->gd_addr)
		return 1;
	return 0;
}

static int
group_addr_cmp(const void *p1, const void *p2)
{
	const struct ad_group_df *gd1 = p1;
	const struct ad_group_df *gd2 = p2;

	if (gd1->gd_addr < gd2->gd_addr)
		return -1;
	if (gd1->gd_addr > gd2->gd_addr)
		return 1;

	/* two groups have the same address? */
	D_ASSERTF(0, "Two groups cannot have the same address\n");
	return 0;
}

static int
arena_find(struct ad_blob *blob, bool create, uint32_t arena_id, struct ad_arena_df **ad_p)
{
	struct ad_blob_df  *bd = blob->bb_df;
	bool		    created;
	bool		    reserved;

	if ((arena_id << ARENA_SIZE_BITS) > blob_size(blob))
		return -DER_INVAL;

	created = isset64(bd->bd_bmap, arena_id);
	reserved = isset64(blob->bb_bmap_rsv, arena_id);
	if (create == (created || reserved))
		return create ? -DER_EXIST : -DER_NONEXIST;

	/* Arena is the header of each page */
	*ad_p = (struct ad_arena_df *)blob->bb_pages[arena_id].pa_rpg;
	return 0;
}

int
arena_load(struct ad_blob *blob, uint32_t arena_id, struct ad_arena **arena_p)
{
	struct ad_arena	    *arena = NULL;
	struct ad_arena_df  *ad = NULL;
	struct ad_group_df  *gd;
	int		     i;
	int		     rc;

	rc = arena_find(blob, false, arena_id, &ad);
	if (rc) {
		D_ERROR("No available arena\n");
		return rc;
	}

	if (ad->ad_magic != ARENA_MAGIC) {
		D_ERROR("Invalid arena magic: %x/%x\n", ad->ad_magic, ARENA_MAGIC);
		return -DER_PROTO;
	}

	if (ad->ad_incarnation != blob_incarnation(blob)) {
		ad->ad_incarnation = blob_incarnation(blob);
		ad->ad_back_ptr = 0; /* clear stale back-pointer to DRAM */
	}

	if (ad->ad_back_ptr) {
		arena = arena_df2ptr(ad);
		D_ASSERT(arena->ar_df == ad);
		arena->ar_ref++;
		if (arena->ar_ref == 1) { /* remove from LRU */
			d_list_del_init(&arena->ar_link);
			blob->bb_ars_lru_size--;
		}
		goto out;
	}
	/* no cached arena, allocate it now */

	arena = alloc_arena(blob, false);
	if (!arena)
		return -DER_NOMEM;

	/* NB: stale pointer can be detected by incarnation. */
	ad->ad_back_ptr = (unsigned long)arena;

	arena->ar_ref	 = 1; /* for caller */
	arena->ar_df	 = ad;
	arena->ar_type	 = ad->ad_type;
	arena->ar_grp_nr = ad->ad_grp_nr;

	for (i = 0; i < ad->ad_grp_nr; i++) {
		gd = &ad->ad_groups[i];
		if (gd->gd_incarnation != blob_incarnation(blob)) {
			/* reset stale backpointer */
			gd->gd_incarnation = blob_incarnation(blob);
			gd->gd_back_ptr = 0;
		}
		arena->ar_size_sorter[i] = arena->ar_addr_sorter[i] = gd;
	}

	if (ad->ad_grp_nr > 0) {
		qsort(arena->ar_size_sorter, ad->ad_grp_nr, sizeof(gd), group_size_cmp);
		qsort(arena->ar_addr_sorter, ad->ad_grp_nr, sizeof(gd), group_addr_cmp);
	}
out:
	if (arena_p)
		*arena_p = arena;
	return 0;
}

/** Reserve a new arena for the specified type */
static int
arena_reserve(struct ad_blob *blob, unsigned int type, struct umem_store *store_extern,
	      struct ad_arena **arena_p)
{
	struct ad_blob_df	*bd = blob->bb_df;
	struct ad_arena_df	*ad;
	struct ad_arena		*arena;
	uint32_t		 id;
	int			 rc;

	D_ASSERT(store_extern == NULL); /* XXX: not supported yet */
	if (store_extern && store_extern->stor_addr == 0) {
		/* cannot register an arena starts from 0 address, because 0 is reserved
		 * for failed allocation.
		 */
		return -DER_INVAL;
	}

	D_ASSERT(bd->bd_asp_nr == 1); /* XXX only support one type for now */
	D_ASSERT(type < bd->bd_asp_nr);

	if (blob->bb_arena_last[type] != AD_ARENA_ANY)
		id = blob->bb_arena_last[type] + 1;
	else
		id = 0; /* the first one */

	rc = arena_find(blob, true, id, &ad);
	if (rc) {
		D_ERROR("Arena ID is occupied: %u\n", id);
		return rc;
	}

	D_DEBUG(DB_TRACE, "Reserved a new arena: type=%d, id=%d\n", type, id);
	blob->bb_arena_last[type] = id;
	D_ASSERT(ad->ad_magic != ARENA_MAGIC);

	/* This is new memory, no need to undo, arena_tx_publish() will add them to WAL */
	memset(ad, 0, sizeof(*ad));
	ad->ad_id	    = id;
	ad->ad_type	    = type;
	ad->ad_magic	    = ARENA_MAGIC;
	ad->ad_size	    = ARENA_SIZE;
	ad->ad_unit	    = ARENA_UNIT_SIZE;
	ad->ad_addr	    = blob_addr(blob) + id * ARENA_SIZE;
	ad->ad_incarnation  = blob_incarnation(blob);

	D_CASSERT(ARENA_UNIT_SIZE == ARENA_HDR_SIZE);
	/* the first bit (representing 32K) is reserved for arena header */
	setbit64(ad->ad_bmap, 0);

	D_CASSERT(ARENA_UNIT_SIZE == BLOB_HDR_SIZE);
	if (id == 0) { /* the first arena */
		/* Blob header(superblock) is stored in arena zero, it consumes 32K
		 * which is the same as unit size of arena.
		 * NB: the first arena is written straightway, no WAL
		 */
		setbit64(ad->ad_bmap, 1);
	}
	/* DRAM only operation, mark the arena as reserved */
	D_ASSERT(!isset64(blob->bb_bmap_rsv, id));
	setbit64(blob->bb_bmap_rsv, id);

	rc = arena_load(blob, id, &arena);
	D_ASSERT(rc == 0);

	arena->ar_unpub = true;
	*arena_p = arena;
	return 0;
}

/** Publish a reserved arena */
static int
arena_tx_publish(struct ad_arena *arena, struct ad_tx *tx)
{
	struct ad_blob		*blob	= arena->ar_blob;
	struct ad_blob_df	*bd	= blob->bb_df;
	struct ad_arena_df	*ad	= arena->ar_df;
	struct ad_arena_spec	*spec	= &bd->bd_asp[ad->ad_type];

	D_DEBUG(DB_TRACE, "publishing arena=%d\n", ad->ad_id);
	ad_tx_setbits(tx, bd->bd_bmap, ad->ad_id, 1);

	if (spec->as_arena_last == AD_ARENA_ANY || spec->as_arena_last < ad->ad_id) {
		ad_tx_assign(tx, &spec->as_arena_last, sizeof(spec->as_arena_last), ad->ad_id);
		D_DEBUG(DB_TRACE, "Published arena type = %u, ID = %u\n",
			ad->ad_type, spec->as_arena_last);
	}

	ad_tx_set(tx, ad, 0, sizeof(*ad), AD_TX_REDO | AD_TX_LOG_ONLY);
	ad_tx_snap(tx, ad, offsetof(struct ad_arena_df, ad_bmap[0]), AD_TX_REDO);
	return 0;
}

/** Convert size to group specification. */
struct ad_group_spec *
arena_size2gsp(struct ad_arena *arena, daos_size_t size, int *spec_id)
{
	struct ad_blob	     *bb  = arena->ar_blob;
	struct ad_arena_df   *ad  = arena->ar_df;
	struct ad_arena_spec *asp = &bb->bb_df->bd_asp[ad->ad_type];
	struct ad_group_spec *gsp = NULL;
	int		      start;
	int		      end;
	int		      cur;
	int		      len;
	int		      rc;

	len = asp->as_specs_nr;
	D_ASSERT(len > 0);

	/* check if there is a customized group for the size */
	for (start = cur = 0, end = len - 1; start <= end; ) {
		cur = (start + end) / 2;
		gsp = &asp->as_specs[cur];
		if (gsp->gs_unit < size)
			rc = -1;
		else if (gsp->gs_unit > size)
			rc = 1;
		else
			break;

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	D_ASSERT(gsp);

	if (gsp->gs_unit < size) {
		if (cur < len - 1) {
			cur += 1;
			gsp = &asp->as_specs[cur];
		} else {
			D_ERROR("size is too large: %d\n", (int)size);
			gsp = NULL;
		}
	}

	if (gsp) { /* matched */
		D_DEBUG(DB_TRACE, "Found spec: spec_unit=%d, size=%d\n",
			(int)gsp->gs_unit, (int)size);
		if (spec_id)
			*spec_id = cur;
	}
	return gsp;
}

/** number of available units in a group, reserved units is counted as occupied */
static inline int
group_unit_avail(const struct ad_group_df *gd)
{
	struct ad_group	*grp;
	int		 units;

	units = gd->gd_unit_free;
	grp = group_df2ptr(gd);
	if (grp) {
		D_ASSERTF(units >= grp->gp_unit_rsv, "grp(%p), gd(%p), reserved=%d, free=%d\n",
			  grp, gd, grp->gp_unit_rsv, units);
		units -= grp->gp_unit_rsv;
	}
	return units;
}

static int
group_load(struct ad_group_df *gd, struct ad_arena *arena, struct ad_group **group_p)
{
	struct ad_group *grp;

	if (gd->gd_back_ptr) {
		if (gd->gd_incarnation == blob_incarnation(arena->ar_blob)) {
			grp = group_df2ptr(gd);
			grp->gp_ref++;
			if (grp->gp_ref == 1) { /* remove from LRU */
				d_list_del_init(&grp->gp_link);
				arena->ar_blob->bb_gps_lru_size--;
			}
			goto out;
		}
		gd->gd_back_ptr = 0;
	}

	grp = alloc_group(arena, false);
	if (!grp)
		return -DER_NOMEM;

	gd->gd_incarnation = blob_incarnation(arena->ar_blob);
	gd->gd_back_ptr = (unsigned long)grp;
	grp->gp_ref = 1;
	grp->gp_df = gd;
out:
	*group_p = grp;
	return 0;
}

/** Find a group with free space for the requested allocate size @size */
struct ad_group *
arena_find_grp(struct ad_arena *arena, daos_size_t size, int *pos)
{
	struct ad_group_df    *gd = NULL;
	struct ad_group_spec  *gsp;
	struct ad_group	      *grp;
	int		       start;
	int		       end;
	int		       len;
	int		       cur;
	int		       rc;

	len = arena->ar_grp_nr;
	if (len == 0) /* no group */
		return NULL;

	gsp = arena_size2gsp(arena, size, NULL);
	if (!gsp) {
		D_ASSERT(0);
		D_ERROR("Cannot find matched group specification\n");
		return NULL;
	}

	if (gsp->gs_unit != size) { /* no customized size, use the generic one */
		D_ASSERT(size < gsp->gs_unit);
		size = gsp->gs_unit;
	}

	/* binary search to find a group */
	for (start = cur = 0, end = len - 1; start <= end; ) {
		cur = (start + end) / 2;
		gd = arena->ar_size_sorter[cur];
		if (gd->gd_unit == size) {
			int units = group_unit_avail(gd);

			/* always try to use the group with the least free units */
			if (units == 1)
				goto found;

			if (units == 0)
				rc = -1;
			else
				rc = 1;

		} else if (gd->gd_unit < size) {
			rc = -1;
		} else {
			rc = 1;
		}

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	D_DEBUG(DB_TRACE, "matched grp=%p, unit=%d, size=%d\n", gd, gd->gd_unit, (int)size);

	while (gd->gd_unit <= size) {
		if (gd->gd_unit == size && group_unit_avail(gd) > 0)
			goto found;

		if (++cur == len) /* no more group */
			break;

		gd = arena->ar_size_sorter[cur];
	}
	return NULL;
found:
	rc = group_load(gd, arena, &grp);
	if (rc)
		return NULL;

	*pos = cur;
	return grp;
}

/** Locate the associated group for the provided address @addr */
struct ad_group *
arena_addr2grp(struct ad_arena *arena, daos_off_t addr)
{
	struct ad_group_df *gd = NULL;
	struct ad_group	   *grp;
	int		    start;
	int		    end;
	int		    cur;
	int		    rc;
	bool		    found = false;

	/* binary search, should be quick */
	for (start = cur = 0, end = arena->ar_grp_nr - 1; start <= end; ) {
		daos_size_t	size;

		cur = (start + end) / 2;
		gd = arena->ar_addr_sorter[cur];

		size = gd->gd_unit_nr * gd->gd_unit;
		if (gd->gd_addr <= addr && gd->gd_addr + size > addr) {
			found = true;
			break;
		}

		if (gd->gd_addr + size <= addr) {
			rc = -1;
		} else {
			D_ASSERT(gd->gd_addr > addr);
			rc = 1;
		}

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	D_ASSERT(found);
	rc = group_load(gd, arena, &grp);
	if (rc)
		return NULL;

	/* this function is called from tx_free(), only published group can actually
	 * allocate and free space.
	 */
	D_ASSERT(!grp->gp_unpub);
	return grp;
}

/* Locate group position in the size-sorter */
static int
arena_locate_grp(struct ad_arena *arena, struct ad_group *group)
{
	struct ad_group_df  *gd = group->gp_df;
	struct ad_group_df  *tmp = NULL;
	int		     avail;
	int		     start;
	int		     end;
	int		     cur;
	int		     rc;

	avail = group_unit_avail(gd);
	/* binary search by @group->gd_unit to find @group */
	for (start = cur = 0, end = arena->ar_grp_nr - 1; start <= end; ) {
		cur = (start + end) / 2;
		tmp = arena->ar_size_sorter[cur];
		/* the same unit size */
		if (tmp->gd_unit == gd->gd_unit) {
			int	n;

			if (tmp == gd) /* found */
				return cur;

			n = group_unit_avail(tmp);
			if (n < avail)
				rc = -1;
			else if (n > avail)
				rc = 1;
			else /* group address */
				rc = tmp->gd_addr < gd->gd_addr ? -1 : 1;

		} else if (tmp->gd_unit < gd->gd_unit) {
			rc = -1;
		} else {
			rc = 1;
		}

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	D_ASSERT(0);
	return -1;
}

/* Adjust group position in the size-sorter after alloc/reserve/free (for binary search) */
static void
arena_reorder_grp(struct ad_arena *arena, struct ad_group *group, int pos, bool is_alloc)
{
	struct ad_group_df **sorter = arena->ar_size_sorter;
	struct ad_group_df  *gd = group->gp_df;
	struct ad_group_df  *tmp = NULL;
	int		     gd_units;
	int		     tmp_units;
	int		     i;

	gd_units = group_unit_avail(gd);
	/* swap pointers and order groups after alloc/free */
	if (is_alloc) { /* shift left */
		for (i = pos; i > 0;) {
			tmp = sorter[--i];
			if (tmp->gd_unit != gd->gd_unit) {
				D_ASSERT(tmp->gd_unit < gd->gd_unit);
				break;
			}

			tmp_units = group_unit_avail(tmp);
			if (tmp_units < gd_units ||
			    (tmp_units == gd_units && tmp->gd_addr < gd->gd_addr))
				break;

			sorter[pos] = tmp;
			sorter[i] = gd;
			pos = i;
		}
	} else { /* shift right */
		for (i = pos; i < arena->ar_grp_nr - 1;) {
			tmp = sorter[++i];
			if (tmp->gd_unit != gd->gd_unit) {
				D_ASSERT(tmp->gd_unit > gd->gd_unit);
				break;
			}

			tmp_units = group_unit_avail(tmp);
			if (tmp_units > gd_units ||
			    (tmp_units == gd_units && tmp->gd_addr > gd->gd_addr))
				break;

			sorter[pos] = tmp;
			sorter[i] = gd;
			pos = i;
		}
	}
}

static void
arena_debug_sorter(struct ad_arena *arena)
{
#if AD_MEM_DEBUG
	struct ad_group_df *gd;
	struct ad_group    *grp;
	int		    cur;

	D_DEBUG(DB_TRACE, "arena[%d]=%p, groups=%d\n", arena->ar_df->ad_id,
		arena, arena->ar_grp_nr);
	D_DEBUG(DB_TRACE, "size sorted groups:\n");
	for (cur = 0; cur < arena->ar_grp_nr; cur++) {
		gd = arena->ar_size_sorter[cur];
		grp = group_df2ptr(gd);
		D_DEBUG(DB_TRACE,
			"group[%d]=%p, arena=%p, size=%d, addr=%lx, avail=%d, pub=%d\n",
			cur, gd, grp->gp_arena, gd->gd_unit, (unsigned long)gd->gd_addr,
			group_unit_avail(gd), !grp->gp_unpub);
	}

	D_DEBUG(DB_TRACE, "address sorted groups:\n");
	for (cur = 0; cur < arena->ar_grp_nr; cur++) {
		gd = arena->ar_addr_sorter[cur];
		grp = group_df2ptr(gd);
		D_DEBUG(DB_TRACE,
			"group[%d]=%p, arena=%p, size=%d, addr=%lx, avail=%d, pub=%d\n",
			cur, gd, grp->gp_arena, gd->gd_unit, (unsigned long)gd->gd_addr,
			group_unit_avail(gd), !grp->gp_unpub);
	}
#endif
}

/**
 * add a new group to arena, this function inserts the new group into two arrays to support
 * binary search:
 * - the first array is for searching by size and available units (alloc)
 * - the second array is for searching by address (free)
 */
static void
arena_add_grp(struct ad_arena *arena, struct ad_group *grp, int *pos)
{
	struct ad_group_df **size_sorter = arena->ar_size_sorter;
	struct ad_group_df **addr_sorter = arena->ar_addr_sorter;
	struct ad_group_df  *gd   = grp->gp_df;
	struct ad_group_df  *tmp  = NULL;
	int		     avail;
	int		     start;
	int		     end;
	int		     cur;
	int		     len;
	int		     rc;

	/* no WAL, in DRAM */
	len = arena->ar_grp_nr++;
	if (len == 0) {
		addr_sorter[0] = gd;
		size_sorter[0] = gd;
		if (pos)
			*pos = 0;
		return;
	}

	D_DEBUG(DB_TRACE, "Adding a new group to address sorter\n");
	/* step-1: add the group the address sorter */
	for (start = cur = 0, end = len - 1; start <= end; ) {
		cur = (start + end) / 2;
		tmp = addr_sorter[cur];
		if (tmp->gd_addr < gd->gd_addr) {
			rc = -1;
		} else if (tmp->gd_addr > gd->gd_addr) {
			rc = 1;
		} else {
			arena_debug_sorter(arena);
			D_ASSERT(0);
		}

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	if (tmp->gd_addr < gd->gd_addr)
		cur += 1;

	if (cur < len) {
		memmove(&addr_sorter[cur + 1], &addr_sorter[cur],
			(len - cur) * sizeof(addr_sorter[0]));
	}
	addr_sorter[cur] = gd;

	/* step-2: add the group the size sorter */
	D_DEBUG(DB_TRACE, "Adding a new group to size sorter\n");
	avail = group_unit_avail(gd);
	for (start = cur = 0, end = len - 1; start <= end; ) {
		cur = (start + end) / 2;
		tmp = size_sorter[cur];
		if (tmp->gd_unit == gd->gd_unit) {
			int	n = group_unit_avail(tmp);

			if (n < avail)
				rc = -1;
			else if (n > avail)
				rc = 1;
			else
				rc = (tmp->gd_addr < gd->gd_addr) ? -1 : 1;

		} else if (tmp->gd_unit < gd->gd_unit) {
			rc = -1;
		} else {
			rc = 1;
		}

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}

	if (tmp->gd_unit < gd->gd_unit ||
	    (tmp->gd_unit == gd->gd_unit && tmp->gd_addr < gd->gd_addr))
		cur += 1;

	if (cur < len) {
		memmove(&size_sorter[cur + 1], &size_sorter[cur],
			(len - cur) * sizeof(size_sorter[0]));
	}

	if (pos)
		*pos = cur;
	size_sorter[cur] = gd;
	D_DEBUG(DB_TRACE, "Added a new group to arena=%d\n", arena->ar_df->ad_id);
	arena_debug_sorter(arena);
}

/** Find requested number of unused bits (neither set it @used or @reserved */
static int
find_bits(uint64_t *used, uint64_t *reserved, int bmap_sz, int bits_min, int *bits)
{
	int	nr_saved;
	int	at_saved;
	int	nr;
	int	at;
	int	i;
	int	j;

	nr = nr_saved = 0;
	at = at_saved = -1;

	for (i = 0; i < bmap_sz; i++) {
		uint64_t free_bits = ~used[i];

		if (reserved)
			free_bits &= ~reserved[i];

		if (free_bits == 0) { /* no space in the current int64 */
			if (nr > nr_saved) {
				nr_saved = nr;
				at_saved = at;
			}
			nr = 0;
			at = -1;
			continue;
		}

		j = ffsll(free_bits);
		D_ASSERT(j > 0);
		if (at >= 0 && j == 1) {
			D_ASSERT(nr > 0);
			nr++;
		} else {
			at = i * 64 + j - 1;
			nr = 1;
		}

		for (; j < 64; j++) {
			if (nr == *bits) /* done */
				goto out;

			if (isset64(&free_bits, j)) {
				nr++;
				continue;
			}

			if (nr > nr_saved) {
				nr_saved = nr;
				at_saved = at;
			}
			nr = 0;
			at = -1;
			if ((free_bits >> j) == 0)
				break;
		}
		if (nr == *bits)
			goto out;
	}
 out:
	if (nr == *bits || nr > nr_saved) {
		nr_saved = nr;
		at_saved = at;
	}

	if (nr_saved >= bits_min)
		*bits = nr_saved;
	else
		at_saved = -1;

	return at_saved;
}

/** reserve a new group within @arena */
static int
arena_reserve_grp(struct ad_arena *arena, daos_size_t size, int *pos,
		  struct ad_group **grp_p)
{
	struct ad_blob		*blob = arena->ar_blob;
	struct ad_arena_df	*ad = arena->ar_df;
	struct ad_group_spec	*gsp;
	struct ad_group_df	*gd;
	struct ad_group		*grp;
	int			 bits;
	int			 bits_min;
	int			 bit_at;

	gsp = arena_size2gsp(arena, size, NULL);
	if (!gsp) {
		D_ERROR("No matched group spec for size=%d\n", (int)size);
		return -DER_INVAL;
	}

	bits = (gsp->gs_unit * gsp->gs_count) >> GRP_SIZE_SHIFT;
	D_ASSERT(bits >= 1);

	/* at least 8 units within a group */
	bits_min = (gsp->gs_unit * 8) >> GRP_SIZE_SHIFT;
	if (bits_min == 0)
		bits_min = 1;
	if (bits_min > bits)
		bits_min = bits;

	bit_at = find_bits(ad->ad_bmap, arena->ar_bmap_rsv, ARENA_GRP_BMSZ, bits_min, &bits);
	if (bit_at < 0)
		return -DER_NOSPACE;

	D_ASSERT(bits >= bits_min);
	grp = alloc_group(arena, false);
	if (!grp)
		return -DER_NOMEM;

	gd = &ad->ad_groups[arena->ar_grp_nr];

	gd->gd_addr	   = ad->ad_addr + ARENA_HDR_SIZE + (bit_at << GRP_SIZE_SHIFT);
	gd->gd_unit	   = gsp->gs_unit;
	gd->gd_unit_nr	   = ((bits << GRP_SIZE_SHIFT) + gd->gd_unit - 1) / gd->gd_unit;
	gd->gd_unit_free   = gd->gd_unit_nr;
	gd->gd_back_ptr	   = (unsigned long)grp;
	gd->gd_incarnation = blob_incarnation(blob);

	grp->gp_unpub	= true;
	grp->gp_ref	= 1;
	grp->gp_df	= gd;

	D_DEBUG(DB_TRACE, "Reserve new group: bit_at=%d, bits=%d, size=%d\n",
		bit_at, bits, (int)size);

	setbits64(arena->ar_bmap_rsv, bit_at, bits);
	arena_add_grp(arena, grp, pos);
	if (grp_p)
		*grp_p = grp;

	return 0;
}

/** publish a reserved group */
static int
group_tx_publish(struct ad_group *group, struct ad_tx *tx)
{
	struct ad_arena		*arena = group->gp_arena;
	struct ad_arena_df	*ad = arena->ar_df;
	struct ad_group_df	*gd = group->gp_df;
	int			 bit_at;
	int			 bit_nr;

	bit_at = (gd->gd_addr - ad->ad_addr - ARENA_HDR_SIZE) >> GRP_SIZE_SHIFT;
	bit_nr = (gd->gd_unit_nr * gd->gd_unit) >> GRP_SIZE_SHIFT;
	D_DEBUG(DB_TRACE, "publishing group=%p, bit_at=%d, bits_nr=%d\n", group, bit_at, bit_nr);

	ad_tx_setbits(tx, ad->ad_bmap, bit_at, bit_nr);
	ad_tx_increase(tx, &ad->ad_grp_nr);

	ad_tx_set(tx, gd, 0, sizeof(*gd), AD_TX_REDO | AD_TX_LOG_ONLY);
	ad_tx_snap(tx, gd, offsetof(struct ad_group_df, gd_bmap[0]), AD_TX_REDO);
	return 0;
}

/** reserve space within a group, the reservation actions are returned to @act */
static daos_off_t
group_reserve_addr(struct ad_group *grp, struct ad_reserv_act *act)
{
	struct ad_group_df *gd = grp->gp_df;
	int	b = 1;
	int	at;

	at = find_bits(gd->gd_bmap, grp->gp_bmap_rsv, GRP_UNIT_BMSZ, 1, &b);
	if (at < 0)
		return 0;

	setbit64(grp->gp_bmap_rsv, at);
	D_ASSERT(gd->gd_unit_free > 0);
	grp->gp_unit_rsv++;

	group_addref(grp);
	act->ra_group = grp;
	act->ra_bit   = at;

	return gd->gd_addr + at * gd->gd_unit;
}

static int
group_tx_free_addr(struct ad_group *grp, daos_off_t addr, struct ad_tx *tx)
{
	struct ad_group_df *gd = grp->gp_df;
	struct ad_free_act *act;
	int		    at;
	int		    rc;

	D_ALLOC_PTR(act);
	if (!act)
		return -DER_NOMEM;

	at = (addr - gd->gd_addr) / gd->gd_unit;
	rc = ad_tx_clrbits(tx, gd->gd_bmap, at, 1);
	if (rc)
		goto failed;

	rc = ad_tx_increase(tx, &gd->gd_unit_free);
	if (rc)
		goto failed;

	/* lock the bit and prevent it from being reused before commit */
	setbit64(grp->gp_bmap_rsv, at);
	grp->gp_unit_rsv++;

	group_addref(grp);
	act->fa_group = grp;
	act->fa_at    = at;
	d_list_add_tail(&act->fa_link, &tx->tx_gp_free);
	return 0;
failed:
	D_FREE(act);
	return rc;
}

static daos_off_t
arena_reserve_addr(struct ad_arena *arena, daos_size_t size, struct ad_reserv_act *act)
{
	struct ad_group	   *grp;
	daos_off_t	    addr;
	int		    grp_at;
	int		    rc;
	bool		    tried = false;

	grp = arena_find_grp(arena, size, &grp_at);
	while (1) {
		if (grp == NULL) { /* full group */
			D_DEBUG(DB_TRACE, "No group(size=%d) found in arena=%d\n",
				(int)size, arena->ar_df->ad_id);
			rc = arena_reserve_grp(arena, size, &grp_at, &grp);
			if (rc) { /* cannot create a new group, full arena */
				/* XXX: other sized groups may have space. */
				arena->ar_full = true;
				return 0;
			}
		}
		D_DEBUG(DB_TRACE, "Found group=%p [r=%d, f=%d] for size=%d in arena=%d\n",
			grp, grp->gp_unit_rsv, grp->gp_df->gd_unit_free,
			(int)size, arena->ar_df->ad_id);

		addr = group_reserve_addr(grp, act);
		if (addr)
			break;

		D_ASSERT(!tried);
		tried = true;

		group_decref(grp);
		grp = NULL;
	}
	/* NB: reorder only does minimum amount of works most of the time */
	arena_reorder_grp(arena, grp, grp_at, true);
	group_decref(grp);

	arena_addref(arena);
	act->ra_arena = arena;
	return addr;
}

static int
arena_tx_free_addr(struct ad_arena *arena, daos_off_t addr, struct ad_tx *tx)
{
	struct ad_group	     *grp;
	int		      rc;

	/* convert to address to the group it belonging to */
	grp = arena_addr2grp(arena, addr);
	if (!grp)
		return -DER_NOMEM; /* the only possible failure */

	rc = group_tx_free_addr(grp, addr, tx);
	group_decref(grp);
	return rc;
}

/**
 * Reserve storage space with specified @size, the space should be allocated from
 * default arena if @arena_id is set to zero, otherwise it is allocated from the
 * provided arena.
 */
daos_off_t
blob_reserve_addr(struct ad_blob *blob, int type, daos_size_t size,
		  uint32_t *arena_id, struct ad_reserv_act *act)
{
	struct ad_arena	*arena = NULL;
	daos_off_t	 addr;
	uint32_t	 id;
	bool		 tried;
	int		 rc;

	if (arena_id && *arena_id != AD_ARENA_ANY)
		id = *arena_id;
	else
		id = blob->bb_arena_last[type]; /* the last used arena */

	/* arena zero should have been created while initializing the blob */
	D_ASSERT(id != AD_ARENA_ANY);
	D_DEBUG(DB_TRACE, "Loading arena=%u\n", id);

	rc = arena_load(blob, id, &arena);
	if (rc) {
		D_DEBUG(DB_TRACE, "Failed to load arena %u: %d\n", id, rc);
		/* fall through and create a new one */

	} else if (arena->ar_full) {
		D_DEBUG(DB_TRACE, "Arena %u is full, create a new one\n", id);
		arena_decref(arena);
		arena = NULL;
	}

	tried = false;
	while (1) {
		if (arena == NULL) {
			rc = arena_reserve(blob, type, NULL, &arena);
			if (rc) {
				D_ERROR("Failed to reserve new arena: %d\n", rc);
				return rc;
			}
			tried = true;
		}

		D_DEBUG(DB_TRACE, "reserve from arena==%d\n", arena->ar_df->ad_id);
		addr = arena_reserve_addr(arena, size, act);
		if (!addr) { /* full arena */
			D_DEBUG(DB_TRACE, "full arena=%d\n", arena->ar_df->ad_id);
			arena_decref(arena);
			if (tried)
				return 0;

			arena = NULL;
			continue;
		}

		/* completed */
		if (arena_id)
			*arena_id = arena->ar_df->ad_id;

		arena_decref(arena);
		return addr;
	}
}

daos_off_t
ad_reserve(struct ad_blob_handle bh, int type, daos_size_t size,
	   uint32_t *arena_id, struct ad_reserv_act *act)
{
	return blob_reserve_addr(bh.bh_blob, type, size, arena_id, act);
}

int
tx_complete(struct ad_tx *tx, int err)
{
	struct ad_blob	   *blob  = tx->tx_blob;
	struct umem_store  *store = &blob->bb_store;
	struct ad_arena    *arena;
	struct ad_group	   *group;
	struct ad_free_act *fac;
	int		    i;
	int		    rc;

	if (!err)
		rc = store->stor_ops->so_wal_submit(store, tx->tx_id, &tx->tx_redo);
	else
		rc = err;

	while ((arena = d_list_pop_entry(&tx->tx_ar_pub, struct ad_arena, ar_link))) {
		arena->ar_publishing = false;
		if (err) { /* keep the refcount and pin it */
			d_list_add(&arena->ar_link, &blob->bb_ars_rsv);
			continue;
		}
		clrbit64(blob->bb_bmap_rsv, arena->ar_df->ad_id);
		D_ASSERT(arena->ar_unpub);
		arena->ar_unpub = false;
		arena_decref(arena);
	}

	while ((group = d_list_pop_entry(&tx->tx_gp_pub, struct ad_group, gp_link))) {
		group->gp_publishing = false;
		if (err) { /* keep the refcount and pin it */
			d_list_add(&group->gp_link, &blob->bb_gps_rsv);
			continue;
		}
		for (i = 0; i < group->gp_bit_nr; i++)
			clrbit64(group->gp_arena->ar_bmap_rsv, group->gp_bit_at + i);
		D_ASSERT(group->gp_unpub);
		group->gp_unpub = false;
		group_decref(group);
	}

	while ((fac = d_list_pop_entry(&tx->tx_gp_free, struct ad_free_act, fa_link))) {
		int	pos;

		group = fac->fa_group;
		/* unlock the free bit, it can be used by future allocation */
		D_ASSERT(isset64(group->gp_bmap_rsv, fac->fa_at));
		clrbit64(group->gp_bmap_rsv, fac->fa_at);

		D_ASSERT(group->gp_unit_rsv > 0);
		if (err == 0) {
			/* Find current position of the group in binary-search array, change the
			 * available units of it, adjust position of the group and make sure the
			 * array is in right order.
			 */
			pos = arena_locate_grp(group->gp_arena, group);
			group->gp_unit_rsv--;
			/* NB: reorder only does minimum amount of works most of the time */
			arena_reorder_grp(group->gp_arena, group, pos, false);
		} else {
			/* cancel the delayed free, no reorder */
			group->gp_unit_rsv--;
		}
		group_decref(group);
		D_FREE(fac);
	}
	/* TODO: if rc != 0, run all undo operations */
	return rc;
}

/** Publish all the space reservations in @acts */
int
ad_tx_publish(struct ad_tx *tx, struct ad_reserv_act *acts, int act_nr)
{
	int	i;
	int	rc = 0;

	for (i = 0; i < act_nr; i++) {
		struct ad_arena	   *arena = acts[i].ra_arena;
		struct ad_group    *group = acts[i].ra_group;
		struct ad_group_df *gd = group->gp_df;

		if (arena->ar_unpub && !arena->ar_publishing) {
			D_DEBUG(DB_TRACE, "publishing arena=%d\n", arena->ar_df->ad_id);
			rc = arena_tx_publish(arena, tx);
			if (rc)
				break;

			arena->ar_publishing = true;
			if (d_list_empty(&arena->ar_link)) {
				arena_addref(arena);
				d_list_add_tail(&arena->ar_link, &tx->tx_ar_pub);
			} else {
				/* still on bb_ars_rsv, take over the refcount */
				d_list_move_tail(&arena->ar_link, &tx->tx_ar_pub);
			}
		}
		acts[i].ra_arena = NULL;
		arena_decref(arena);

		if (group->gp_unpub && !group->gp_publishing) {
			D_DEBUG(DB_TRACE, "publishing a new group, size=%d\n",
				(int)group->gp_df->gd_unit);
			rc = group_tx_publish(group, tx);
			if (rc)
				break;

			group->gp_publishing = true;
			if (d_list_empty(&group->gp_link)) {
				group_addref(group);
				d_list_add_tail(&group->gp_link, &tx->tx_gp_pub);
			} else {
				/* still on bb_gps_rsv, take over the refcount */
				d_list_move_tail(&group->gp_link, &tx->tx_gp_pub);
			}
		}

		D_DEBUG(DB_TRACE, "publishing reserved bit=%d\n", acts[i].ra_bit);
		ad_tx_setbits(tx, gd->gd_bmap, acts[i].ra_bit, 1);
		ad_tx_decrease(tx, &gd->gd_unit_free);

		clrbit64(group->gp_bmap_rsv, acts[i].ra_bit);
		group->gp_unit_rsv--;

		acts[i].ra_group = NULL;
		group_decref(group);
	}
	return rc;
}

/** Cancel all the space reservaction in @acts */
void
ad_cancel(struct ad_reserv_act *acts, int act_nr)
{
	int	i;

	for (i = 0; i < act_nr; i++) {
		struct ad_arena	*arena = acts[i].ra_arena;
		struct ad_group *group = acts[i].ra_group;
		struct ad_blob	*blob  = arena->ar_blob;
		int		 pos;

		D_DEBUG(DB_TRACE, "cancel bit=%d\n", acts[i].ra_bit);
		clrbit64(group->gp_bmap_rsv, acts[i].ra_bit);

		/* Find current position of the group in binary-search array, change the
		 * available units of it, adjust position of the group and make sure the
		 * array is in right order.
		 */
		pos = arena_locate_grp(arena, group);
		group->gp_unit_rsv--;
		/* NB: reorder only does minimum amount of works most of the time */
		arena_reorder_grp(arena, group, pos, false);

		/* NB: arena and group are remained as "reserved" for now */
		if (group->gp_unpub && d_list_empty(&group->gp_link)) { /* pin it */
			D_ASSERT(!group->gp_publishing);
			/* refcount is taken over by the list */
			d_list_add(&group->gp_link, &blob->bb_gps_rsv);
		} else {
			group_decref(group);
		}

		if (arena->ar_unpub && d_list_empty(&arena->ar_link)) { /* pin it */
			D_ASSERT(!arena->ar_publishing);
			/* refcount is taken over by the list */
			d_list_add(&arena->ar_link, &blob->bb_ars_rsv);
		} else {
			arena_decref(arena);
		}
		acts[i].ra_arena = NULL;
		acts[i].ra_group = NULL;
	}
}

daos_off_t
ad_alloc(struct ad_blob_handle bh, int type, daos_size_t size, uint32_t *arena_id)
{
	struct ad_reserv_act act;
	struct ad_tx	     tx;
	int		     rc;

	rc = blob_reserve_addr(bh.bh_blob, type, size, arena_id, &act);
	if (rc)
		return rc;

	rc = ad_tx_begin(bh, &tx);
	if (rc)
		goto failed;

	rc = ad_tx_publish(&tx, &act, 1);

	ad_tx_end(&tx, rc);
	return rc;
failed:
	ad_cancel(&act, 1);
	return rc;
}

/** Free address in a transacton */
int
ad_tx_free(struct ad_tx *tx, daos_off_t addr)
{
	struct ad_blob	   *blob = tx->tx_blob;
	struct ad_arena_df *ad;
	struct ad_arena	   *arena;
	int		    rc;

	/* arena is stored as the page header */
	ad = blob_addr2ptr(blob, addr & ~ARENA_SIZE_MASK);
	D_ASSERT(ad->ad_magic == ARENA_MAGIC);

	D_DEBUG(DB_TRACE, "loading arena for free\n");
	arena_load(blob, ad->ad_id, &arena);

	rc = arena_tx_free_addr(arena, addr, tx);
	arena_decref(arena);
	return rc;
}

/** Register a new arena type */
static int
blob_register_arena(struct ad_blob *blob, unsigned int arena_type,
		    struct ad_group_spec *specs, unsigned int specs_nr, struct ad_tx *tx)
{
	struct ad_blob_df	*bd = blob->bb_df;
	struct ad_arena_spec	*spec;
	int			 i;

	if (arena_type >= ARENA_SPEC_MAX)
		return -DER_INVAL;

	if (specs_nr >= ARENA_GRP_SPEC_MAX)
		return -DER_INVAL;

	spec = &bd->bd_asp[arena_type];
	if (spec->as_specs_nr != 0)
		return -DER_EXIST;

	ad_tx_increase(tx, &bd->bd_asp_nr);

	spec->as_type	    = arena_type;
	spec->as_specs_nr   = specs_nr;
	spec->as_arena_last = AD_ARENA_ANY;
	for (i = 0; i < specs_nr; i++)
		spec->as_specs[i] = specs[i];

	blob->bb_arena_last[arena_type] = AD_ARENA_ANY;
	ad_tx_snap(tx, spec, sizeof(*spec), AD_TX_REDO);
	return 0;
}

int
ad_arena_register(struct ad_blob_handle bh, unsigned int arena_type,
		  struct ad_group_spec *specs, unsigned int specs_nr, struct ad_tx *tx)
{
	return blob_register_arena(bh.bh_blob, arena_type, specs, specs_nr, tx);
}

/****************************************************************************
 * A few helper functions:
 ****************************************************************************/

static struct ad_group *
alloc_group(struct ad_arena *arena, bool force)
{
	struct ad_group *grp  = NULL;

	if (!force) {
		struct ad_blob  *blob = arena->ar_blob;

		grp = d_list_pop_entry(&blob->bb_gps_lru, struct ad_group, gp_link);
		if (grp)
			blob->bb_gps_lru_size--;
	}

	if (!grp) {
		D_ALLOC_PTR(grp);
		if (!grp)
			return NULL;
	} else {
		if (grp->gp_df && grp->gp_df->gd_back_ptr) {
			D_ASSERT(grp == group_df2ptr(grp->gp_df));
			grp->gp_df->gd_back_ptr = 0;
		}
		memset(grp, 0, sizeof(*grp));
	}
	D_INIT_LIST_HEAD(&grp->gp_link);
	if (arena) {
		arena_addref(arena);
		grp->gp_arena = arena;
	}
	return grp;
}

static void
free_group(struct ad_group *grp, bool force)
{
	D_ASSERT(grp->gp_ref == 0);
	D_ASSERT(d_list_empty(&grp->gp_link));

	if (!force) {
		struct ad_arena *arena = grp->gp_arena;
		struct ad_blob  *blob;

		D_ASSERT(arena);
		blob = arena->ar_blob;
		D_ASSERT(blob);

		d_list_add_tail(&grp->gp_link, &blob->bb_gps_lru);
		if (blob->bb_gps_lru_size < GROUP_LRU_SIZE) {
			blob->bb_gps_lru_size++;
			return;
		}
		/* release an old one from the LRU */
		grp = d_list_pop_entry(&blob->bb_gps_lru, struct ad_group, gp_link);
	}
	if (grp->gp_arena)
		arena_decref(grp->gp_arena);
	if (grp->gp_df)
		grp->gp_df->gd_back_ptr = 0;
	D_FREE(grp);
}

static struct ad_arena *
alloc_arena(struct ad_blob *blob, bool force)
{
	struct ad_arena *arena = NULL;

	if (!force) {
		D_ASSERT(blob);
		arena = d_list_pop_entry(&blob->bb_ars_lru, struct ad_arena, ar_link);
		if (arena)
			blob->bb_ars_lru_size--;
	}

	if (!arena) {
		D_ALLOC_PTR(arena);
		if (!arena)
			return NULL;

		D_ALLOC(arena->ar_size_sorter, ARENA_GRP_MAX * sizeof(struct ad_group_df *));
		if (!arena->ar_size_sorter)
			goto failed;

		D_ALLOC(arena->ar_addr_sorter, ARENA_GRP_MAX * sizeof(struct ad_group_df *));
		if (!arena->ar_addr_sorter)
			goto failed;

	} else {
		struct ad_group_df **p1;
		struct ad_group_df **p2;

		if (arena->ar_df && arena->ar_df->ad_back_ptr) {
			D_ASSERT(arena_df2ptr(arena->ar_df) == arena);
			arena->ar_df->ad_back_ptr = 0;
		}
		p1 = arena->ar_size_sorter;
		p2 = arena->ar_addr_sorter;

		memset(arena, 0, sizeof(*arena));
		arena->ar_size_sorter = p1;
		arena->ar_addr_sorter = p2;
	}
	D_INIT_LIST_HEAD(&arena->ar_link);
	if (blob) {
		blob_addref(blob);
		arena->ar_blob = blob;
	}
	return arena;
failed:
	free_arena(arena, true);
	return NULL;
}

static void
free_arena(struct ad_arena *arena, bool force)
{
	D_ASSERT(arena->ar_ref == 0);
	D_ASSERT(d_list_empty(&arena->ar_link));

	if (!force) {
		struct ad_blob *blob = arena->ar_blob;

		D_ASSERT(blob);
		d_list_add_tail(&arena->ar_link, &blob->bb_ars_lru);
		if (blob->bb_ars_lru_size < ARENA_LRU_SIZE) {
			blob->bb_ars_lru_size++;
			return;
		}
		/* release an old one from the LRU */
		arena = d_list_pop_entry(&blob->bb_ars_lru, struct ad_arena, ar_link);
	}
	if (arena->ar_blob)
		blob_decref(arena->ar_blob);
	if (arena->ar_addr_sorter)
		D_FREE(arena->ar_addr_sorter);
	if (arena->ar_size_sorter)
		D_FREE(arena->ar_size_sorter);
	if (arena->ar_df)
		arena->ar_df->ad_back_ptr = 0;

	D_FREE(arena);
}
