/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fcntl.h>
#include <libgen.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <gurt/debug.h>
#include <ddb_common.h>
#include <ddb_main.h>
#include <sys/stat.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

#define DEFINE_IOV(str) {.iov_buf = str, .iov_buf_len = strlen(str), .iov_len = strlen(str)}

bool g_verbose; /* Can be set to true while developing/debugging tests */

const char *g_uuids_str[] = {
	"12345678-1234-1234-1234-123456789001",
	"12345678-1234-1234-1234-123456789002",
	"12345678-1234-1234-1234-123456789003",
	"12345678-1234-1234-1234-123456789004",
	"12345678-1234-1234-1234-123456789005",
	"12345678-1234-1234-1234-123456789006",
	"12345678-1234-1234-1234-123456789007",
	"12345678-1234-1234-1234-123456789008",
	"12345678-1234-1234-1234-123456789009",
	"12345678-1234-1234-1234-123456789010",
};

char *g_dkeys_str[] = {
	"dkey-1",
	"dkey-2",
	"dkey-3",
	"dkey-4",
	"dkey-5",
	"dkey-6",
	"dkey-7",
	"dkey-8",
	"dkey-9",
	"dkey-10",
};

char *g_akeys_str[] = {
	"akey-1",
	"akey-2",
	"akey-3",
	"akey-4",
	"akey-5",
	"akey-6",
	"akey-7",
	"akey-8",
	"akey-9",
	"akey-10",
};

daos_unit_oid_t	g_oids[10];
uuid_t		g_uuids[10];
daos_key_t	g_dkeys[10];
daos_key_t	g_akeys[10];

daos_obj_id_t
oid_gen(uint32_t lo)
{
	daos_obj_id_t	oid;
	uint64_t	hdr;

	hdr = 100;
	hdr <<= 32;

	/* generate a unique and not scary long object ID */
	oid.lo	= lo;
	oid.lo	|= hdr;
	oid.hi	= 100;

	return oid;
}

daos_unit_oid_t
gen_uoid(uint32_t lo)
{
	daos_unit_oid_t	uoid;

	uoid.id_pub	= oid_gen(lo);
	daos_obj_set_oid(&uoid.id_pub, daos_obj_feat2type(0), OC_SX, 1, 0);
	uoid.id_shard	= 0;
	uoid.id_pad_32	= 0;

	return uoid;
}

void
dvt_vos_insert_recx(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		    int recx_idx, char *data_str, daos_epoch_t epoch)
{
	daos_key_t dkey = DEFINE_IOV(dkey_str);

	d_iov_t iov = DEFINE_IOV(data_str);
	d_sg_list_t sgl = {.sg_iovs = &iov, .sg_nr = 1, .sg_nr_out = 1};

	daos_recx_t recx = {.rx_nr = daos_sgl_buf_size(&sgl), .rx_idx = recx_idx};
	daos_iod_t iod = {
		.iod_name = DEFINE_IOV(akey_str),
		.iod_type = DAOS_IOD_ARRAY,
		.iod_nr = 1,
		.iod_size = 1,
		.iod_recxs = &recx
	};

	assert_success(vos_obj_update(coh, uoid, epoch, 0, 0, &dkey, 1, &iod, NULL, &sgl));
}

void
dvt_vos_insert_single(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		      char *data_str, daos_epoch_t epoch)
{
	daos_key_t dkey = DEFINE_IOV(dkey_str);

	d_iov_t iov = DEFINE_IOV(data_str);
	d_sg_list_t sgl = {.sg_iovs = &iov, .sg_nr = 1, .sg_nr_out = 1};

	daos_iod_t iod = {
		.iod_name = DEFINE_IOV(akey_str),
		.iod_type = DAOS_IOD_SINGLE,
		.iod_nr = 1,
		.iod_size = strlen(data_str)
	};

	assert_success(vos_obj_update(coh, uoid, epoch, 0, 0, &dkey, 1, &iod, NULL, &sgl));
}

/*
 * -----------------------------------------------
 * Test infrastructure
 * -----------------------------------------------
 */

int
ddb_test_pool_setup(struct dt_vos_pool_ctx *tctx)
{
	int			 rc;
	uint64_t		 size = (1ULL << 30);
	struct stat		 st = {0};
	char			*pool_uuid = "12345678-1234-1234-1234-123456789012";

	if (strlen(tctx->dvt_pmem_file) == 0) {
		char dir[64] = {0};

		sprintf(dir, "/mnt/daos/%s", pool_uuid);
		if (stat(dir, &st) == -1) {
			if (!SUCCESS(mkdir(dir, 0700))) {
				rc = daos_errno2der(errno);
				return rc;
			}
		}
		snprintf(tctx->dvt_pmem_file, ARRAY_SIZE(tctx->dvt_pmem_file),
			 "%s/ddb_vos_test", dir);
	}
	if (uuid_is_null(tctx->dvt_pool_uuid))
		uuid_parse(pool_uuid, tctx->dvt_pool_uuid);

	D_ASSERT(!daos_file_is_dax(tctx->dvt_pmem_file));
	rc = open(tctx->dvt_pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (rc < 0) {
		rc = daos_errno2der(errno);
		return rc;
	}

	tctx->dvt_fd = rc;
	rc = fallocate(tctx->dvt_fd, 0, 0, size);
	if (rc) {
		rc = daos_errno2der(errno);
		close(tctx->dvt_fd);
		return rc;
	}

	rc = vos_pool_create(tctx->dvt_pmem_file, tctx->dvt_pool_uuid, 0, 0, 0, NULL);
	if (rc) {
		close(tctx->dvt_fd);
		return rc;
	}

	return rc;
}

static int
setup_global_arrays()
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_oids); i++)
		g_oids[i] = gen_uoid(i);

	for (i = 0; i < ARRAY_SIZE(g_uuids_str); i++)
		uuid_parse(g_uuids_str[i], g_uuids[i]);

	for (i = 0; i < ARRAY_SIZE(g_dkeys); i++)
		d_iov_set(&g_dkeys[i], g_dkeys_str[i], strlen(g_dkeys_str[i]));

	for (i = 0; i < ARRAY_SIZE(g_akeys); i++)
		d_iov_set(&g_akeys[i], g_akeys_str[i], strlen(g_akeys_str[i]));
	return 0;
}

int
ddb_test_setup_vos(void **state)
{
	struct dt_vos_pool_ctx *tctx = NULL;
	daos_handle_t poh;

	D_ASSERT(state);
	D_ALLOC_PTR(tctx);
	assert_non_null(tctx);

	assert_success(ddb_test_pool_setup(tctx));

	assert_success(vos_pool_open(tctx->dvt_pmem_file, tctx->dvt_pool_uuid, 0, &poh));
	dvt_insert_data(poh, 0, 0, 0, 0);
	vos_pool_close(poh);

	*state = tctx;

	return 0;
}

int
ddb_teardown_vos(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;

	assert_success(vos_pool_destroy(tctx->dvt_pmem_file, tctx->dvt_pool_uuid));
	close(tctx->dvt_fd);
	D_FREE(tctx);

	return 0;
}

void
dvt_iov_alloc(d_iov_t *iov, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	iov->iov_buf_len = iov->iov_len = len;
}

void
dvt_iov_alloc_str(d_iov_t *iov, const char *str)
{
	dvt_iov_alloc(iov, strlen(str) + 1);
	strcpy(iov->iov_buf, str);
}

void
dvt_insert_data(daos_handle_t poh, uint32_t conts, uint32_t objs, uint32_t dkeys, uint32_t akeys)
{
	daos_handle_t		coh;
	uint32_t		cont_to_create = ARRAY_SIZE(g_uuids);
	uint32_t		obj_to_create = ARRAY_SIZE(g_oids);
	uint32_t		dkeys_to_create = ARRAY_SIZE(g_dkeys);
	uint32_t		akeys_to_create = ARRAY_SIZE(g_akeys);
	int			c, o, d, a; /* loop indexes */

	if (conts > 0)
		cont_to_create = conts;
	if (objs > 0)
		obj_to_create = objs;
	if (dkeys > 0)
		dkeys_to_create = dkeys;
	if (akeys > 0)
		akeys_to_create = akeys;

	/* Setup by creating containers */
	for (c = 0; c < cont_to_create; c++) {
		assert_success(vos_cont_create(poh, g_uuids[c]));
		assert_success(vos_cont_open(poh, g_uuids[c], &coh));
		for (o = 0; o < obj_to_create; o++) {
			for (d = 0; d < dkeys_to_create; d++) {
				for (a = 0; a < akeys_to_create; a++) {
					if (a % 2 == 0) {
						dvt_vos_insert_recx(coh, g_oids[o],
								    g_dkeys_str[d],
								    g_akeys_str[a], 1,
								    "This is an array value", 1);
					} else {
						dvt_vos_insert_single(coh, g_oids[o],
								      g_dkeys_str[d],
								      g_akeys_str[a],
								      "This is a single value", 1);
					}
				}
			}
		}
		vos_cont_close(coh);
	}
}

void
dvt_delete_all_containers(daos_handle_t poh)
{
	int	c;
	uuid_t	uuid;

	for (c = 0; c < ARRAY_SIZE(g_uuids_str); c++) {
		uuid_parse(g_uuids_str[c], uuid);
		assert_success(vos_cont_destroy(poh, uuid));
	}
}


struct ddb_test_driver_arguments {
	bool	 dtda_create_vos_file;
};

static int
ddb_test_driver_arguments_parse(uint32_t argc, char **argv, struct ddb_test_driver_arguments *args)
{
	struct option	program_options[] = {
		{ "create_vos", optional_argument, NULL,	'c' },
		{ NULL }
	};
	int		index = 0, opt;

	memset(args, 0, sizeof(*args));

	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "c", program_options, &index)) != -1) {
		switch (opt) {
		case 'c':
			args->dtda_create_vos_file = true;
			break;
		case '?':
			printf("'%c' is unknown\n", optopt);
			return -DER_INVAL;
		default:
			return -DER_INVAL;
		}
	}

	return 0;
}

static int
create_test_vos_file()
{
	struct dt_vos_pool_ctx	tctx = {0};
	daos_handle_t		poh;
	int			conts = 2;
	int			objs = 5;
	int			dkeys = 5;
	int			akeys = 5;
	int			rc;

	rc = ddb_test_pool_setup(&tctx);
	if (!SUCCESS(rc)) {
		print_error("Unable to setup pool: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	assert_success(vos_pool_open(tctx.dvt_pmem_file, tctx.dvt_pool_uuid, 0, &poh));
	dvt_insert_data(poh, conts, objs, dkeys, akeys);
	vos_pool_close(poh);

	close(tctx.dvt_fd);

	print_message("VOS file created at: %s\n", tctx.dvt_pmem_file);
	print_message("\t- pool uuid: "DF_UUIDF"\n", DP_UUID(tctx.dvt_pool_uuid));
	print_message("\t- containers: %d\n", conts);
	print_message("\t- objs: %d\n", objs);
	print_message("\t- dkeys: %d\n", dkeys);
	print_message("\t- akeys: %d\n", akeys);

	return 0;
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
int main(int argc, char *argv[])
{
	struct ddb_test_driver_arguments	args = {0};
	int					rc;

	rc = ddb_init();
	if (rc != 0)
		return -rc;
	rc = vos_self_init("/mnt/daos");
	if (rc != 0) {
		fprintf(stderr, "Unable to initialize VOS: "DF_RC"\n", DP_RC(rc));
		ddb_fini();
		return -rc;
	}

	ddb_test_driver_arguments_parse(argc, argv, &args);

	setup_global_arrays();

	if (args.dtda_create_vos_file) {
		create_test_vos_file();
	} else {
		rc += ddb_parse_tests_run();
		rc += ddb_cmd_options_tests_run();
		rc += dv_tests_run();
		rc += dvc_tests_run();
		rc += ddb_main_tests();
	}

	vos_self_fini();
	ddb_fini();
	return rc;
}
