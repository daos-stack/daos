/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef DAOS_DDB_TEST_DRIVER_H
#define DAOS_DDB_TEST_DRIVER_H

#include "ddb_cmocka.h"
#include "ddb_vos.h"
#include <daos/tests_lib.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/vos.h>
#include <ddb_common.h>
#include <fcntl.h>
#include <gurt/debug.h>
#include <libgen.h>
#include <sys/stat.h>

extern bool            g_verbose;
extern const char     *g_uuids_str[10];
extern const char     *g_invalid_uuid_str;
extern uuid_t          g_uuids[10];
extern daos_unit_oid_t g_oids[10];
extern daos_unit_oid_t g_invalid_oid;
extern char           *g_dkeys_str[10];
extern char           *g_akeys_str[10];
extern daos_key_t      g_dkeys[10];
extern daos_key_t      g_akeys[10];
extern daos_key_t      g_invalid_key;
extern daos_recx_t     g_recxs[10];
extern daos_recx_t     g_invalid_recx;
extern const char     *g_csum_uuid_str;

struct dt_vos_pool_ctx {
	daos_handle_t dvt_poh;
	uuid_t        dvt_pool_uuid;
	int           dvt_fd;
	char          dvt_pmem_file[128];
	uint32_t      dvt_cont_count;
	uint32_t      dvt_obj_count;
	uint32_t      dvt_dkey_count;
	uint32_t      dvt_akey_count;
	bool          dvt_special_pool_destroy;
	void         *dvt_extra;
};

#define DVT_FAKE_SV_COUNT   (2)
#define DVT_FAKE_RECX_COUNT (2)
#define DVT_FAKE_SV_SIZE    (1u << 10)
#define DVT_FAKE_RECX_SIZE  (1u << 13)
#define DVT_FAKE_CHUNK_SIZE (1u << 12)
#define DVT_FAKE_CSUM_TYPE    (HASH_TYPE_CRC64)
#define DVT_FAKE_RECX_BAD_IDX (1)

/* Checksum fixture objects: SV under g_akeys_str[0] and recxs A=[0, S)@1, B=[S/2, 3S/2)@2 under
 * g_akeys_str[1].  The *_FILLER constants are the data byte of the first version/extent, the next
 * ones use the following bytes. */
#define DVT_FAKE_CSUM_OID_NONE               (0) /* no checksum stored, SV@1 only */
#define DVT_FAKE_CSUM_OID_NONE_SV_FILLER     ('a')
#define DVT_FAKE_CSUM_OID_NONE_RECX_FILLER   ('c')
#define DVT_FAKE_CSUM_OID_VALID              (1) /* valid checksums, SV@1 and SV@2 */
#define DVT_FAKE_CSUM_OID_VALID_SV_FILLER    ('b')
#define DVT_FAKE_CSUM_OID_VALID_RECX_FILLER  ('e')
#define DVT_FAKE_CSUM_OID_BAD                (2) /* corrupted SV@1 and recx DVT_FAKE_RECX_BAD_IDX */
#define DVT_FAKE_CSUM_OID_BAD_SV_FILLER      ('d')
#define DVT_FAKE_CSUM_OID_BAD_RECX_FILLER    ('f')

/* Partially overwritten recx fixture objects: A=[0, S)@1, B=[0, S/2)@2 */
#define DVT_FAKE_PART_RECX_COUNT             (2)
#define DVT_FAKE_PART_RECX_A                 (0)
#define DVT_FAKE_PART_RECX_B                 (1)
#define DVT_FAKE_PART_OID_COUNT              (3)
#define DVT_FAKE_PART_OID_VALID              (3) /* not corrupted */
#define DVT_FAKE_PART_OID_VALID_FILLER       ('h')
#define DVT_FAKE_PART_OID_BAD_VISIBLE        (4) /* chunk A=[S/2, S) corrupted */
#define DVT_FAKE_PART_OID_BAD_VISIBLE_FILLER ('j')
#define DVT_FAKE_PART_OID_BAD_HIDDEN         (5) /* chunk A=[0, S/2) corrupted */
#define DVT_FAKE_PART_OID_BAD_HIDDEN_FILLER  ('l')
/* Row of dct_part_ics holding the checksum infos of the object g_oids[oid] */
#define DVT_FAKE_PART_ICS_IDX(oid)           ((oid) - DVT_FAKE_PART_OID_VALID)
D_CASSERT(DVT_FAKE_PART_ICS_IDX(DVT_FAKE_PART_OID_BAD_VISIBLE) == 1);
D_CASSERT(DVT_FAKE_PART_ICS_IDX(DVT_FAKE_PART_OID_BAD_HIDDEN) == DVT_FAKE_PART_OID_COUNT - 1);

struct dt_csum_ctx {
	uuid_t                dct_cont_uuid;
	size_t                dct_sv_size;
	size_t                dct_recx_size;
	enum DAOS_HASH_TYPE   dct_csum_type;
	size_t                dct_chunk_size;
	struct daos_csummer  *dct_csummer;
	struct dcs_iod_csums *dct_sv_ics[DVT_FAKE_SV_COUNT];
	struct dcs_iod_csums *dct_recx_ics[DVT_FAKE_RECX_COUNT];
	struct dcs_iod_csums *dct_sv_ic_bad;
	struct dcs_iod_csums *dct_recx_ics_bad[DVT_FAKE_RECX_COUNT];
	struct dcs_iod_csums *dct_part_ics[DVT_FAKE_PART_OID_COUNT][DVT_FAKE_PART_RECX_COUNT];
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

/* Requires tctx->dvt_poh to be a valid open pool handle (caller-owned). */
int
ddb_test_csum_setup(void **state);
/* Requires tctx->dvt_poh to be the same valid open pool handle as provided to
 * ddb_test_csum_setup(). */
void
    ddb_test_csum_teardown(void **state);

int ddb_parse_tests_run(void);
int ddb_vos_tests_run(void);
int
		ddb_commands_tests_run(void);
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
#define DVT_FAKE_PRINT_BUFFER_SIZE (0x20000)
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
