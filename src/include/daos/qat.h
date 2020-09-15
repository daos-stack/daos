/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
#ifndef __DAOS_QAT_H
#define __DAOS_QAT_H

#include "cpa.h"
#include "cpa_cy_im.h"
#include "cpa_cy_sym.h"

/** Hash Functions */
CpaStatus qat_hash_init(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx,
						            CpaCySymHashAlgorithm hashAlg, 
                                    Cpa32U digestResultLenInBytes);
CpaStatus qat_hash_update(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx, 
                                    uint8_t *buf, 
                                    size_t bufLen, 
                                    uint8_t *csumBuf,
									size_t csumLen,
                                    bool packetTypePartial);
CpaStatus qat_hash_finish(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx, 
                                    uint8_t *csumBuf, size_t csumLen);

CpaStatus qat_hash_destroy(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx);

/** Encryption Functions */

/** Compression Functions */

#endif /** __DAOS_QAT_H */
