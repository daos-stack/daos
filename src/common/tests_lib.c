/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

/**
 * Test suite helper functions.
 */
#include <daos/common.h>
#include <daos/object.h>
#include <daos/tests_lib.h>
#include <daos.h>
#include <gurt/debug.h>

static uint32_t obj_id_gen	= 1;
static uint64_t int_key_gen	= 1;

daos_obj_id_t
dts_oid_gen(unsigned seed)
{
	daos_obj_id_t	oid;
	uint64_t	hdr;

	hdr = seed;
	hdr <<= 32;

	/* generate a unique and not scary long object ID */
	oid.lo	= obj_id_gen++;
	oid.lo	|= hdr;
	oid.hi	= rand() % 100;

	return oid;
}

daos_unit_oid_t
dts_unit_oid_gen(enum daos_otype_t type, uint32_t shard)
{
	daos_unit_oid_t	uoid;

	uoid.id_pub	= dts_oid_gen((unsigned int)(time(NULL) & 0xFFFFFFFFUL));
	daos_obj_set_oid(&uoid.id_pub, type, DTS_OCLASS_DEF, shard + 1, 0);
	uoid.id_shard	= shard;
	uoid.id_layout_ver = 0;
	uoid.id_padding = 0;

	return uoid;
}

void
dts_key_gen(char *key, unsigned int key_len, const char *prefix)
{
	memset(key, 0, key_len);
	if (prefix == NULL)
		memcpy(key, &int_key_gen, sizeof(int_key_gen));
	else
		snprintf(key, key_len, "%s-"DF_U64, prefix, int_key_gen);
	int_key_gen++;
}

void
dts_buf_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

void
dts_buf_render_uppercase(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = ('a' + randv) - 32;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

static void
rand_iarr_swap(void *array, int a, int b)
{
	uint64_t	*iarray = array;
	uint64_t	 tmp;

	tmp = iarray[a];
	iarray[a] = iarray[b];
	iarray[b] = tmp;
}

static daos_sort_ops_t rand_iarr_ops = {
	.so_swap	= rand_iarr_swap,
};

uint64_t *
dts_rand_iarr_alloc(int nr)
{
	uint64_t	*array;

	D_ALLOC_ARRAY(array, nr);
	if (!array)
		return NULL;

	return array;
}

void
dts_rand_iarr_set(uint64_t *array, int nr, int base, bool shuffle)
{
	int		 i;

	for (i = 0; i < nr; i++)
		array[i] = base + i;

	if (shuffle)
		daos_array_shuffle((void *)array, nr, &rand_iarr_ops);
}

uint64_t *
dts_rand_iarr_alloc_set(int nr, int base, bool shuffle)
{
	uint64_t	*array;

	array = dts_rand_iarr_alloc(nr);
	if (!array)
		return NULL;

	dts_rand_iarr_set(array, nr, base, shuffle);
	return array;
}

void
dts_reset_key(void)
{
	int_key_gen = 1;
}

void
dts_log(const char *msg, const char *file, const char *func, int line,
		uint64_t py_logfac)
{
	int logfac = 0;

	switch (py_logfac) {
	case 0:
		logfac = DB_ANY;
		break;
	case 1:
		logfac = DLOG_INFO;
		break;
	case 2:
		logfac = DLOG_WARN;
		break;
	case 3:
		logfac = DLOG_ERR;
		break;
	}

	int mask = d_log_check(logfac | D_LOGFAC);

	if (mask)
		d_log(mask, "%s:%d %s() %s", file, line, func, msg);
}

static void
v_dts_sgl_init_with_strings_repeat(d_sg_list_t *sgl, uint32_t repeat,
				   uint32_t count, char *d,
				   va_list valist)
{
	char *arg = d;
	int i, j;

	d_sgl_init(sgl, count);
	for (i = 0; i < count; i++) {
		size_t arg_len = strlen(arg);

		char *buf = NULL;
		size_t buf_len = arg_len * repeat + 1; /** +1 for NULL
							* Terminator
							*/
		D_ALLOC(buf, buf_len);
		D_ASSERT(buf != 0);
		for (j = 0; j < repeat; j++)
			memcpy(buf + j * arg_len, arg, arg_len);
		buf[buf_len - 1] = '\0';

		sgl->sg_iovs[i].iov_buf = buf;
		sgl->sg_iovs[i].iov_buf_len = sgl->sg_iovs[i].iov_len = buf_len;

		arg = va_arg(valist, char *);
	}
	sgl->sg_nr_out = count;
}

void
dts_sgl_init_with_strings(d_sg_list_t *sgl, uint32_t count, char *d, ...)
{
	va_list valist;

	va_start(valist, d);
	v_dts_sgl_init_with_strings_repeat(sgl, 1, count, d, valist);
	va_end(valist);
}

void
dts_sgl_init_with_strings_repeat(d_sg_list_t *sgl, uint32_t repeat,
				 uint32_t count, char *d, ...)
{
	va_list valist;

	va_start(valist, d);
	v_dts_sgl_init_with_strings_repeat(sgl, repeat, count, d, valist);
	va_end(valist);
}

void
dts_sgl_alloc_single_iov(d_sg_list_t *sgl, daos_size_t size)
{
	d_sgl_init(sgl, 1);
	D_ALLOC(sgl->sg_iovs[0].iov_buf, size);
	D_ASSERT(sgl->sg_iovs[0].iov_buf != NULL);
	sgl->sg_iovs[0].iov_buf_len = size;
}

void
dts_sgl_generate(d_sg_list_t *sgl, uint32_t iov_nr, daos_size_t data_size, uint8_t value)
{
	int i;

	d_sgl_init(sgl, iov_nr);

	for (i = 0; i < iov_nr; i++) {
		daos_iov_alloc(&sgl->sg_iovs[0], data_size, true);
		D_ASSERT(sgl->sg_iovs[0].iov_buf != NULL);
		memset(sgl->sg_iovs[0].iov_buf, value, data_size);
	}
	sgl->sg_nr_out = iov_nr; /* All have data */
}

void
td_init(struct test_data *td, uint32_t iod_nr, struct td_init_args args)
{
	int i, j;
	const daos_iod_type_t	*iod_types = args.ca_iod_types;
	const uint32_t		*recx_nr = args.ca_recx_nr;

	if (args.ca_data_size == 0)
		args.ca_data_size = 100;

	D_ALLOC_ARRAY(td->td_iods, iod_nr);
	D_ALLOC_ARRAY(td->td_sgls, iod_nr);
	D_ALLOC_ARRAY(td->td_maps, iod_nr);
	D_ALLOC_ARRAY(td->td_sizes, iod_nr);
	td->td_iods_nr = iod_nr;

	dts_iov_alloc_str(&td->dkey, "dkey");

	for (i = 0; i < iod_nr; i++) {
		daos_iod_t	*iod = &td->td_iods[i];
		d_sg_list_t	*sgl = &td->td_sgls[i];
		daos_iom_t	*map = &td->td_maps[i];
		uint32_t	 data_len = args.ca_data_size;

		/* Initialize and create some data */
		dts_sgl_generate(sgl, 1, args.ca_data_size, 0xAB);
		D_ASSERT(daos_sgl_data_len(sgl) == data_len);

		iod->iod_type = iod_types[i];
		dts_iov_alloc_str(&iod->iod_name, "akey");
		if (iod_types[i] == DAOS_IOD_ARRAY) {
			D_ALLOC_ARRAY(iod->iod_recxs, recx_nr[i]);
			D_ALLOC_ARRAY(map->iom_recxs, recx_nr[i]);

			iod->iod_nr = recx_nr[i];
			iod->iod_size = 1;
			map->iom_nr = recx_nr[i];
			map->iom_nr_out = recx_nr[i];

			/* split the data evenly over the recxs */
			for (j = 0; j < iod->iod_nr; j++) {
				daos_recx_t *recx = &iod->iod_recxs[j];

				recx->rx_nr = data_len / iod->iod_nr;
				recx->rx_idx = recx->rx_nr * j;
				map->iom_recxs[j] = *recx;
			}
			map->iom_recx_lo = map->iom_recxs[0];
			map->iom_recx_hi = map->iom_recxs[map->iom_nr - 1];
		} else {
			iod->iod_size = data_len;
			iod->iod_nr = 1;
		}
		map->iom_size = iod->iod_size;
		map->iom_type = iod->iod_type;
		td->td_sizes[i] = iod->iod_size;
	}
}

void
td_init_single_values(struct test_data *td, uint32_t iod_nr)
{
	struct td_init_args args = {0};
	int i;

	for (i = 0; i < iod_nr; i++)
		args.ca_iod_types[i] = DAOS_IOD_SINGLE;

	td_init(td, iod_nr, args);
}

void
td_init_array_values(struct test_data *td, uint32_t iod_nr, uint32_t recx_nr, uint32_t data_size,
		     uint32_t chunksize)
{
	struct td_init_args args = {0};
	int i;

	for (i = 0; i < iod_nr; i++) {
		args.ca_iod_types[i] = DAOS_IOD_ARRAY;
		args.ca_recx_nr[i] = recx_nr;
	}

	args.ca_data_size = data_size;

	td_init(td, iod_nr, args);
}

void
td_destroy(struct test_data *td)
{
	int i;

	for (i = 0; i < td->td_iods_nr; i++) {
		D_FREE(td->td_iods[i].iod_recxs);
		D_FREE(td->td_maps[i].iom_recxs);
		daos_iov_free(&td->td_iods[i].iod_name);

		d_sgl_fini(&td->td_sgls[i], true);
	}

	daos_iov_free(&td->dkey);
	D_FREE(td->td_sgls);
	D_FREE(td->td_maps);
	D_FREE(td->td_sizes);
	D_FREE(td->td_iods);
}
