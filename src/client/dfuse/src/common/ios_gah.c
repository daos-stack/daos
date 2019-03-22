/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>

#include <gurt/common.h>

#include "include/ios_gah.h"

#define IOS_GAH_STORE_INIT_CAPACITY (1024 * 8)
#define IOS_GAH_STORE_DELTA (1024 * 8)
#define IOS_GAH_VERSION 1

/**
 * Increase the totoal capacity of the gah store by "delta" slots
 *
 * \param gah_store	[IN/OUT]	pointer to the gah_store
 * \param delta		[IN]		number of entries to add
 */
static int
ios_gah_store_increase_capacity(struct ios_gah_store *gah_store, int delta)
{
	void *new_array;
	int ii;

	/* allocate more space and copy old data over */
	struct ios_gah_ent *new_data;
	int new_cap = gah_store->capacity + delta;

	D_ALLOC_ARRAY(new_data, delta);
	if (new_data == NULL)
		return -DER_NOMEM;

	D_REALLOC(new_array,
		  gah_store->ptr_array,
		  new_cap * sizeof(struct ios_gah_ent));
	if (!new_array) {
		D_FREE(new_data);
		return -DER_NOMEM;
	}

	gah_store->ptr_array = new_array;
	/* setup the pointer array */
	for (ii = 0; ii < delta; ii++)
		gah_store->ptr_array[gah_store->capacity + ii] = &new_data[ii];

	for (ii = gah_store->capacity; ii < gah_store->capacity + delta; ii++) {
		gah_store->ptr_array[ii]->fid = ii;

		/* point tail to the new tail */
		d_list_add_tail(&gah_store->ptr_array[ii]->list,
				&gah_store->free_list);
	}
	gah_store->capacity = new_cap;

	return -DER_SUCCESS;
}

/**
 * CRC-8-CCITT, x^8 + x^2 + x + 1, 0x07
 *
 * \param data		[IN]	pointer to input data
 * \param len		[IN]	length of data in bytes
 *
 * return			returns the crc code of the input data
 */
static uint8_t my_crc8(uint8_t *data, size_t len)
{
	uint8_t my_crc = 0;
	uint8_t poly = 0x07;
	int ii, jj;

	for (ii = 0; ii < len; ii++) {
		my_crc ^= *(data + ii);
		for (jj = 0; jj < 8; jj++) {
			if ((my_crc & 0x80) != 0)
				my_crc ^= (my_crc << 1) ^ poly;
			else
				my_crc <<= 1;
		}
	}

	return my_crc;
}

/*
 * Initialize the gah store. Allocate storage, initialize the pointer array, and
 * setup the linked-lists.
 *
 */
struct ios_gah_store *ios_gah_init(d_rank_t rank)
{
	struct ios_gah_store *gah_store;
	int ii;

	D_ALLOC_PTR(gah_store);
	if (gah_store == NULL)
		return NULL;

	gah_store->size = 0;
	gah_store->rank = rank;
	gah_store->capacity = IOS_GAH_STORE_INIT_CAPACITY;
	D_ALLOC_ARRAY(gah_store->data, IOS_GAH_STORE_INIT_CAPACITY);
	if (gah_store->data == NULL) {
		D_FREE(gah_store);
		return NULL;
	}
	D_ALLOC_ARRAY(gah_store->ptr_array, IOS_GAH_STORE_INIT_CAPACITY);
	if (gah_store->ptr_array == NULL) {
		D_FREE(gah_store->data);
		D_FREE(gah_store);
		return NULL;
	}

	D_INIT_LIST_HEAD(&gah_store->free_list);

	/* setup the pointer array */
	for (ii = 0; ii < IOS_GAH_STORE_INIT_CAPACITY; ii++) {
		gah_store->ptr_array[ii] = &gah_store->data[ii];
		gah_store->ptr_array[ii]->fid = ii;

		/* setup the linked list */
		d_list_add_tail(&gah_store->ptr_array[ii]->list,
				&gah_store->free_list);
	}

	return gah_store;
}

int ios_gah_destroy(struct ios_gah_store *ios_gah_store)
{
	int ii = 0;

	if (ios_gah_store == NULL)
		return -DER_INVAL;
	/* check for active handles */
	if (ios_gah_store->size != 0)
		return -DER_BUSY;

	for (ii = 0; ii < ios_gah_store->capacity; ii++)
		if (ios_gah_store->ptr_array[ii]->in_use)
			return -DER_BUSY;

	/* free the chunck allocated on init */
	D_FREE(ios_gah_store->ptr_array[0]);
	/* walk down the pointer array, free all memory chuncks */
	for (ii = IOS_GAH_STORE_INIT_CAPACITY; ii < ios_gah_store->capacity;
	    ii += IOS_GAH_STORE_DELTA) {
		D_FREE(ios_gah_store->ptr_array[ii]);
	}
	D_FREE(ios_gah_store->ptr_array);
	D_FREE(ios_gah_store);

	return -DER_SUCCESS;
}

int ios_gah_allocate_base(struct ios_gah_store *gah_store,
			  struct ios_gah *gah, d_rank_t base,
			  void *arg)
{
	struct ios_gah_ent *ent;
	int rc = -DER_SUCCESS;

	if (gah == NULL)
		return -DER_INVAL;
	if (d_list_empty(&gah_store->free_list)) {
		rc = ios_gah_store_increase_capacity(gah_store,
						     IOS_GAH_STORE_DELTA);
		if (rc != -DER_SUCCESS)
			return rc;
	}

	/* take one gah from the head of the list */
	ent = d_list_pop_entry(&gah_store->free_list, struct ios_gah_ent, list);

	ent->in_use = true;
	ent->arg = arg;

	gah->fid = ent->fid;
	gah->revision = ++ent->revision;
	gah->reserved = 0;
	/* setup the gah */
	gah->version = IOS_GAH_VERSION;
	gah->root = gah_store->rank;
	gah->base = base;
	gah->crc = my_crc8((uint8_t *)gah, 120 / 8);

	gah_store->size++;

	return -DER_SUCCESS;
}

int ios_gah_allocate(struct ios_gah_store *gah_store,
		     struct ios_gah *gah, void *arg)
{
	return ios_gah_allocate_base(gah_store, gah, gah_store->rank, arg);
}

int ios_gah_deallocate(struct ios_gah_store *gah_store,
		       struct ios_gah *gah)
{
	int ret;

	if (!gah_store)
		return -DER_INVAL;
	if (!gah)
		return -DER_INVAL;
	ret = ios_gah_check_crc(gah);
	if (ret != -DER_SUCCESS)
		return ret;
	ret = ios_gah_check_version(gah);
	if (ret != -DER_SUCCESS)
		return ret;
	if (gah->fid >= gah_store->capacity)
		return -DER_OVERFLOW;
	if (!(gah_store->ptr_array[gah->fid]->in_use))
		return -DER_NONEXIST;
	if (gah_store->ptr_array[gah->fid]->revision != gah->revision)
		return -DER_NONEXIST;

	gah_store->ptr_array[gah->fid]->in_use = false;

	/* append the reclaimed entry to the list of available entires */
	d_list_add(&gah_store->ptr_array[gah->fid]->list,
		   &gah_store->free_list);

	gah_store->size--;

	return -DER_SUCCESS;
}

int ios_gah_get_info(struct ios_gah_store *gah_store,
		     struct ios_gah *gah, void **arg)
{
	int ret;

	if (!arg)
		return -DER_INVAL;
	*arg = NULL;
	if (!gah_store)
		return -DER_INVAL;
	if (!gah)
		return -DER_INVAL;
	ret = ios_gah_check_crc(gah);
	if (ret != -DER_SUCCESS)
		return ret;
	ret = ios_gah_check_version(gah);
	if (ret != -DER_SUCCESS)
		return ret;
	if (gah_store->rank != gah->root)
		return -DER_INVAL;
	if (gah->fid >= gah_store->capacity)
		return -DER_OVERFLOW;
	if (!(gah_store->ptr_array[gah->fid]->in_use))
		return -DER_NONEXIST;
	if (gah_store->ptr_array[gah->fid]->revision != gah->revision)
		return -DER_NONEXIST;
	*arg = (void *)(gah_store->ptr_array[gah->fid]->arg);

	return -DER_SUCCESS;
}

int ios_gah_check_crc(struct ios_gah *gah)
{
	uint8_t tmp_crc;

	if (gah == NULL)
		return -DER_INVAL;
	tmp_crc = my_crc8((uint8_t *)gah, 120 / 8);

	return (tmp_crc == gah->crc) ? -DER_SUCCESS : -DER_NO_HDL;
}

int ios_gah_check_version(struct ios_gah *gah)
{
	if (gah == NULL)
		return -DER_INVAL;
	return (gah->version == IOS_GAH_VERSION)
		? -DER_SUCCESS : -DER_MISMATCH;
}
