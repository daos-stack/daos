/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_DAOS_MOCK_H__
#define __DLCK_UT_DAOS_MOCK_H__

#include <stdbool.h>
#include <stddef.h>

#include <daos/tls.h>
#include <daos_srv/bio.h>
#include <daos_srv/daos_engine.h>

#include "../dlck_args.h"

/*
 * Shared DAOS mocks: TLS, module keys, BIO, and VOS. Link dlck_ut_daos_mock.c and enable the
 * TLS wraps (-Wl,--wrap=dss_tls_*, -Wl,--wrap=ds_tls_key_*, -Wl,--wrap=daos_*_key) for the calls
 * controlled by the test binary. The BIO and VOS mocks are plain definitions of symbols not
 * provided by the linked libraries.
 */

/** Sentinel NVMe context installed and checked by the BIO mocks. */
extern struct bio_xs_context *const     Mock_xs_ctxt;

/** Thread-local storage shared by all mocked execution streams. */
extern struct daos_thread_local_storage Mock_dtls;
extern struct dss_module_info           Mock_dmi;

/** Module data normally provided by dlck_tls.c and the VOS server module. */
extern struct dss_module_key            daos_srv_modkey;
extern struct dss_module_key            vos_module_key;
extern struct dss_module                vos_srv_module;

/** TLS primitives */

#define EXPECT_TLS_INIT(TAG, XS_ID, TGT_ID, DTLS)                                                  \
	do {                                                                                       \
		expect_value(__wrap_dss_tls_init, tag, TAG);                                       \
		expect_value(__wrap_dss_tls_init, xs_id, XS_ID);                                   \
		expect_value(__wrap_dss_tls_init, tgt_id, TGT_ID);                                 \
		will_return(__wrap_dss_tls_init, DTLS);                                            \
	} while (0)

/** queue a complete dss_get_module_info() lookup resolving to Mock_dmi */
#define EXPECT_GET_DMI()                                                                           \
	do {                                                                                       \
		will_return(__wrap_dss_tls_get, &Mock_dtls);                                       \
		expect_value(__wrap_daos_get_module_key, index, daos_srv_modkey.dmk_index);        \
		will_return(__wrap_daos_get_module_key, &daos_srv_modkey);                         \
	} while (0)

/** BIO primitives */

#define EXPECT_BIO_NVME_INIT(ARGS, RC)                                                             \
	do {                                                                                       \
		expect_string(bio_nvme_init, nvme_conf, (ARGS)->nvme_conf);                        \
		expect_value(bio_nvme_init, numa_node, (ARGS)->numa_node);                         \
		expect_value(bio_nvme_init, mem_size, (ARGS)->max_dma_buf_size);                   \
		expect_value(bio_nvme_init, hugepage_size, (ARGS)->nvme_hugepage_size);            \
		expect_value(bio_nvme_init, tgt_nr, (ARGS)->targets);                              \
		will_return(bio_nvme_init, RC);                                                    \
	} while (0)

#define EXPECT_BIO_NVME_CONFIGURED(CONFIGURED)                                                     \
	do {                                                                                       \
		expect_value(bio_nvme_configured, type, SMD_DEV_TYPE_META);                        \
		will_return(bio_nvme_configured, CONFIGURED);                                      \
	} while (0)

#define EXPECT_BIO_NVME_POLL(CTXT, RC)                                                             \
	do {                                                                                       \
		expect_value(bio_nvme_poll, ctxt, CTXT);                                           \
		will_return(bio_nvme_poll, RC);                                                    \
	} while (0)

#define EXPECT_BIO_XSCTXT_ALLOC(TGT_ID, RC, CTXT)                                                  \
	do {                                                                                       \
		expect_value(bio_xsctxt_alloc, tgt_id, TGT_ID);                                    \
		will_return(bio_xsctxt_alloc, RC);                                                 \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(bio_xsctxt_alloc, CTXT);                                       \
		}                                                                                  \
	} while (0)

/** VOS and engine primitives */

#define EXPECT_VOS_TLS_INIT(TAGS, RC)                                                              \
	do {                                                                                       \
		expect_value(vos_standalone_tls_init, tags, TAGS);                                 \
		will_return(vos_standalone_tls_init, RC);                                          \
	} while (0)

#define EXPECT_VOS_SYS_DB_INIT(ARGS, STORAGE_PATH, RC)                                             \
	do {                                                                                       \
		expect_string(vos_sys_db_init, nvme_conf, (ARGS)->nvme_conf);                      \
		expect_string(vos_sys_db_init, storage_path, STORAGE_PATH);                        \
		will_return(vos_sys_db_init, RC);                                                  \
	} while (0)

/** int sm_init(void) mock of vos_srv_module.sm_init */
int
mock_sm_init(void);

/** int sm_fini(void) mock of vos_srv_module.sm_fini */
int
mock_sm_fini(void);

#endif /* __DLCK_UT_DAOS_MOCK_H__ */
