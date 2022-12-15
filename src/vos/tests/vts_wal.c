/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_wal.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vos_internal.h>
#include "vts_io.h"

struct wal_test_args {
	char	*wta_clone;
	void	*wta_buf;
	int	 wta_buf_sz;
};

static int
teardown_wal_test(void **state)
{
	struct wal_test_args	*arg = *state;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup issue\n");
		return 0;
	}

	unlink(arg->wta_clone);
	D_FREE(arg->wta_clone);
	D_FREE(arg->wta_buf);
	D_FREE(arg);
	return 0;
}

static int
setup_wal_test(void **state)
{
	struct wal_test_args	*arg = NULL;
	char			*pool_name;
	int			 rc;

	D_ALLOC(arg, sizeof(struct wal_test_args));
	if (arg == NULL)
		return -1;

	arg->wta_buf_sz = (32UL << 20);	/* 32MB */
	D_ALLOC(arg->wta_buf, arg->wta_buf_sz);
	if (arg->wta_buf == NULL)
		goto error;

	D_ASPRINTF(arg->wta_clone, "%s/pool_clone", vos_path);
	if (arg->wta_clone == NULL)
		goto error;

	rc = vts_pool_fallocate(&pool_name);
	if (rc)
		goto error;

	rc = rename(pool_name, arg->wta_clone);
	if (rc) {
		unlink(pool_name);
		free(pool_name);
		goto error;
	}
	free(pool_name);

	*state = arg;
	return 0;
error:
	D_FREE(arg->wta_clone);
	D_FREE(arg->wta_buf);
	D_FREE(arg);
	return -1;
}

static int
copy_pool_file(struct wal_test_args *arg, const char *src_pool, const char *dst_pool)
{
	struct stat	lstat;
	uint64_t	copy_sz, left;
	int		src_fd, dst_fd, rc;

	rc = stat(src_pool, &lstat);
	if (rc != 0) {
		D_ERROR("Stat source pool:%s failed. %s\n", src_pool, strerror(errno));
		return -1;
	}

	src_fd = open(src_pool, O_RDONLY);
	if (src_fd < 0) {
		D_ERROR("Open source pool:%s failed. %s\n", src_pool, strerror(errno));
		return -1;
	}

	dst_fd = open(dst_pool, O_WRONLY);
	if (dst_fd < 0) {
		D_ERROR("Open dest pool:%s failed. %s\n", dst_pool, strerror(errno));
		close(src_fd);
		return -1;
	}

	left = lstat.st_size;
	while (left > 0) {
		ssize_t	read_sz;

		copy_sz = min(left, arg->wta_buf_sz);

		read_sz = read(src_fd, arg->wta_buf, copy_sz);
		if (read_sz < copy_sz) {
			D_ERROR("Failed to read "DF_U64" bytes from source pool:%s. %s\n",
				copy_sz, src_pool, strerror(errno));
			rc = -1;
			break;
		}

		read_sz = write(dst_fd, arg->wta_buf, copy_sz);
		if (read_sz < copy_sz) {
			D_ERROR("Failed to write "DF_U64" bytes to dest pool:%s. %s\n",
				copy_sz, dst_pool, strerror(errno));
			rc = -1;
			break;
		}
		left -= copy_sz;
	}

	close(src_fd);
	close(dst_fd);
	return rc;
}

static inline int
save_pool(struct wal_test_args *arg, const char *pool_name)
{
	return copy_pool_file(arg, pool_name, arg->wta_clone);
}

static inline int
restore_pool(struct wal_test_args *arg, const char *pool_name)
{
	return copy_pool_file(arg, arg->wta_clone, pool_name);
}

static inline void
skip_wal_test(void)
{
	unsigned int	val = 0;

	d_getenv_int("DAOS_MD_ON_SSD", &val);
	if (val == 0) {
		print_message("MD_ON_SSD isn't enabled, skip test\n");
		skip();
	}
	/* FIXME Enable WAL test when meta load & WAL replay is integrated in umem */
	print_message("WAL replay isn't integrated in umem, skip test\n");
	skip();
}

/* Create pool, clear content in tmpfs, open pool by meta blob loading & WAL replay */
static void
wal_tst_01(void **state)
{
	struct wal_test_args	*arg = *state;
	char			*pool_name;
	uuid_t			 pool_id;
	daos_handle_t		 poh;
	int			 rc;

	skip_wal_test();

	uuid_generate(pool_id);

	/* Create VOS pool file */
	rc = vts_pool_fallocate(&pool_name);
	assert_int_equal(rc, 0);

	/* Save the empty pool file */
	rc = save_pool(arg, pool_name);
	assert_int_equal(rc, 0);

	/* Create pool: Create meta & WAL blobs, write meta & WAL header */
	rc = vos_pool_create(pool_name, pool_id, 0, 0, 0, NULL);
	assert_int_equal(rc, 0);

	/* Restore pool content from the empty clone */
	rc = restore_pool(arg, pool_name);
	assert_int_equal(rc, 0);

	/* Open pool: Open meta & WAL blobs, load meta & WAL header, replay WAL */
	rc = vos_pool_open(pool_name, pool_id, 0, &poh);
	assert_int_equal(rc, 0);

	/* Close pool: Flush meta & WAL header, close meta & WAL blobs */
	rc = vos_pool_close(poh);
	assert_int_equal(rc, 0);

	/* Destroy pool: Destroy meta & WAL blobs */
	rc = vos_pool_destroy(pool_name, pool_id);
	assert_int_equal(rc, 0);

	free(pool_name);
}

static const struct CMUnitTest wal_tests[] = {
	{ "WAL01: Basic pool operations",
	  wal_tst_01, NULL, NULL },
};

int
run_wal_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "WAL Tests %s", cfg);
	return cmocka_run_group_tests_name(test_name, wal_tests, setup_wal_test,
					   teardown_wal_test);
}
