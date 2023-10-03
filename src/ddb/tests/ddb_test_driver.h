/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef DAOS_DDB_TEST_DRIVER_H
#define DAOS_DDB_TEST_DRIVER_H

extern bool		 g_verbose;
extern const char	*g_uuids_str[10];
extern const char	*g_invalid_uuid_str;
extern uuid_t		 g_uuids[10];
extern daos_unit_oid_t	 g_oids[10];
extern daos_unit_oid_t	 g_invalid_oid;
extern char		*g_dkeys_str[10];
extern char		*g_akeys_str[10];
extern daos_key_t	 g_dkeys[10];
extern daos_key_t	 g_akeys[10];
extern daos_key_t	 g_invalid_key;
extern daos_recx_t	 g_recxs[10];
extern daos_recx_t	 g_invalid_recx;

struct dt_vos_pool_ctx {
	daos_handle_t	dvt_poh;
	uuid_t		dvt_pool_uuid;
	int		dvt_fd;
	char		dvt_pmem_file[128];
	uint32_t	dvt_cont_count;
	uint32_t	dvt_obj_count;
	uint32_t	dvt_dkey_count;
	uint32_t	dvt_akey_count;
};

daos_unit_oid_t dvt_gen_uoid(uint32_t i);
void dvt_vos_insert_recx(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
			 daos_recx_t *recx, daos_epoch_t epoch);
void
dvt_vos_insert_single(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		      char *data_str, daos_epoch_t epoch);

void dvt_iov_alloc(d_iov_t *iov, size_t len);
void dvt_iov_alloc_str(d_iov_t *iov, const char *str);


int ddb_test_setup_vos(void **state);
int ddb_teardown_vos(void **state);

int ddb_parse_tests_run(void);
int ddb_vos_tests_run(void);
int ddb_commands_tests_run(void);
int ddb_main_tests_run(void);
int ddb_cmd_options_tests_run(void);
int ddb_commands_print_tests_run(void);
int ddb_path_tests_run(void);

/*
 * Insert data into the pool. The cont, objs, ... parameters indicate how many of each to
 * insert into its parent. If numbers are 0, then it will use a default number.
 */
void dvt_insert_data(daos_handle_t poh, uint32_t conts, uint32_t objs, uint32_t dkeys,
		     uint32_t akeys, struct dt_vos_pool_ctx *tctx);

int ddb_test_pool_setup(struct dt_vos_pool_ctx *tctx);

extern uint32_t dvt_fake_print_called;
extern bool dvt_fake_print_just_count;
#define DVT_FAKE_PRINT_BUFFER_SIZE (1024)
extern char dvt_fake_print_buffer[DVT_FAKE_PRINT_BUFFER_SIZE];
int dvt_fake_print(const char *fmt, ...);
void dvt_fake_print_reset(void);
#define assert_printed_exact(str) assert_string_equal(str, dvt_fake_print_buffer)
#define assert_printed_not_equal(str) assert_string_not_equal(str, dvt_fake_print_buffer)
#define assert_printed_contains(str) assert_string_contains(dvt_fake_print_buffer, str)


extern size_t dvt_fake_get_file_size_result;
size_t dvt_fake_get_file_size(const char *path);

extern bool dvt_fake_get_file_exists_result;
bool dvt_fake_get_file_exists(const char *path);

extern uint32_t dvt_fake_read_file_called;
extern size_t dvt_fake_read_file_result;
extern char dvt_fake_read_file_buf[64];
size_t dvt_fake_read_file(const char *src_path, d_iov_t *contents);

void dvt_vos_insert_2_records_with_dtx(daos_handle_t coh);
void dvt_vos_insert_dtx_records(daos_handle_t coh, uint32_t nr, uint32_t committed_nr);

#endif /* DAOS_DDB_TEST_DRIVER_H */
