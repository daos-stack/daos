/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef DAOS_DDB_TEST_DRIVER_H
#define DAOS_DDB_TEST_DRIVER_H

extern bool		 g_verbose;
extern const char	*g_uuids_str[10];
extern uuid_t		 g_uuids[10];
extern daos_unit_oid_t	 g_oids[10];
extern char		*g_dkeys_str[10];
extern char		*g_akeys_str[10];
extern daos_key_t	 g_dkeys[10];
extern daos_key_t	 g_akeys[10];

struct dt_vos_pool_ctx {
	daos_handle_t	dvt_poh;
	uuid_t		dvt_pool_uuid;
	int		dvt_fd;
	char		dvt_pmem_file[128];
};

daos_unit_oid_t gen_uoid(uint32_t lo);

void dvt_vos_insert_recx(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
			 int recx_idx, char *data_str, daos_epoch_t epoch);
void
dvt_vos_insert_single(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		      char *data_str, daos_epoch_t epoch);

void dvt_iov_alloc(d_iov_t *iov, size_t len);
void dvt_iov_alloc_str(d_iov_t *iov, const char *str);


int ddb_test_setup_vos(void **state);
int ddb_teardown_vos(void **state);

int ddb_parse_tests_run(void);
int dv_tests_run(void);
int dvc_tests_run(void);
int ddb_main_tests(void);
int ddb_cmd_options_tests_run(void);

/*
 * Insert data into the pool. The cont, objs, ... parameters indicate how many of each to
 * insert into its parent. If numbers are 0, then it will use a default number.
 */
void dvt_insert_data(daos_handle_t poh, uint32_t conts, uint32_t objs, uint32_t dkeys,
		     uint32_t akeys);
void dvt_delete_all_containers(daos_handle_t poh);

#endif /* DAOS_DDB_TEST_DRIVER_H */
