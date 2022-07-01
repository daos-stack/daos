/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_QAT_H
#define __DAOS_QAT_H

#include <gurt/types.h>
#include <cpa.h>
#include <cpa_dc.h>
#include <icp_sal_user.h>
#include <icp_sal_poll.h>
#include <qae_mem.h>

enum QAT_COMPRESS_DIR {
	DIR_COMPRESS   = 0,
	DIR_DECOMPRESS = 1,
};

/** Common Functions */
int
qat_dc_is_available();

/** Compression Functions */
int
qat_dc_init(CpaInstanceHandle *dcInstHandle, CpaDcSessionHandle *sessionHdl,
	    CpaInstanceInfo2 *inst_info, Cpa16U *numInterBuffLists,
	    CpaBufferList ***bufferInterArrayPtr, Cpa32U maxBufferSize, CpaDcCompLvl compLvl);

int
qat_dc_compress(CpaInstanceHandle *dcInstHandle, CpaDcSessionHandle *sessionHdl,
		CpaInstanceInfo2 *inst_info, uint8_t *src, size_t srcLen, uint8_t *dst,
		size_t dstLen, size_t *produced, enum QAT_COMPRESS_DIR dir);

int
qat_dc_compress_async(CpaInstanceHandle *dcInstHandle, CpaDcSessionHandle *sessionHdl,
		      CpaInstanceInfo2 *inst_info, uint8_t *src, size_t srcLen, uint8_t *dst,
		      size_t dstLen, enum QAT_COMPRESS_DIR dir, dc_callback_fn user_cb_fn,
		      void *user_cb_data);

int
qat_dc_poll_response(CpaInstanceHandle *dcInstHandle);

int
qat_dc_destroy(CpaInstanceHandle *dcInstHandle, CpaDcSessionHandle *sessionHdl,
	       CpaBufferList **interBufs, Cpa16U numInterBuffLists);

CpaStatus
qaeMemInit(void);

void
qaeMemDestroy(void);

#endif /** __DAOS_QAT_H */
