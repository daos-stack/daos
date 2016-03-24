/**
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
#ifndef __DAOS_HASH_H__
#define __DAOS_HASH_H__

#define DAOS_HHASH_BITS		16
#define DAOS_HTYPE_BITS		3
#define DAOS_HTYPE_MASK		((1ULL << DAOS_HTYPE_BITS) - 1)

enum {
	DAOS_HTYPE_EQ		= 0,
	DAOS_HTYPE_VOS_POOL	= 1,
	DAOS_HTYPE_VOS_CO	= 2,
	/* More to be added */
};

struct daos_hlink;

struct daos_hlink_ops {
	/** free callback */
	void	(*hop_free)(struct daos_hlink *hlink);
};

struct daos_hlink {
	daos_list_t		hl_link;
	uint64_t		hl_key;
	unsigned int		hl_ref;
	unsigned int		hl_initialized:1;
	struct daos_hlink_ops	*hl_ops;
};

struct daos_hhash {
	pthread_mutex_t         dh_lock;
	unsigned int		dh_lock_init:1;
	unsigned int            dh_bits;
	unsigned int		dh_pid;
	uint64_t                dh_cookie;
	daos_list_t              *dh_hash;
};

int daos_hhash_create(unsigned int bits, struct daos_hhash **hhash);
void daos_hhash_destroy(struct daos_hhash *hh);
void daos_hhash_hlink_init(struct daos_hlink *hlink,
			   struct daos_hlink_ops *ops);
void daos_hhash_link_insert(struct daos_hhash *hhash,
			    struct daos_hlink *hlink, int type);
int daos_hhash_link_insert_key(struct daos_hhash *hhash,
			       uint64_t key, struct daos_hlink *hlink);
struct daos_hlink *daos_hhash_link_lookup(struct daos_hhash *hhash,
					  uint64_t key);
void daos_hhash_link_putref_locked(struct daos_hlink *hlink);
void daos_hhash_link_putref(struct daos_hhash *hhash,
			    struct daos_hlink *hlink);
int daos_hhash_link_delete(struct daos_hhash *hhash,
			   struct daos_hlink *hlink);
int daos_hhash_link_empty(struct daos_hlink *hlink);
void daos_hhash_link_key(struct daos_hlink *hlink, uint64_t *key);

#endif /*__DAOS_HASH_H__*/
