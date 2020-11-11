/**
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
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
#ifndef __DAOS_QAT_H
#define __DAOS_QAT_H

#include <gurt/types.h>
#include <cpa.h>
#include <cpa_dc.h>
#include <icp_sal_user.h>
#include <icp_sal_poll.h>
#include <qae_mem.h>

enum QAT_COMPRESS_DIR {
	DIR_COMPRESS	= 0,
	DIR_DECOMPRESS	= 1,
};

/** Common Functions */
int
qat_dc_is_available();

/** Compression Functions */
int
qat_dc_init(CpaInstanceHandle *dcInstHandle,
	    CpaDcSessionHandle *sessionHdl,
	    Cpa16U *numInterBuffLists,
	    CpaBufferList ***bufferInterArrayPtr,
	    Cpa32U maxBufferSize,
	    CpaDcCompLvl compLvl);

int
qat_dc_compress(CpaInstanceHandle *dcInstHandle,
		CpaDcSessionHandle *sessionHdl,
		uint8_t *src,
		size_t srcLen,
		uint8_t *dst,
		size_t dstLen,
		size_t *produced,
		enum QAT_COMPRESS_DIR dir);

int
qat_dc_destroy(CpaInstanceHandle *dcInstHandle,
	       CpaDcSessionHandle *sessionHdl,
	       CpaBufferList **interBufs,
	       Cpa16U numInterBuffLists);

CpaStatus
qaeMemInit(void);

void
qaeMemDestroy(void);

#endif /** __DAOS_QAT_H */
