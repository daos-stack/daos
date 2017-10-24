#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#define DDSUBSYS       DDFAC(tests)

#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>

#define TS_KEY_LEN	32
#define TS_VAL_LEN	32
#define TS_IDX_MAX	(1000 * 1000)

char			*ts_pmem_file = "/mnt/daos/vos_perf.pmem";
unsigned long		 ts_pool_size = (1U << 30);
unsigned int		 ts_obj_p_cont = 1;
unsigned int		 ts_dkey_p_obj = 1;
unsigned int		 ts_akey_p_dkey = 1;
unsigned int		 ts_recx_p_akey = 1;
bool			 ts_single = true;
bool			 ts_zero_copy;
uuid_t			 ts_pool;
uuid_t			 ts_cont;
uuid_t			 ts_cookie;
daos_handle_t		 ts_poh;
daos_handle_t		 ts_coh;
daos_unit_oid_t		 ts_oid;

static int
ts_vos_update(daos_key_t *dkey, daos_iod_t *iod, daos_sg_list_t *sgl_src,
	      daos_epoch_t epoch)
{
	int	rc;

	if (!ts_zero_copy) {
		rc = vos_obj_update(ts_coh, ts_oid, epoch, ts_cookie, 0,
				    dkey, 1, iod, sgl_src);
		if (rc)
			return -1;
	} else {
		daos_sg_list_t	*sgl_dst;
		daos_handle_t	 ioh;

		rc = vos_obj_zc_update_begin(ts_coh, ts_oid, epoch, dkey, 1,
					     iod, &ioh);
		if (rc)
			return rc;

		rc = vos_obj_zc_sgl_at(ioh, 0, &sgl_dst);
		if (rc)
			return rc;

		D__ASSERT(sgl_src->sg_nr.num == 1);
		D__ASSERT(sgl_dst->sg_nr.num_out == 1);

		memcpy(sgl_dst->sg_iovs[0].iov_buf,
		       sgl_src->sg_iovs[0].iov_buf,
		       sgl_src->sg_iovs[0].iov_len);

		rc = vos_obj_zc_update_end(ioh, ts_cookie, 0, dkey, 1, iod, 0);
		if (rc)
			return rc;
	}
	return 0;
}

static int
ts_key_insert(void)
{
	int			*indices;
	daos_key_t		 dkey;
	daos_iov_t		 val;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	daos_sg_list_t		 sgl;
	char			 dkey_buf[TS_KEY_LEN];
	char			 akey_buf[TS_KEY_LEN];
	char			 val_buf[TS_KEY_LEN];
	int			 val_len;
	int			 i;
	int			 j;
	int			 rc = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D__ASSERT(indices != NULL);

	dts_key_gen(dkey_buf, TS_KEY_LEN, "Jon");
	daos_iov_set(&dkey, dkey_buf, strlen(dkey_buf));

	val_len = snprintf(val_buf, TS_KEY_LEN, "WinderIsComing");

	for (i = 0; i < ts_akey_p_dkey; i++) {
		daos_epoch_t epoch = 0;

		memset(&iod, 0, sizeof(iod));

		dts_key_gen(akey_buf, TS_KEY_LEN, "Sam");
		daos_iov_set(&iod.iod_name, akey_buf, strlen(akey_buf));
		if (ts_single) {
			iod.iod_type = DAOS_IOD_SINGLE;
			iod.iod_size = val_len;
		} else {
			iod.iod_type = DAOS_IOD_ARRAY;
			iod.iod_size = 1;
		}
		iod.iod_nr    = 1;
		iod.iod_recxs = &recx;

		daos_iov_set(&val, val_buf, val_len);
		memset(&sgl, 0, sizeof(sgl));
		sgl.sg_nr.num = 1;
		sgl.sg_iovs = &val;

		for (j = 0; j < ts_recx_p_akey; j++) {
			memset(&recx, 0, sizeof(recx));
			if (ts_single) {
				epoch++;
				recx.rx_nr = 1;
			} else {
				recx.rx_idx = indices[j] * 32;
				recx.rx_nr  = val_len;
			}

			rc = ts_vos_update(&dkey, &iod, &sgl, epoch);
			if (rc != 0) {
				fprintf(stderr, "Update failed: %d\n", rc);
				D__GOTO(failed, rc);
			}
		}
	}
failed:
	free(indices);
	return rc;
}

int
ts_update_perf(void)
{
	int	rc;
	int	i;
	int	j;

	for (i = 0; i < ts_obj_p_cont; i++) {
		ts_oid = dts_unit_oid_gen(0, 0);
		for (j = 0; j < ts_dkey_p_obj; j++) {
			rc = ts_key_insert();
			if (rc)
				return rc;
		}
	}
	return 0;
}

int
ts_prepare(void)
{
	int	fd;
	int	rc;

	uuid_generate(ts_pool);
	uuid_generate(ts_cont);
	uuid_generate(ts_cookie);

	fd = open(ts_pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (fd < 0) {
		fprintf(stderr, "Failed to open file\n");
		return -1;
	}

	D__PRINT("pool file=%s, size=%lu\n", ts_pmem_file, ts_pool_size);
	rc = posix_fallocate(fd, 0, ts_pool_size);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_pool_create(ts_pmem_file, ts_pool, 0);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_pool_open(ts_pmem_file, ts_pool, &ts_poh);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_cont_create(ts_poh, ts_cont);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_cont_open(ts_poh, ts_cont, &ts_coh);
	D__ASSERT(!rc);

	return 0;
}

void
ts_finish(void)
{
	vos_cont_close(ts_coh);
	vos_cont_destroy(ts_poh, ts_cont);

	vos_pool_close(ts_poh);
	vos_pool_destroy(ts_pmem_file, ts_pool);
}

static struct option ts_ops[] = {
	{ "type",	required_argument,	NULL,	't' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "recx",	required_argument,	NULL,	'r' },
	{ "zcopy",	required_argument,	NULL,	'z' },
	{ NULL,		0,			NULL,	0   },
};

unsigned int
ts_val_factor(unsigned int val, char factor)
{
	switch (factor) {
	default:
		return val;
	case 'k':
	case 'K':
		val *= 1000;
		return val;
	case 'm':
	case 'M':
		val *= 1000 * 1000;
		return val;
	}
}

int
main(int argc, char **argv)
{
	double		then;
	double		now;
	int		rc;

	rc = daos_debug_init(NULL);
	if (rc) {
		fprintf(stderr, "Failed to initialize debug\n");
		return rc;
	}

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "Failed to initialize VOS\n");
		return rc;
	}

	while ((rc = getopt_long(argc, argv, "t:o:d:a:r:z:",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 't':
			ts_single = *optarg == 's' ? true : false;
			break;
		case 'o':
			ts_obj_p_cont = strtoul(optarg, &endp, 0);
			ts_obj_p_cont = ts_val_factor(ts_obj_p_cont, *endp);
			break;
		case 'd':
			ts_dkey_p_obj = strtoul(optarg, &endp, 0);
			ts_dkey_p_obj = ts_val_factor(ts_dkey_p_obj, *endp);
			break;
		case 'a':
			ts_akey_p_dkey = strtoul(optarg, &endp, 0);
			ts_akey_p_dkey = ts_val_factor(ts_akey_p_dkey, *endp);
			break;
		case 'r':
			ts_recx_p_akey = strtoul(optarg, &endp, 0);
			ts_recx_p_akey = ts_val_factor(ts_recx_p_akey, *endp);
			break;
		case 'z':
			ts_zero_copy = !!atoi(optarg);
			break;
		}
	}

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 || ts_recx_p_akey == 0) {
		fprintf(stderr, "Invalid arguments %d/%d\\n",
			ts_akey_p_dkey, ts_recx_p_akey);
		return -1;
	}

	D__PRINT("rec_type=%s, zero_copy=%s, obj_per_cont=%u, dkey_per_obj=%u, "
		"akey_per_dkey=%u, recx_per_akey=%u\n",
		ts_single ? "single" : "array", ts_zero_copy ? "yes" : "no",
		ts_obj_p_cont, ts_dkey_p_obj, ts_akey_p_dkey, ts_recx_p_akey);

	rc = ts_prepare();
	if (rc)
		return -1;

	then = dts_time_now();

	/* TODO: add fetch performance test */
	rc = ts_update_perf();
	if (rc) {
		fprintf(stderr, "Test failed: %d\n", rc);
	} else {
		unsigned long total;

		now = dts_time_now();
		total = ts_obj_p_cont * ts_dkey_p_obj *
			ts_akey_p_dkey * ts_recx_p_akey;

		D__PRINT("duration = %12.8f sec, iops = %10.2f/sec, "
			"latency =%12.6f us\n",
			now - then, total / (now - then),
			((now - then) * 1000 * 1000) / total);
	}

	ts_finish();
	vos_fini();
	daos_debug_fini();

	return 0;
}
