/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_array.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include "vts_array.h"

struct vts_metadata {
	uint64_t	 vm_magic;
	uint64_t	 vm_record_size;
	uint64_t	 vm_per_key;
	uint64_t	 vm_akey_size;
};

struct vts_array {
	daos_unit_oid_t		 va_oid;
	daos_handle_t		 va_coh;
	daos_iod_t		 va_iod;
	daos_iod_t		 va_sv_iod;
	d_iov_t			 va_dkey;
	uint64_t		 va_recx_nr;
	daos_recx_t		*va_recx;
	d_iov_t			*va_iovs;
	uint64_t		 va_dkey_value;
	uint64_t		 va_io_size;
	d_sg_list_t		 va_sgl;
	struct vts_metadata	 va_meta;
	uint8_t			*va_akey_value;
	uint8_t			*va_zero;
};

#define ARRAY_MAGIC 0xdeadbeef

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

	D_ASSERT(array != NULL && array->va_meta.vm_magic == ARRAY_MAGIC);

	return array;
}

static int
array_open(struct vts_array *array)
{
	D_ALLOC_ARRAY(array->va_akey_value, array->va_meta.vm_akey_size);
	if (array->va_akey_value == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(array->va_zero, array->va_meta.vm_record_size);
	if (array->va_zero == NULL)
		return -DER_NOMEM;

	array->va_recx_nr = array->va_io_size = array->va_meta.vm_per_key;
	D_ALLOC_ARRAY(array->va_recx, array->va_io_size);
	if (array->va_recx == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(array->va_iovs, array->va_io_size);
	if (array->va_recx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
array_init(struct vts_array *in, daos_handle_t coh, daos_unit_oid_t oid,
	   struct vts_array **out)
{
	D_ASSERT((in == NULL && out != NULL) || (in != NULL && out == NULL));

	if (in == NULL) {
		D_ALLOC_PTR(in);
		if (in == NULL)
			return -DER_NOMEM;
	} else {
		memset(in, 0, sizeof(*in));
	}

	if (out)
		*out = in;

	d_iov_set(&in->va_dkey, &in->va_dkey_value, sizeof(in->va_dkey_value));
	in->va_iod.iod_type = DAOS_IOD_ARRAY;
	in->va_iod.iod_recxs = in->va_recx;
	in->va_iod.iod_nr = 1;
	in->va_coh = coh;
	in->va_oid = oid;

	in->va_sv_iod.iod_type = DAOS_IOD_SINGLE;
	in->va_sv_iod.iod_recxs = NULL;
	in->va_sv_iod.iod_nr = 1;
	in->va_sv_iod.iod_size = sizeof(struct vts_metadata);

	return 0;
}

static void
array_fini(struct vts_array *in, bool free_array)
{
	in->va_meta.vm_magic = 0;
	if (!free_array)
		return;

	D_FREE(in->va_zero);
	D_FREE(in->va_akey_value);
	D_FREE(in->va_recx);
	D_FREE(in->va_iovs);
	D_FREE(in);
}

static int
update_array(struct vts_array *array, daos_epoch_t epoch, uint64_t dkey,
	     uint64_t rec_size, uint64_t offset, uint64_t nr, void *values)
{
	d_sg_list_t	*sgls = NULL;
	d_sg_list_t	 sgl;
	uint64_t	 cursor = offset;
	uint64_t	 left = nr;
	int		 idx = 0;

	if (values)
		sgls = &sgl;

	while (cursor < (offset + nr)) {
		array->va_recx[idx].rx_idx = cursor;
		if (left > array->va_io_size)
			array->va_recx[idx].rx_nr = array->va_io_size;
		else
			array->va_recx[idx].rx_nr = left;
		if (values) {
			d_iov_set(&array->va_iovs[idx],
				  values + (cursor - offset) * rec_size,
				  array->va_recx[idx].rx_nr * rec_size);
		}

		cursor += array->va_io_size;
		left -= array->va_io_size;
		idx++;
	}

	array->va_iod.iod_recxs = array->va_recx;
	array->va_iod.iod_nr = idx;
	sgl.sg_iovs = array->va_iovs;
	sgl.sg_nr = idx;
	sgl.sg_nr_out = 0;

	array->va_dkey_value = dkey;
	array->va_iod.iod_size = rec_size;
	d_iov_set(&array->va_iod.iod_name, array->va_akey_value,
		  array->va_meta.vm_akey_size);

	D_DEBUG(DB_IO, "Writing "DF_U64" records of size "DF_U64" at offset "
		DF_U64"\n", nr, rec_size, offset);
	return vos_obj_update(array->va_coh, array->va_oid, epoch, 0, 0,
			      &array->va_dkey, 1, &array->va_iod, NULL, sgls);
}

static int
fetch_array(struct vts_array *array, daos_epoch_t epoch, uint64_t dkey,
	    uint64_t rec_size, uint64_t offset, uint64_t nr, void *values)
{
	d_sg_list_t	 sgl;
	d_sg_list_t	*sgls = &sgl;
	uint64_t	 cursor = offset;
	uint64_t	 left = nr;
	int		 idx = 0;

	while (cursor < (offset + nr)) {
		array->va_recx[idx].rx_idx = cursor;
		if (left > array->va_io_size)
			array->va_recx[idx].rx_nr = array->va_io_size;
		else
			array->va_recx[idx].rx_nr = left;
		if (values) {
			d_iov_set(&array->va_iovs[idx],
				  values + (cursor - offset) * rec_size,
				  array->va_recx[idx].rx_nr * rec_size);
		}

		cursor += array->va_io_size;
		left -= array->va_io_size;
		idx++;
	}

	array->va_iod.iod_nr = idx;
	sgl.sg_iovs = array->va_iovs;
	sgl.sg_nr = idx;
	sgl.sg_nr_out = 0;

	array->va_iod.iod_size = rec_size;
	array->va_dkey_value = dkey;
	d_iov_set(&array->va_iod.iod_name, array->va_akey_value,
		  array->va_meta.vm_akey_size);

	D_DEBUG(DB_IO, "Reading "DF_U64" records of size "DF_U64" at offset "
		DF_U64"\n", nr, rec_size, offset);
	return vos_obj_fetch(array->va_coh, array->va_oid, epoch, 0,
			     &array->va_dkey, 1, &array->va_iod, sgls);
}

static int
update_meta(struct vts_array *array, daos_epoch_t epoch,
	    struct vts_metadata *meta)
{
	d_sg_list_t	 sgl = {0};
	d_iov_t		 iov;
	uint8_t		 akey = 0;

	array->va_dkey_value = 0;
	d_iov_set(&iov, meta, sizeof(*meta));
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;

	d_iov_set(&array->va_sv_iod.iod_name, &akey, sizeof(akey));

	D_DEBUG(DB_IO, "Writing metadata at epoch "DF_U64"\n", epoch);
	return vos_obj_update(array->va_coh, array->va_oid, epoch, 0, 0,
			&array->va_dkey, 1, &array->va_sv_iod, NULL, &sgl);
}

static int
fetch_meta(struct vts_array *array, daos_epoch_t epoch,
	   struct vts_metadata *meta)
{
	d_sg_list_t	 sgl = {0};
	d_iov_t		 iov;
	uint8_t		 akey = 0;

	array->va_dkey_value = 0;
	d_iov_set(&iov, meta, sizeof(*meta));
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;

	d_iov_set(&array->va_sv_iod.iod_name, &akey, sizeof(akey));

	D_DEBUG(DB_IO, "Reading metadata at epoch "DF_U64"\n", epoch);
	return vos_obj_fetch(array->va_coh, array->va_oid, epoch, 0,
			     &array->va_dkey, 1, &array->va_sv_iod, &sgl);
}


int
vts_array_alloc(daos_handle_t coh, daos_epoch_t epoch, daos_size_t record_size,
		daos_size_t nr_per_key, daos_size_t akey_size,
		daos_unit_oid_t *oid)
{
	struct vts_array	 array;
	struct vts_metadata	 meta;
	int			 rc = 0;

	*oid = dts_unit_oid_gen(0, DAOS_OF_DKEY_UINT64, 0);
	rc = array_init(&array, coh, *oid, NULL);
	if (rc != 0)
		return rc;

	meta.vm_magic = ARRAY_MAGIC;
	meta.vm_record_size = record_size;
	meta.vm_per_key = nr_per_key ? nr_per_key : 8;
	meta.vm_akey_size = akey_size ? akey_size : 1;
	rc = update_meta(&array, epoch, &meta);

	array_fini(&array, false);

	if (rc != 0)
		D_ERROR("Failed to create array: "DF_RC"\n", DP_RC(rc));

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
	int			 rc;

	rc = array_init(NULL, coh, oid, &array);
	if (rc != 0)
		return rc;

	rc = fetch_meta(array, DAOS_EPOCH_MAX, &array->va_meta);
	if (rc != 0 || array->va_meta.vm_magic != ARRAY_MAGIC) {
		if (rc == 0)
			rc = -DER_INVAL;
		array_fini(array, true);
		return rc;
	}

	rc = array_open(array);
	if (rc != 0) {
		array_fini(array, true);
		return rc;
	}

	D_ASSERT(array->va_meta.vm_magic == ARRAY_MAGIC);
	*aoh = vts_array2hdl(array);
	return 0;
}

int
vts_array_reset(daos_handle_t *aoh, daos_epoch_t punch_epoch,
		daos_epoch_t create_epoch, daos_size_t record_size,
		daos_size_t nr_per_key, daos_size_t akey_size)
{
	struct vts_array	*array = vts_hdl2array(*aoh);
	struct vts_metadata	 meta;
	daos_handle_t		 coh;
	daos_unit_oid_t		 oid;
	int			 rc;

	D_ASSERT(punch_epoch < create_epoch);

	rc = vos_obj_punch(array->va_coh, array->va_oid, punch_epoch, 0, 0,
			   NULL, 0, NULL, NULL);
	if (rc != 0)
		return rc;

	meta.vm_magic = ARRAY_MAGIC;
	meta.vm_record_size = record_size;
	meta.vm_per_key = nr_per_key ? nr_per_key : 8;
	meta.vm_akey_size = akey_size ? akey_size : 1;
	rc = update_meta(array, create_epoch, &meta);
	if (rc != 0)
		return rc;

	oid = array->va_oid;
	coh = array->va_coh;

	vts_array_close(*aoh);
	return vts_array_open(coh, oid, aoh);
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
	struct vts_array	*array = vts_hdl2array(aoh);
	daos_size_t		 old_size;
	int			 rc;

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

	return vts_array_write(aoh, epoch, new_size - 1, 1, array->va_zero);
}

int
vts_array_get_size(daos_handle_t aoh, daos_epoch_t epoch, daos_size_t *size)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	daos_recx_t		 recx;
	int			 rc;
	d_iov_t			 dkey;
	uint64_t		 val;

	d_iov_set(&dkey, NULL, 0);
	d_iov_set(&array->va_iod.iod_name, array->va_akey_value,
		  array->va_meta.vm_akey_size);

	rc = vos_obj_query_key(array->va_coh, array->va_oid,
			       DAOS_GET_DKEY | DAOS_GET_RECX | DAOS_GET_MAX,
			       epoch, &dkey, &array->va_iod.iod_name,
			       &recx, NULL);

	if (rc == -DER_NONEXIST) {
		*size = 0;
		return 0;
	}

	if (rc != 0)
		return rc;

	val = *(uint64_t *)dkey.iov_buf;
	if (val == 0) {
		*size = 0;
		return 0;
	}

	*size = (val - 1) * array->va_meta.vm_per_key + recx.rx_idx +
		recx.rx_nr;

	return 0;
}

int
vts_array_set_iosize(daos_handle_t aoh, uint64_t io_size)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	uint64_t		 new_size;
	uint64_t		 count;

	new_size = io_size;

	if (io_size != 0 && io_size <= array->va_meta.vm_per_key)
		goto resize;

	/* Default to per_key */
	new_size = array->va_meta.vm_per_key;
resize:
	count = array->va_meta.vm_per_key / new_size;
	if (array->va_meta.vm_per_key % new_size)
		count++;

	if (count > array->va_recx_nr) {
		d_iov_t		*iovs;
		daos_recx_t	*recx;

		count = count * 2;
		D_ALLOC_ARRAY(recx, count);
		if (recx == NULL)
			return -DER_NOMEM;
		D_ALLOC_ARRAY(iovs, count);
		if (iovs == NULL) {
			D_FREE(recx);
			return -DER_NOMEM;
		}

		D_FREE(array->va_recx);
		D_FREE(array->va_iovs);
		array->va_recx_nr = count;
		array->va_recx = recx;
		array->va_iovs = iovs;
	}

	array->va_io_size = new_size;
	return 0;
}

int
vts_array_write(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count, void *elements)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	void			*cursor = elements;
	uint64_t		 first;
	uint64_t		 last;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	first = offset / array->va_meta.vm_per_key;
	last = (offset + count - 1) / array->va_meta.vm_per_key;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = array->va_meta.vm_per_key;

		tmp = offset % array->va_meta.vm_per_key;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = array->va_meta.vm_per_key - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) % array->va_meta.vm_per_key;
			if (tmp == 0)
				tmp = array->va_meta.vm_per_key;
			nr = tmp - stripe_offset;
		}

		rc = update_array(array, epoch, stripe + 1,
				  array->va_meta.vm_record_size, stripe_offset,
				  nr, cursor);
		if (rc != 0)
			break;
		cursor += nr * array->va_meta.vm_record_size;
	}

	D_ASSERT(cursor ==
		 (elements + (count * array->va_meta.vm_record_size)));

	return rc;
}

int
vts_array_punch(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	uint64_t		 first;
	uint64_t		 last;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	first = offset / array->va_meta.vm_per_key;
	last = (offset + count - 1) / array->va_meta.vm_per_key;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = array->va_meta.vm_per_key;

		tmp = offset % array->va_meta.vm_per_key;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = array->va_meta.vm_per_key - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) % array->va_meta.vm_per_key;
			if (tmp == 0)
				tmp = array->va_meta.vm_per_key;
			nr = tmp - stripe_offset;
		}

		if (nr != array->va_meta.vm_per_key) {
			rc = update_array(array, epoch, stripe + 1,
					  0 /* punch */, stripe_offset, nr,
					  NULL);
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
	       uint64_t count, void *elements)
{
	struct vts_array	*array = vts_hdl2array(aoh);
	void			*cursor = elements;
	uint64_t		 first;
	uint64_t		 last;
	uint64_t		 stripe;
	uint64_t		 stripe_offset;
	uint64_t		 nr;
	int			 rc = 0;

	first = offset / array->va_meta.vm_per_key;
	last = (offset + count - 1) / array->va_meta.vm_per_key;

	D_ASSERT(last >= first);
	for (stripe = first; stripe <= last; stripe++) {
		uint64_t	tmp;

		stripe_offset = 0;
		nr = array->va_meta.vm_per_key;

		tmp = offset % array->va_meta.vm_per_key;
		if (stripe == first && tmp != 0) {
			stripe_offset = tmp;
			nr = array->va_meta.vm_per_key - stripe_offset;
		}
		if (stripe == last) {
			tmp = (count + offset) % array->va_meta.vm_per_key;
			if (tmp == 0)
				tmp = array->va_meta.vm_per_key;
			nr = tmp - stripe_offset;
		}

		rc = fetch_array(array, epoch, stripe + 1,
				 array->va_meta.vm_record_size, stripe_offset,
				 nr, cursor);
		if (rc != 0)
			break;
		cursor += nr * array->va_meta.vm_record_size;
	}

	D_ASSERT(cursor ==
		 (elements + (count * array->va_meta.vm_record_size)));

	return rc;
}
