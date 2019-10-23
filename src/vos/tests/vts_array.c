/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_aggregate.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include "vts_array.h"

struct vts_array {
	daos_unit_oid_t	va_oid;
	daos_handle_t	va_coh;
	daos_iod_t	va_iod;
	d_iov_t		va_dkey;
	daos_recx_t	va_recx;
	uint64_t	va_dkey_value;
	d_sg_list_t	va_sgl;
	uint32_t	va_akey_value;
	int32_t		va_magic;
};

#define ARRAY_MAGIC 0xdeadbeef
static int32_t	array_magic = ARRAY_MAGIC;

static daos_handle_t
vts_array2hdl(struct vts_array *array)
{
	daos_handle_t	aoh;

	aoh.cookie = (uint64_t)array;

	return aoh;
}

static struct vts_array *
vts_hdl2array(daos_handle_t aoh)
{
	struct vts_array	*array = (struct vts_array *)aoh.cookie;

	D_ASSERT(array != NULL && array->va_magic == ARRAY_MAGIC);

	return array;
}

static int
array_init(struct vts_array *in, daos_handle_t coh, daos_unit_oid_t oid,
	   struct vts_array **out)
{
	int	rc;

	D_ASSERT((in == NULL && out != NULL) || (in != NULL && out == NULL));

	if (in == NULL) {
		D_ALLOC_PTR(in);
		if (in == NULL)
			return -DER_NOMEM;
	} else {
		memset(in, 0, sizeof(*in));
	}

	rc = daos_sgl_init(&in->va_sgl, 1);
	if (rc != 0) {
		if (out)
			D_FREE(in);

		return rc;
	}


	if (out)
		*out = in;

	d_iov_set(&in->va_dkey, &in->va_dkey_value, sizeof(in->va_dkey_value));
	in->va_akey_value = 0;
	d_iov_set(&in->va_iod.iod_name, &in->va_akey_value,
		  sizeof(in->va_akey_value));
	in->va_iod.iod_type = DAOS_IOD_ARRAY;
	in->va_iod.iod_recxs = &in->va_recx;
	in->va_iod.iod_nr = 1;
	in->va_coh = coh;
	in->va_oid = oid;
	in->va_magic = array_magic;

	return 0;
}

static void
array_fini(struct vts_array *in, bool free_array)
{
	daos_sgl_fini(&in->va_sgl, false);
	in->va_magic = 0;
	if (!free_array)
		return;

	D_FREE(in);
}

static int
update_array(struct vts_array *array, daos_epoch_t epoch, uint64_t dkey,
	     uint64_t offset, uint64_t nr, int32_t *values)
{
	d_sg_list_t	*sgl = NULL;

	array->va_recx.rx_idx = offset;
	array->va_recx.rx_nr = nr;
	array->va_iod.iod_size = 0;
	array->va_dkey_value = dkey;
	if (values) {
		d_iov_set(&array->va_sgl.sg_iovs[0], values,
			  nr * sizeof(*values));
		array->va_iod.iod_size = sizeof(*values);
		sgl = &array->va_sgl;
	}

	return vos_obj_update(array->va_coh, array->va_oid, epoch, 0,
			      &array->va_dkey, 1, &array->va_iod, sgl);
}

static int
fetch_array(struct vts_array *array, daos_epoch_t epoch, uint64_t dkey,
	    uint64_t offset, uint64_t nr, int32_t *values)
{
	d_sg_list_t	*sgl = &array->va_sgl;

	array->va_recx.rx_idx = offset;
	array->va_recx.rx_nr = nr;
	d_iov_set(&array->va_sgl.sg_iovs[0], values,
		  nr * sizeof(*values));
	array->va_iod.iod_size = sizeof(*values);
	array->va_dkey_value = dkey;

	return vos_obj_fetch(array->va_coh, array->va_oid, epoch,
			     &array->va_dkey, 1, &array->va_iod, sgl);
}


int
vts_array_alloc(daos_handle_t coh, daos_epoch_t epoch, daos_unit_oid_t *oid)
{
	struct vts_array	 array;
	int			 rc = 0;

	*oid = dts_unit_oid_gen(0, DAOS_OF_DKEY_UINT64, 0);
	rc = array_init(&array, coh, *oid, NULL);
	if (rc != 0)
		return rc;

	rc = update_array(&array, epoch, 0, 0, 1, &array_magic);

	array_fini(&array, false);

	return rc;
}

int
vts_array_free(daos_handle_t coh, daos_unit_oid_t oid)
{
	return vos_obj_delete(coh, oid);
}

int
vts_array_open(daos_handle_t coh, daos_unit_oid_t oid, daos_handle_t *aoh)
{
	struct vts_array	*array;
	int32_t			 fetch_magic;
	int			 rc;

	rc = array_init(NULL, coh, oid, &array);
	if (rc != 0)
		return rc;

	rc = fetch_array(array, DAOS_EPOCH_MAX, 0, 0, 1, &fetch_magic);
	if (rc != 0 || fetch_magic != ARRAY_MAGIC) {
		array_fini(array, true);
		return rc;
	}

	*aoh = vts_array2hdl(array);
	return 0;
}

int
vts_array_reset(daos_handle_t aoh, daos_epoch_t punch_epoch,
		daos_epoch_t create_epoch)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	int			 rc;

	D_ASSERT(punch_epoch < create_epoch);

	rc = vos_obj_punch(array->va_coh, array->va_oid, punch_epoch, 0, 0,
			   NULL, 0, NULL, NULL);
	if (rc != 0)
		return rc;

	return update_array(array, create_epoch, 0, 0, 1, &array_magic);
}

void
vts_array_close(daos_handle_t aoh)
{
	struct vts_array	*array = vts_hdl2array(aoh);

	array_fini(array, true);
}

int
vts_array_set_size(daos_handle_t aoh, daos_epoch_t epoch, daos_size_t new_size)
{
	daos_size_t	old_size;
	int32_t		element = 0;
	int		rc;

	/* Should probably put this in vos transaction but keep it simple for
	 * now.
	 */
	D_DEBUG(DB_IO, "Getting the old array size\n");
	rc = vts_array_get_size(aoh, epoch, &old_size);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_IO, "Old size is "DF_U64"\n", old_size);
	if (old_size > new_size) {
		D_DEBUG(DB_IO, "Truncate at "DF_U64"\n", new_size);
		rc = vts_array_punch(aoh, epoch, new_size, old_size - new_size);
		if (rc != 0)
			return rc;
		D_DEBUG(DB_IO, "Checking array size again\n");
		rc = vts_array_get_size(aoh, epoch, &old_size);
		if (rc != 0)
			return rc;
		D_DEBUG(DB_IO, "Size is now "DF_U64"\n", old_size);
	}

	if (old_size == new_size)
		return 0;

	D_DEBUG(DB_IO, "Writing one entry at "DF_U64"\n", new_size - 1);
	return vts_array_write(aoh, epoch, new_size - 1, 1, &element);
}

#define ENTRIES_LOG2	3
#define ENTRIES_PER_KEY	(1 << ENTRIES_LOG2)
#define ENTRIES_MASK	(ENTRIES_PER_KEY - 1)

int
vts_array_get_size(daos_handle_t aoh, daos_epoch_t epoch, daos_size_t *size)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	daos_recx_t		 recx;
	int			 rc;
	d_iov_t			 dkey;
	uint64_t		 val;

	d_iov_set(&dkey, NULL, 0);

	rc = vos_obj_query_key(array->va_coh, array->va_oid,
			       DAOS_GET_DKEY | DAOS_GET_RECX | DAOS_GET_MAX,
			       epoch, &dkey, &array->va_iod.iod_name,
			       &recx);

	if (rc != 0)
		return rc;

	val = *(uint64_t *)dkey.iov_buf;
	if (val == 0) {
		*size = 0;
		return 0;
	}

	*size = (val - 1) * ENTRIES_PER_KEY + recx.rx_idx + recx.rx_nr;

	return 0;
}

int
vts_array_write(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count, int32_t *elements)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	int32_t			*cursor = elements;
	uint64_t		 first = offset >> ENTRIES_LOG2;
	uint64_t		 last = (offset + count - 1) >> ENTRIES_LOG2;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = ENTRIES_PER_KEY;

		tmp = offset & ENTRIES_MASK;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = ENTRIES_PER_KEY - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) & ENTRIES_MASK;
			if (tmp == 0)
				tmp = ENTRIES_PER_KEY;
			nr = tmp - stripe_offset;
		}

		rc = update_array(array, epoch, stripe + 1, stripe_offset, nr,
				  cursor);
		if (rc != 0)
			break;
		cursor += nr;
	}

	D_ASSERT(cursor == (elements + count));

	return rc;
}

int
vts_array_punch(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	uint64_t		 first = offset >> ENTRIES_LOG2;
	uint64_t		 last = (offset + count - 1) >> ENTRIES_LOG2;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = ENTRIES_PER_KEY;

		tmp = offset & ENTRIES_MASK;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = ENTRIES_PER_KEY - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) & ENTRIES_MASK;
			if (tmp == 0)
				tmp = ENTRIES_PER_KEY;
			nr = tmp - stripe_offset;
		}

		if (nr != ENTRIES_PER_KEY) {
			rc = update_array(array, epoch, stripe + 1,
					  stripe_offset, nr, NULL);
		} else {
			array->va_dkey_value = stripe + 1;
			rc = vos_obj_punch(array->va_coh, array->va_oid, epoch,
					   0, 0, &array->va_dkey, 0, NULL,
					   NULL);
		}
		if (rc != 0)
			break;
	}

	return rc;
}

int
vts_array_read(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
	       uint64_t count, int32_t *elements)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	int32_t			*cursor = elements;
	uint64_t		 first = offset >> ENTRIES_LOG2;
	uint64_t		 last = (offset + count - 1) >> ENTRIES_LOG2;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = ENTRIES_PER_KEY;

		tmp = offset & ENTRIES_MASK;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = ENTRIES_PER_KEY - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) & ENTRIES_MASK;
			if (tmp == 0)
				tmp = ENTRIES_PER_KEY;
			nr = tmp - stripe_offset;
		}

		rc = fetch_array(array, epoch, stripe + 1, stripe_offset, nr,
				 cursor);
		if (rc != 0)
			break;
		cursor += nr;
	}

	return rc;
}
