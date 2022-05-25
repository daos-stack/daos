/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fcntl.h>
#include <libgen.h>
#include <daos/tests_lib.h>
#include <daos_srv/dtx_srv.h>
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

const char *g_invalid_uuid_str = "99999999-9999-9999-9999-999999999999";
daos_unit_oid_t g_invalid_oid = {.id_pub = {.lo = 99999, .hi = 9999} };

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

char *g_invalid_key_str = "invalid key";

daos_unit_oid_t	g_oids[10];
uuid_t		g_uuids[10];
daos_key_t	g_dkeys[10];
daos_key_t	g_akeys[10];
daos_recx_t	g_recxs[10];
daos_key_t	g_invalid_key;
daos_recx_t	g_invalid_recx = {.rx_nr = 9999, .rx_idx = 9999};


daos_unit_oid_t
dvt_gen_uoid(uint32_t i)
{
	daos_unit_oid_t	uoid = {0};
	daos_obj_id_t	oid;

	oid.lo	= (1L << 32) + i;
	oid.hi = (1 << 16) + i;
	daos_obj_set_oid(&oid, DAOS_OT_MULTI_HASHED, OR_RP_1, 1, 0);

	uoid.id_shard	= 0;
	uoid.id_pad_32	= 0;
	uoid.id_pub = oid;

	return uoid;
}

void
dvt_vos_insert_recx(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		    daos_recx_t *recx, daos_epoch_t epoch)
{
	daos_key_t dkey = DEFINE_IOV(dkey_str);

	d_iov_t iov = DEFINE_IOV("This is a recx value");
	d_sg_list_t sgl = {.sg_iovs = &iov, .sg_nr = 1, .sg_nr_out = 1};

	daos_iod_t iod = {
		.iod_name = DEFINE_IOV(akey_str),
		.iod_type = DAOS_IOD_ARRAY,
		.iod_nr = 1,
		.iod_size = 1,
		.iod_recxs = recx
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
 * These tests look at and verify how the ddb types are printed.
 */

uint32_t dvt_fake_print_called;
char dvt_fake_print_buffer[1024];

int
dvt_fake_print(const char *fmt, ...)
{
	va_list args;
	uint32_t buffer_offset = strlen(dvt_fake_print_buffer);
	uint32_t buffer_left;

	buffer_left = ARRAY_SIZE(dvt_fake_print_buffer) - buffer_offset;
	dvt_fake_print_called++;
	va_start(args, fmt);
	vsnprintf(dvt_fake_print_buffer + buffer_offset, buffer_left, fmt, args);
	va_end(args);
	if (g_verbose)
		printf("%s", dvt_fake_print_buffer + buffer_offset);

	return 0;
}

void dvt_fake_print_reset(void)
{
	memset(dvt_fake_print_buffer, 0, ARRAY_SIZE(dvt_fake_print_buffer));
}

size_t dvt_fake_get_file_size_result;

size_t
dvt_fake_get_file_size(const char *path)
{
	return dvt_fake_get_file_size_result;
}

bool dvt_fake_get_file_exists_result;

bool
dvt_fake_get_file_exists(const char *path)
{
	return dvt_fake_get_file_exists_result;
}

size_t dvt_fake_read_file_result;
char dvt_fake_read_file_buf[64];

size_t
dvt_fake_read_file(const char *src_path, d_iov_t *contents)
{
	size_t to_copy = min(contents->iov_buf_len, ARRAY_SIZE(dvt_fake_read_file_buf));

	memcpy(contents->iov_buf, dvt_fake_read_file_buf, to_copy);
	contents->iov_len = to_copy;

	return dvt_fake_read_file_result;
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
		g_oids[i] = dvt_gen_uoid(i);

	for (i = 0; i < ARRAY_SIZE(g_uuids_str); i++)
		uuid_parse(g_uuids_str[i], g_uuids[i]);

	for (i = 0; i < ARRAY_SIZE(g_dkeys); i++)
		d_iov_set(&g_dkeys[i], g_dkeys_str[i], strlen(g_dkeys_str[i]));

	for (i = 0; i < ARRAY_SIZE(g_akeys); i++)
		d_iov_set(&g_akeys[i], g_akeys_str[i], strlen(g_akeys_str[i]));

	d_iov_set(&g_invalid_key, g_invalid_key_str, strlen(g_invalid_key_str));

	for (i = 0; i < ARRAY_SIZE(g_recxs); i++) {
		g_recxs[0].rx_idx = i;
		g_recxs[0].rx_nr = 10;
	}

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

static void
create_object_data(daos_handle_t *coh, uint32_t obj_to_create, uint32_t dkeys_to_create,
		   uint32_t akeys_to_create, uint32_t recx_to_create)
{
	int o, d, a, r; /* loop indexes */

	for (o = 0; o < obj_to_create; o++) {
		for (d = 0; d < dkeys_to_create; d++) {
			for (a = 0; a < akeys_to_create; a++) {
				if (a % 2 == 0) {
					for (r = 0; r < recx_to_create; r++)
						dvt_vos_insert_recx((*coh), g_oids[o],
								    g_dkeys_str[d],
								    g_akeys_str[a],
								    &g_recxs[r], 1);
				} else {
					dvt_vos_insert_single((*coh), g_oids[o],
							      g_dkeys_str[d],
							      g_akeys_str[a],
							      "This is a single value", 1);
				}
			}
		}
	}
}

void
dvt_insert_data(daos_handle_t poh, uint32_t conts, uint32_t objs, uint32_t dkeys, uint32_t akeys)
{
	daos_handle_t		coh;
	uint32_t		cont_to_create = ARRAY_SIZE(g_uuids);
	uint32_t		obj_to_create = ARRAY_SIZE(g_oids);
	uint32_t		dkeys_to_create = ARRAY_SIZE(g_dkeys);
	uint32_t		akeys_to_create = ARRAY_SIZE(g_akeys);
	uint32_t		recx_to_create = ARRAY_SIZE(g_recxs);
	int			c;

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

		create_object_data(&coh, obj_to_create, dkeys_to_create, akeys_to_create,
				   recx_to_create);
		vos_cont_close(coh);
	}
}

static void
dvt_dtx_begin_helper(daos_handle_t coh, const daos_unit_oid_t *oid, daos_epoch_t epoch,
		     uint64_t dkey_hash, struct dtx_handle **dthp)
{
	struct dtx_handle	*dth;
	struct dtx_memberships	*mbs;
	size_t			 size;

	D_ALLOC_PTR(dth);
	assert_non_null(dth);

	memset(dth, 0, sizeof(*dth));

	size = sizeof(struct dtx_memberships) + sizeof(struct dtx_daos_target);

	D_ALLOC(mbs, size);
	assert_non_null(mbs);

	mbs->dm_tgt_cnt = 1;
	mbs->dm_grp_cnt = 1;
	mbs->dm_data_size = sizeof(struct dtx_daos_target);
	mbs->dm_tgts[0].ddt_id = 1;

	/** Use unique API so new UUID is generated even on same thread */
	daos_dti_gen_unique(&(&dth->dth_dte)->dte_xid);
	dth->dth_dte.dte_ver = 1;
	dth->dth_dte.dte_refs = 1;
	dth->dth_dte.dte_mbs = mbs;

	dth->dth_coh = coh;
	dth->dth_epoch = epoch;
	dth->dth_leader_oid = *oid;

	dth->dth_flags = DTE_LEADER;
	dth->dth_modification_cnt = 1;

	dth->dth_op_seq = 1;
	dth->dth_dkey_hash = dkey_hash;

	D_INIT_LIST_HEAD(&dth->dth_share_cmt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_abt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_act_list);
	D_INIT_LIST_HEAD(&dth->dth_share_tbd_list);
	dth->dth_shares_inited = 1;

	vos_dtx_rsrvd_init(dth);

	*dthp = dth;
}

static void
dvt_dtx_end(struct dtx_handle *dth)
{
	D_FREE(dth->dth_dte.dte_mbs);
	D_FREE_PTR(dth);
}

void
dvt_vos_insert_2_records_with_dtx(daos_handle_t coh)
{
	struct dtx_handle	*dth1;
	struct dtx_handle	*dth2;
	const uint32_t		 recxs_nr = 1;
	const uint32_t		 rec_size = 1;
	daos_recx_t		 recxs[recxs_nr];
	daos_iod_t		 iod = {0};
	d_sg_list_t		 sgl = {0};
	daos_epoch_t		 epoch = 1;

	d_sgl_init(&sgl, 1);

	recxs[0].rx_idx = 0;
	recxs[0].rx_nr = daos_sgl_buf_size(&sgl);

	iod.iod_recxs = recxs;
	iod.iod_nr = recxs_nr;
	iod.iod_size = rec_size;
	iod.iod_type = DAOS_IOD_ARRAY;
	dvt_iov_alloc_str(&iod.iod_name, "akey");

	dvt_dtx_begin_helper(coh, &g_oids[0], epoch++, 0x123, &dth1);
	dvt_dtx_begin_helper(coh, &g_oids[0], epoch++, 0x124, &dth2);
	assert_success(vos_obj_update_ex(coh, g_oids[0], epoch, 0, 0, &g_dkeys[0], 1, &iod,
					 NULL, &sgl, dth1));
	assert_success(vos_obj_update_ex(coh, g_oids[1], epoch, 0, 0, &g_dkeys[1], 1, &iod,
					 NULL, &sgl, dth2));
	/* Only commit 1 of the  transactions */
	assert_int_equal(1, vos_dtx_commit(coh, &dth1->dth_xid, 1, NULL));

	dvt_dtx_end(dth1);
	dvt_dtx_end(dth2);
	daos_iov_free(&iod.iod_name);
	d_sgl_fini(&sgl, false);
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
	daos_handle_t		coh;
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

	assert_success(vos_cont_open(poh, g_uuids[0], &coh));
	dvt_vos_insert_2_records_with_dtx(coh);
	vos_cont_close(coh);

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

static bool
char_in_tests(char a, char *str, uint32_t str_len)
{
	int i;

	if (strlen(str) == 0) /* if there is no filter, always return true */
		return true;
	for (i = 0; i < str_len; i++) {
		if (a == str[i])
			return true;
	}

	return false;
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
		rc = create_test_vos_file();
		goto done;
	}

#define RUN_TEST_SUIT(c, func)\
	do {if (char_in_tests(c, test_suites, ARRAY_SIZE(test_suites))) \
		rc += func(); } while (0)

		/* filtering suites and tests */
		char test_suites[] = "";
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
		cmocka_set_test_filter("**");
#endif
		RUN_TEST_SUIT('a', ddb_parse_tests_run);
		RUN_TEST_SUIT('b', ddb_cmd_options_tests_run);
		RUN_TEST_SUIT('c', dv_tests_run);
		RUN_TEST_SUIT('d', dvc_tests_run);
		RUN_TEST_SUIT('e', ddb_main_tests);
		RUN_TEST_SUIT('f', ddb_commands_print_tests_run);

done:
	vos_self_fini();
	ddb_fini();
	return rc;
}
