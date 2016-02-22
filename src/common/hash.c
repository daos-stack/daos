/*
 * SPECIAL LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of Contract No. B599860,
 * and the terms of the LGPL License.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * (C) Copyright 2012, 2013 Intel Corporation
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 or (at your discretion) any later version.
 * (LGPL) version 2.1 accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * LGPL HEADER END
 */
/*
 * This file is part of DAOSM
 *
 * daos_event/hash.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */
#include <pthread.h>
#include <daos/daos_common.h>
#include <daos/daos_list.h>
#include <daos/daos_hash.h>

uint64_t
daos_hash_mix64(uint64_t key)
{
	key = (~key) + (key << 21);
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);

	return key;
}

/** Robert Jenkins' 96 bit Mix Function */
uint32_t
daos_hash_mix96(uint32_t a, uint32_t b, uint32_t c)
{
	a = a - b;
	a = a - c;
	a = a ^ (c >> 13);
	b = b - c;
	b = b - a;
	b = b ^ (a << 8);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 13);
	a = a - b;
	a = a - c;
	a = a ^ (c >> 12);
	b = b - c;
	b = b - a;
	b = b ^ (a << 16);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 5);
	a = a - b;
	a = a - c;
	a = a ^ (c >> 3);
	b = b - c;
	b = b - a;
	b = b ^ (a << 10);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 15);

	return c;
}

/** consistent hash search */
unsigned int
daos_chash_srch_u64(uint64_t *hashes, unsigned int nhashes, uint64_t value)
{
	int	high = nhashes - 1;
	int	low = 0;
	int     i;

	for (i = high / 2; high - low > 1; i = (low + high) / 2) {
		if (value >= hashes[i])
			low = i;
		else /* value < hashes[i] */
			high = i;
	}
	return value >= hashes[high] ? high : low;
}


static unsigned int
daos_hhash_key2hash(uint64_t key, int hbits)
{
	return (unsigned int)((key >> DAOS_HTYPE_BITS) & ((1U << hbits) - 1));
}

struct daos_hlink *
daos_hhash_link_lookup_locked(struct daos_hhash *hhash, uint64_t key)
{
	struct daos_hlink	*hlink;
	unsigned int	hash;

	hash = daos_hhash_key2hash(key, hhash->dh_bits);

	daos_list_for_each_entry(hlink, &hhash->dh_hash[hash], hl_link) {
		if (hlink->hl_key == key) {
			hlink->hl_ref++;
			return hlink;
		}
	}
	return NULL;
}

struct daos_hlink *
daos_hhash_link_lookup(struct daos_hhash *hhash, uint64_t key)
{
	struct daos_hlink *hlink;

	pthread_mutex_lock(&hhash->dh_lock);
	hlink = daos_hhash_link_lookup_locked(hhash, key);
	pthread_mutex_unlock(&hhash->dh_lock);

	return hlink;
}

int
daos_hhash_link_delete(struct daos_hhash *hhash, struct daos_hlink *hlink)
{
	pthread_mutex_lock(&hhash->dh_lock);

	if (daos_list_empty(&hlink->hl_link)) {
		pthread_mutex_unlock(&hhash->dh_lock);
		return 0;
	}

	daos_list_del_init(&hlink->hl_link);

	D_ASSERT(hlink->hl_ref > 0);
	hlink->hl_ref--;
	if (hlink->hl_ref == 0 && hlink->hl_ops != NULL &&
				  hlink->hl_ops->hop_free != NULL)
		hlink->hl_ops->hop_free(hlink);

	pthread_mutex_unlock(&hhash->dh_lock);

	return 1;
}

void
daos_hhash_link_putref_locked(struct daos_hlink *hlink)
{
	D_ASSERT(hlink->hl_ref > 0);
	hlink->hl_ref--;
	if (hlink->hl_ref == 0) {
		D_ASSERT(daos_list_empty(&hlink->hl_link));
		if (hlink->hl_ops != NULL &&
		    hlink->hl_ops->hop_free != NULL)
			hlink->hl_ops->hop_free(hlink);
	}
}


void
daos_hhash_link_putref(struct daos_hhash *hhash, struct daos_hlink *hlink)
{
	pthread_mutex_lock(&hhash->dh_lock);
	daos_hhash_link_putref_locked(hlink);
	pthread_mutex_unlock(&hhash->dh_lock);
}

void
daos_hhash_link_key(struct daos_hlink *hlink, uint64_t *key)
{
	*key = hlink->hl_key;
}

int
daos_hhash_link_empty(struct daos_hlink *hlink)
{
	if (!hlink->hl_initialized)
		return 1;

	D_ASSERT(hlink->hl_ref != 0 || daos_list_empty(&hlink->hl_link));
	return daos_list_empty(&hlink->hl_link);
}

void
daos_hhash_hlink_init(struct daos_hlink *hlink, struct daos_hlink_ops *ops)
{
	DAOS_INIT_LIST_HEAD(&hlink->hl_link);
	hlink->hl_initialized = 1;
	hlink->hl_ref = 1; /* for caller */
	hlink->hl_ops = ops;
}

void
daos_hhash_link_insert(struct daos_hhash *hhash, struct daos_hlink *hlink,
		       int type)
{
	unsigned int	hash;

	pthread_mutex_lock(&hhash->dh_lock);

	hlink->hl_key = ((hhash->dh_cookie++) << DAOS_HTYPE_BITS) | type;

	hash = daos_hhash_key2hash(hlink->hl_key, hhash->dh_bits);

	hlink->hl_ref += 1;
	daos_list_add_tail(&hlink->hl_link, &hhash->dh_hash[hash]);

	pthread_mutex_unlock(&hhash->dh_lock);
}

int
daos_hhash_link_insert_key(struct daos_hhash *hhash, uint64_t key,
			   struct daos_hlink *hlink)
{
	struct daos_hlink	*tmp;
	unsigned int	hash;

	hlink->hl_key = key;
	pthread_mutex_lock(&hhash->dh_lock);

	tmp = daos_hhash_link_lookup_locked(hhash, hlink->hl_key);
	if (tmp != NULL) {
		D_ERROR("Failed to insert hlink with key %"PRIu64"\n",
		       hlink->hl_key);
		daos_hhash_link_putref_locked(tmp);
		pthread_mutex_unlock(&hhash->dh_lock);
		return 0;
	}

	hash = daos_hhash_key2hash(hlink->hl_key, hhash->dh_bits);

	hlink->hl_ref += 1;
	daos_list_add_tail(&hlink->hl_link, &hhash->dh_hash[hash]);

	pthread_mutex_unlock(&hhash->dh_lock);
	return 1;
}

int
daos_hhash_create(unsigned int bits, struct daos_hhash **hhash)
{
	struct daos_hhash	*hh;
	int		rc;
	int		i;

	hh = malloc(sizeof(*hh));
	if (hh == NULL)
		return -ENOMEM;

	hh->dh_pid = getpid();
	hh->dh_bits = bits;
	hh->dh_hash = malloc(sizeof(hh->dh_hash[0]) * (1 << bits));
	if (hh == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	for (i = 0; i < (1 << bits); i++)
		DAOS_INIT_LIST_HEAD(&hh->dh_hash[i]);

	rc = pthread_mutex_init(&hh->dh_lock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for handle hash\n");
		goto failed;
	}

	hh->dh_lock_init = 1;
	hh->dh_cookie = 1;
	*hhash = hh;
	return 0;
 failed:
	daos_hhash_destroy(hh);
	return rc;
}

void
daos_hhash_destroy(struct daos_hhash *hh)
{
	int	i;

	if (hh->dh_hash != NULL) {
		for (i = 0; i < (1 << hh->dh_bits); i++) {
			while (!daos_list_empty(&hh->dh_hash[i])) {
				struct daos_hlink *hlink;

				hlink = daos_list_entry(hh->dh_hash[i].next,
							struct daos_hlink,
							hl_link);
				daos_list_del_init(&hlink->hl_link);
				if (hlink->hl_ops != NULL &&
				    hlink->hl_ops->hop_free != NULL)
				hlink->hl_ops->hop_free(hlink);
			}
		}

		free(hh->dh_hash);
	}

	if (hh->dh_lock_init && hh->dh_pid == getpid())
		pthread_mutex_destroy(&hh->dh_lock);

	free(hh);
}
