/**
 * (C) Copyright 2020 Intel Corporation.
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
#define D_LOGFAC    DD_FAC(csum)

#ifdef HAVE_QAT

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include <daos/common.h>
#include <gurt/types.h>

#include <cpa.h>
#include <cpa_cy_im.h>
#include <cpa_cy_sym.h>
#include <icp_sal_user.h>
#include <icp_sal_poll.h>
#include <qae_mem.h>

#define MAX_INSTANCES 32

/* Memory */
#define PHYS_CONTIG_ALLOC(ppMemAddr, sizeBytes)                                \
    Mem_Alloc_Contig((void *)(ppMemAddr), (sizeBytes), 1)

#define PHYS_CONTIG_ALLOC_ALIGNED(ppMemAddr, sizeBytes, alignment)             \
    Mem_Alloc_Contig((void *)(ppMemAddr), (sizeBytes), (alignment))

#define PHYS_CONTIG_FREE(pMemAddr) Mem_Free_Contig((void *)&pMemAddr)

extern CpaStatus qaeMemInit(void);
extern void qaeMemDestroy(void);

static __inline CpaStatus 
Mem_Alloc_Contig(void **ppMemAddr,
                 Cpa32U sizeBytes,
                 Cpa32U alignment)
{
    *ppMemAddr = qaeMemAllocNUMA(sizeBytes, 0, alignment);
    if (NULL == *ppMemAddr)
    {
        return CPA_STATUS_RESOURCE;
    }
    return CPA_STATUS_SUCCESS;
}

static __inline void 
Mem_Free_Contig(void **ppMemAddr)
{
    if (NULL != *ppMemAddr)
    {
        qaeMemFreeNUMA(ppMemAddr);
        *ppMemAddr = NULL;
    }
}

static __inline CpaStatus 
OS_SLEEP(Cpa32U ms)
{
    int ret = 0;
    struct timespec resTime, remTime;
    resTime.tv_sec = ms / 1000;
    resTime.tv_nsec = (ms % 1000) * 1000000;
    do
    {
        ret = nanosleep(&resTime, &remTime);
        resTime = remTime;
    } while ((ret != 0) && (errno == EINTR));

    if (ret != 0)
    {
        return CPA_STATUS_FAIL;
    }
    else
    {
        return CPA_STATUS_SUCCESS;
    }
}

static __inline CpaPhysicalAddr 
virtToPhys(void *virtAddr)
{
    return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virtAddr);
}

static void 
getInstance(CpaInstanceHandle *pCyInstHandle)
{
    CpaInstanceHandle cyInstHandles[MAX_INSTANCES];
    Cpa16U numInstances = 0;
    CpaStatus status = CPA_STATUS_SUCCESS;

    *pCyInstHandle = NULL;
    status = cpaCyGetNumInstances(&numInstances);

    if (numInstances >= MAX_INSTANCES)
        numInstances = MAX_INSTANCES;

    if ((status == CPA_STATUS_SUCCESS) && (numInstances > 0))
    {
        status = cpaCyGetInstances(numInstances, cyInstHandles);
        if (status == CPA_STATUS_SUCCESS)
            *pCyInstHandle = cyInstHandles[0];
    }

    if (0 == numInstances)
        D_DEBUG(DB_TRACE, "No QAT instances found.\n");
}

static void 
symCallback(void *pCallbackTag,
                        CpaStatus status,
                        const CpaCySymOp operationType,
                        void *pOpData,
                        CpaBufferList *pDstBuffer,
                        CpaBoolean verifyResult)
{
    if (NULL != pCallbackTag)
    {
       *(Cpa32U*)pCallbackTag = 1;
    }
}

CpaStatus 
qat_hash_init(CpaInstanceHandle *cyInstHandle, 
                        CpaCySymSessionCtx *sessionCtx,
                        CpaCySymHashAlgorithm hashAlg, 
                        Cpa32U digestResultLenInBytes)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa32U sessionCtxSize = 0;
    CpaCySymSessionSetupData sessionSetupData = {0};

    status = qaeMemInit();
    if (CPA_STATUS_SUCCESS != status)
    {
        D_DEBUG(DB_TRACE, "Failed to initialize memory driver\n");
        return 0;
    }

    status = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);
    if (CPA_STATUS_SUCCESS != status)
    {
        D_DEBUG(DB_TRACE, "Failed to start user process SSL\n");
        qaeMemDestroy();
        return 0;
    }
    
    getInstance(cyInstHandle);
    if (*cyInstHandle == NULL)
    {
        return CPA_STATUS_FAIL;
    }

    status = cpaCyStartInstance(*cyInstHandle);

    if (CPA_STATUS_SUCCESS == status)
    {
        /*
         * Set the address translation function for the instance
         */
        status = cpaCySetAddressTranslation(*cyInstHandle, virtToPhys);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /*
         * We now populate the fields of the session operational data and create
         * the session.  Note that the size required to store a session is
         * implementation-dependent, so we query the API first to determine how
         * much memory to allocate, and then allocate that memory.
         */
        sessionSetupData.sessionPriority = CPA_CY_PRIORITY_NORMAL;
        sessionSetupData.symOperation = CPA_CY_SYM_OP_HASH;
        sessionSetupData.hashSetupData.hashAlgorithm = hashAlg;
        sessionSetupData.hashSetupData.hashMode = CPA_CY_SYM_HASH_MODE_PLAIN;
        sessionSetupData.hashSetupData.digestResultLenInBytes = digestResultLenInBytes;
        sessionSetupData.digestIsAppended = CPA_FALSE;
        sessionSetupData.verifyDigest = CPA_FALSE;

        status = cpaCySymSessionCtxGetSize(
            *cyInstHandle, &sessionSetupData, &sessionCtxSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Allocate session context */
        status = PHYS_CONTIG_ALLOC(sessionCtx, sessionCtxSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = cpaCySymInitSession(
            *cyInstHandle, symCallback, &sessionSetupData, *sessionCtx);
    }
    return status;
}

CpaStatus 
qat_hash_update(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx, 
                                    uint8_t *buf, 
                                    size_t bufLen, 
                                    uint8_t *csumBuf,
                                    size_t csumLen,
                                    bool packetTypePartial)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa8U *pBufferMeta = NULL;
    Cpa32U bufferMetaSize = 0;
    CpaBufferList *pBufferList = NULL;
    CpaFlatBuffer *pFlatBuffer = NULL;
    CpaCySymOpData *pOpData = NULL;
    Cpa32U bufferSize = bufLen;
    Cpa32U numBuffers = 1;
    Cpa32U bufferListMemSize =
        (sizeof(CpaBufferList) + (numBuffers * sizeof(CpaFlatBuffer)));
    Cpa8U *pSrcBuffer = NULL;
    Cpa8U *pDigestBuffer = NULL;
    Cpa32U complete = 0;

    /* The following variables are allocated on the stack because we block
     * until the callback comes back. If a non-blocking approach was to be
     * used then these variables should be dynamically allocated */
    status =
        cpaCyBufferListGetMetaSize(*cyInstHandle, numBuffers, &bufferMetaSize);

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pBufferMeta, bufferMetaSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        D_ALLOC(pBufferList, bufferListMemSize);
        status = pBufferList ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pSrcBuffer, bufferSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pDigestBuffer, csumLen);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /* increment by sizeof(CpaBufferList) to get at the
         * array of flatbuffers */
        pFlatBuffer = (CpaFlatBuffer *)(pBufferList + 1);

        pBufferList->pBuffers = pFlatBuffer;
        pBufferList->numBuffers = 1;
        pBufferList->pPrivateMetaData = pBufferMeta;

        pFlatBuffer->dataLenInBytes = bufferSize;
        pFlatBuffer->pData = pSrcBuffer;

        D_ALLOC(pOpData, sizeof(CpaCySymOpData));
        status = pOpData ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        memcpy(pSrcBuffer, buf, bufLen);

        pOpData->packetType = packetTypePartial ? 
                CPA_CY_SYM_PACKET_TYPE_PARTIAL : CPA_CY_SYM_PACKET_TYPE_FULL;
        pOpData->sessionCtx = *sessionCtx;
        pOpData->hashStartSrcOffsetInBytes = 0;
        pOpData->messageLenToHashInBytes = bufferSize;
        pOpData->pDigestResult = pDigestBuffer;

        status = cpaCySymPerformOp(
            *cyInstHandle,
            (void *)&complete, /* data sent as is to the callback function*/
            pOpData,           /* operational data struct */
            pBufferList,       /* source buffer list */
            pBufferList,       /* same src & dst for an in-place operation*/
            NULL);

        if (CPA_STATUS_SUCCESS != status)
        {
            D_DEBUG(DB_TRACE, "cpaCySymPerformOp failed. (status = %d)\n", status);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            /** wait until the completion of the operation*/
            do {
                icp_sal_CyPollInstance(*cyInstHandle, 0);
                /** Busy polling vs. sleep and polling */
                // OS_SLEEP(1);
            } while (!complete);
        } 

        if (!packetTypePartial)
            memcpy(csumBuf, pDigestBuffer, csumLen);
    }

    PHYS_CONTIG_FREE(pSrcBuffer);
    D_FREE(pBufferList);
    PHYS_CONTIG_FREE(pBufferMeta);
    PHYS_CONTIG_FREE(pDigestBuffer);
    D_FREE(pOpData);

    return status;
}

CpaStatus 
qat_hash_finish(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx, 
                                    uint8_t *csumBuf, size_t csumLen)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa8U *pBufferMeta = NULL;
    Cpa32U bufferMetaSize = 0;
    CpaBufferList *pBufferList = NULL;
    CpaCySymOpData *pOpData = NULL;
    Cpa32U bufferListMemSize =
        sizeof(CpaBufferList) +  sizeof(CpaFlatBuffer);
    Cpa8U *pDigestBuffer = NULL;
    Cpa32U complete = 0;

    status =
        cpaCyBufferListGetMetaSize(*cyInstHandle, 1, &bufferMetaSize);

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pBufferMeta, bufferMetaSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {        
        D_ALLOC(pBufferList, bufferListMemSize);        
        status = pBufferList ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pDigestBuffer, csumLen);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        pBufferList->numBuffers = 0;
        pBufferList->pPrivateMetaData = pBufferMeta;
        D_ALLOC(pOpData, sizeof(CpaCySymOpData));        
        status = pOpData ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Set packet type to final */ 
        pOpData->packetType = CPA_CY_SYM_PACKET_TYPE_LAST_PARTIAL;

        pOpData->sessionCtx = *sessionCtx;
        pOpData->hashStartSrcOffsetInBytes = 0;
        pOpData->messageLenToHashInBytes = 0;
        pOpData->pDigestResult = pDigestBuffer;

        status = cpaCySymPerformOp(
            *cyInstHandle,
            (void *)&complete, /* data sent as is to the callback function*/
            pOpData,           /* operational data struct */
            pBufferList,       /* source buffer list */
            pBufferList,       /* same src & dst for an in-place operation*/
            NULL);

        if (CPA_STATUS_SUCCESS != status)
        {
            D_DEBUG(DB_TRACE, "cpaCySymPerformOp failed. (status = %d)\n", status);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            /** wait until the completion of the operation*/
            do {
                icp_sal_CyPollInstance(*cyInstHandle, 0);
                /** Busy polling vs. sleep and polling */
                // OS_SLEEP(1);
            } while (!complete);
        } 

        memcpy(csumBuf, pDigestBuffer, csumLen);
    }

    D_FREE(pBufferList);
    PHYS_CONTIG_FREE(pBufferMeta);
    PHYS_CONTIG_FREE(pDigestBuffer);
    D_FREE(pOpData);
    
    return status;
}

CpaStatus 
qat_hash_destroy(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx)
{
    /* Free session Context */
    PHYS_CONTIG_FREE(*sessionCtx);

    /* Stop instance */
    cpaCyStopInstance(*cyInstHandle);

    return CPA_STATUS_SUCCESS;
}
#endif
