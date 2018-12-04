/**
 * (C) Copyright 2018 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>

#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos_srv/smd.h>
#include "../smd_internal.h"

static void
print_usage(void)
{
	print_message("Default <smd_ut> runs all tests\n");
	print_message("Options:\n");
	print_message("--file=<pool> | -f pfile (d: test_smd)\n");
	print_message("--size=<size> | -s (should be over 128M) (d: 256M)\n");
	print_message("--help	     | -h Print this message and exit.\n");
}

daos_size_t		size = -1;
char			*fname;
uuid_t			global_uuid;
int			global_stream_list[11];

static int
smd_ut_setup(void **state)
{
	int			rc;

	rc = daos_debug_init(NULL);
	if (rc) {
		print_error("Error initializing the debug instance\n");
		return rc;
	}

	rc = smd_create_initialize("/mnt/daos", fname, size);
	if (rc) {
		print_error("Error initializing SMD instance\n");
		D_GOTO(exit_0, rc);
	}
exit_0:
	daos_debug_fini();
	return 0;
}

static int
smd_ut_teardown(void **state)
{
	struct smd_ut_args	*args = *state;

	smd_remove("/mnt/daos", fname);
	smd_fini();
	daos_debug_fini();
	D_FREE(args);
	return 0;
}

static int
smd_ut_addfetch_setup(void **state)
{
	return 0;
}

static int
smd_ut_addfetch_teardown(void **state)
{
	return 0;
}

static int
smd_ut_listing_setup(void **state)
{
	int				i, rc = 0;
	int				stream;
	struct smd_nvme_stream_bond	bond;


	/** Adding 10 streams to the stream table */
	for (i = 0; i < 10; i++) {
		stream = (i + 1);
		smd_nvme_set_stream_bond(stream, global_uuid, &bond);
		rc = smd_nvme_add_stream_bond(&bond);
		if (rc) {
			print_error("Add stream bond failure\n");
			return rc;
		}
	}
	return 0;
}

static void
smd_ut_listing_test(void **state)
{
	int				i, rc = 0, stream;
	uint32_t			nr = 5;
	daos_anchor_t			anchor;
	struct smd_nvme_stream_bond	*bonds, streams[10];

	daos_anchor_set_zero(&anchor);
	bonds = &streams[0];
	rc = smd_nvme_list_streams(&nr, bonds, &anchor);
	assert_int_equal(rc, 0);
	assert_int_equal(nr, 5);
	for (i = 0; i < nr; i++) {
		stream = global_stream_list[i] = (i + 1);
		assert_int_equal(stream, streams[i].nsm_stream_id);
		rc = uuid_compare(global_uuid, streams[i].nsm_dev_id);
		assert_int_equal(rc, 0);
	}
	bonds = &streams[5];
	rc = smd_nvme_list_streams(&nr, bonds, &anchor);
	assert_int_equal(rc, 0);
	assert_int_equal(nr, 5);
	for (i = nr; i < nr*2; i++) {
		stream = global_stream_list[i] = (i + 1);
		assert_int_equal(stream, streams[i].nsm_stream_id);
		rc = uuid_compare(global_uuid, streams[i].nsm_dev_id);
		assert_int_equal(rc, 0);
	}

	memset(&streams[0], '0', (10 * sizeof(struct smd_nvme_stream_bond)));
	bonds = &streams[0];
	rc = smd_nvme_list_streams(&nr, bonds, &anchor);
	assert_int_equal(rc, 0);
	assert_int_equal(nr, 1);
	assert_int_equal(streams[0].nsm_stream_id, 2000);
	global_stream_list[10] = 2000;
	rc = uuid_compare(global_uuid, streams[0].nsm_dev_id);
	assert_int_equal(rc, 0);
}

static void
smd_ut_fetch_empty_ptab(void **state)
{
	int				rc;
	uuid_t				pool_uuid;
	int				stream;
	struct smd_nvme_pool_info	info;

	uuid_generate(pool_uuid);
	stream = 4;

	rc = smd_nvme_get_pool(pool_uuid, stream, &info);
	assert_int_equal(rc, -DER_NONEXIST);
}

static void
smd_ut_add_and_fetch_ptab(void **state)
{
	int				rc;
	uuid_t				pool_uuid;
	int				stream;
	uint64_t			blob_id = 32;
	struct smd_nvme_pool_info	info;

	uuid_generate(pool_uuid);
	stream = 4;

	smd_nvme_set_pool_info(pool_uuid, stream, blob_id, &info);
	rc = smd_nvme_add_pool(&info);
	assert_int_equal(rc, 0);
	memset(&info, '0', sizeof(info));

	rc = smd_nvme_get_pool(pool_uuid, stream, &info);
	if (rc) {
		print_error("NVME get pool failed :%d\n", rc);
		assert_int_equal(rc, 0);
	}

	rc = uuid_compare(pool_uuid, info.npi_pool_uuid);
	assert_int_equal(rc, 0);
	assert_true(info.npi_stream_id == stream);
	assert_true(info.npi_blob_id == blob_id);
}

static void
smd_ut_fetch_empty_devtab(void **state)
{
	int				rc	= 0;
	uuid_t				dev_uuid;
	struct smd_nvme_device_info	info;

	uuid_generate(dev_uuid);

	memset(&info, '0', sizeof(struct smd_nvme_device_info));
	rc = smd_nvme_get_device(dev_uuid, &info);
	assert_int_equal(rc, -DER_NONEXIST);
}

static void
smd_ut_fetch_devtab_streams(void **state)
{
	int				i, rc	= 0;
	struct smd_nvme_device_info	info;

	memset(&info, 0, sizeof(struct smd_nvme_device_info));
	rc = smd_nvme_get_device(global_uuid, &info);
	assert_int_equal(rc, 0);
	assert_int_equal(uuid_compare(info.ndi_dev_id, global_uuid), 0);
	assert_int_equal(info.ndi_xs_cnt, 11);
	for (i = 0; i < info.ndi_xs_cnt; i++) {
		print_message("streams[%d]: %d, gstreams[%d]: %d\n",
		       i, info.ndi_xstreams[i], i, global_stream_list[i]);
		assert_int_equal(info.ndi_xstreams[i], global_stream_list[i]);
	}
}


static void
smd_ut_add_and_fetch_devtab(void **state)
{
	int				rc	= 0;
	uuid_t				dev_uuid;
	struct smd_nvme_device_info	info;

	uuid_generate(dev_uuid);
	rc = smd_nvme_set_device_status(dev_uuid, SMD_NVME_NORMAL);
	assert_int_equal(rc, 0);

	memset(&info, 0, sizeof(struct smd_nvme_device_info));
	rc = smd_nvme_get_device(dev_uuid, &info);
	assert_int_equal(rc, 0);

	rc = uuid_compare(dev_uuid, info.ndi_dev_id);
	assert_int_equal(rc, 0);
	assert_int_equal(info.ndi_status, SMD_NVME_NORMAL);
	assert_int_equal(info.ndi_xs_cnt, 0);
}

static void
smd_ut_add_and_fetch_stab(void **state)
{
	int					rc	= 0;
	int					stream;
	struct smd_nvme_stream_bond		bond;

	stream = 2000;
	uuid_generate(global_uuid);

	smd_nvme_set_stream_bond(stream, global_uuid, &bond);
	rc = smd_nvme_add_stream_bond(&bond);
	assert_int_equal(rc, 0);

	memset(&bond, '0', sizeof(struct smd_nvme_stream_bond));
	rc = smd_nvme_get_stream_bond(stream, &bond);
	assert_int_equal(rc, 0);

	rc = uuid_compare(global_uuid, bond.nsm_dev_id);
	assert_int_equal(rc, 0);
	assert_int_equal(bond.nsm_stream_id, stream);
}

static void
smd_ut_fetch_empty_stab(void **state)
{
	int					rc	= 0;
	int					stream;
	struct smd_nvme_stream_bond		bond;

	stream = 14;

	memset(&bond, '0', sizeof(struct smd_nvme_stream_bond));
	rc = smd_nvme_get_stream_bond(stream, &bond);
	assert_int_equal(rc, -DER_NONEXIST);
}


static const struct CMUnitTest smd_uts[] = {
	{"SMD 1.0: Add and fetch Device table",
		smd_ut_add_and_fetch_devtab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 1.1: Device table empty fetch test",
		smd_ut_fetch_empty_devtab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 2.0: Add and fetch Pool table",
		smd_ut_add_and_fetch_ptab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 2.1: Pool table empty fetch test",
		smd_ut_fetch_empty_ptab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 3.0: Add and fetch Stream table",
		smd_ut_add_and_fetch_stab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 3.1: Stream table empty fetch test",
		smd_ut_fetch_empty_stab, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},
	{"SMD 3.2: Stream table listing test",
		smd_ut_listing_test, smd_ut_listing_setup,
		smd_ut_addfetch_teardown},
	{"SMD 4.1: Device table fetch test",
		smd_ut_fetch_devtab_streams, smd_ut_addfetch_setup,
		smd_ut_addfetch_teardown},

};

int main(int argc, char **argv)
{
	int	rc, nr_failed = 0;

	fname = NULL;
	static struct option long_ops[] = {
		{ "fname",	required_argument,	NULL,	'f' },
		{ "size",	required_argument,	NULL,	's' },
		{ "help",	no_argument,		NULL,	'h' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((rc = getopt_long(argc, argv,
				 "f:s:h", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'f':
			D_ALLOC(fname, strlen(optarg));
			strcpy(fname, optarg);
			break;
		case 's':
			size = atoi(optarg);
			break;
		case 'h':
			print_usage();
			goto exit_1;
		default:
			print_error("unknown option %c\n", rc);
			print_usage();
			rc = -1;
			goto exit_1;
		}
	}

	nr_failed = cmocka_run_group_tests_name("SMD unit tests", smd_uts,
						smd_ut_setup, smd_ut_teardown);
	if (nr_failed)
		print_error("ERROR, %i TEST(S) FAILED\n", nr_failed);
	else
		print_message("\nSUCCESS! NO TEST FAILURES\n");
exit_1:
	return 0;
}
