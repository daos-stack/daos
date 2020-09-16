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
#include <daos/common.h>
#include <gurt/types.h>

#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <cpa.h>
#include <cpa_cy_im.h>
#include <cpa_cy_sym.h>
#include <icp_sal_user.h>
#include <icp_sal_poll.h>
#include <qae_mem.h>

#define MAX_INSTANCES 32

/* Use semaphores to signal completion of events */
struct completion_struct
{
    sem_t semaphore;
};

#define COMPLETION_STRUCT completion_struct

#define COMPLETION_INIT(s) sem_init(&((s)->semaphore), 0, 0);

#define COMPLETION_WAIT(s, timeout) (sem_wait(&((s)->semaphore)) == 0)

#define COMPLETE(s) sem_post(&((s)->semaphore))

#define COMPLETION_DESTROY(s) sem_destroy(&((s)->semaphore))

/* Memory */
#define OS_MALLOC(ppMemAddr, sizeBytes)                                        \
    Mem_OsMemAlloc((void *)(ppMemAddr), (sizeBytes))

#define PHYS_CONTIG_ALLOC(ppMemAddr, sizeBytes)                                \
    Mem_Alloc_Contig((void *)(ppMemAddr), (sizeBytes), 1)

#define PHYS_CONTIG_ALLOC_ALIGNED(ppMemAddr, sizeBytes, alignment)             \
    Mem_Alloc_Contig((void *)(ppMemAddr), (sizeBytes), (alignment))

#define OS_FREE(pMemAddr) Mem_OsMemFree((void *)&pMemAddr)

#define PHYS_CONTIG_FREE(pMemAddr) Mem_Free_Contig((void *)&pMemAddr)

extern CpaStatus qaeMemInit(void);
extern void qaeMemDestroy(void);

/* Threads */
typedef pthread_t sampleThread;
static sampleThread gPollingThread;
static volatile int gPollingCy = 0;

static __inline CpaStatus 
Mem_OsMemAlloc(void **ppMemAddr, Cpa32U sizeBytes)
{
    *ppMemAddr = malloc(sizeBytes);
    if (NULL == *ppMemAddr)
    {
        return CPA_STATUS_RESOURCE;
    }
    return CPA_STATUS_SUCCESS;
}

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
Mem_OsMemFree(void **ppMemAddr)
{
    if (NULL != *ppMemAddr)
    {
        free(*ppMemAddr);
        *ppMemAddr = NULL;
    }
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

static __inline CpaStatus 
threadCreate(sampleThread *thread,
            void *funct,
            void *args)
{
    if (pthread_create(thread, NULL, funct, args) != 0)
    {
        D_DEBUG(DB_TRACE, "Failed create thread\n");
        return CPA_STATUS_FAIL;
    }
    else
    {
        pthread_detach(*thread);
        return CPA_STATUS_SUCCESS;
    }
}

static __inline void 
threadExit(void)
{
    pthread_exit(NULL);
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
sal_polling(CpaInstanceHandle cyInstHandle)
{
    gPollingCy = 1;
    while (gPollingCy)
    {
        icp_sal_CyPollInstance(cyInstHandle, 0);
        OS_SLEEP(10);
    }

    threadExit();
}

static void 
startPolling(CpaInstanceHandle cyInstHandle)
{
    CpaInstanceInfo2 info2 = {0};
    CpaStatus status = CPA_STATUS_SUCCESS;

    status = cpaCyInstanceGetInfo2(cyInstHandle, &info2);
    if ((status == CPA_STATUS_SUCCESS) && (info2.isPolled == CPA_TRUE))
    {
        /* Start thread to poll instance */
        threadCreate(&gPollingThread, sal_polling, cyInstHandle);
    }
}

static void 
stopPolling(void)
{
    gPollingCy = 0;
    OS_SLEEP(10);
}

/*
 * Callback function when operation is completed.
 */
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
        /** indicate that the function has been called*/
        COMPLETE((struct COMPLETION_STRUCT *)pCallbackTag);
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
         * If the instance is polled start the polling thread. Note that
         * how the polling is done is implementation-dependent.
         */
        startPolling(*cyInstHandle);

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
        sizeof(CpaBufferList) + (numBuffers * sizeof(CpaFlatBuffer));
    Cpa8U *pSrcBuffer = NULL;
    Cpa8U *pDigestBuffer = NULL;

    /* The following variables are allocated on the stack because we block
     * until the callback comes back. If a non-blocking approach was to be
     * used then these variables should be dynamically allocated */
    struct COMPLETION_STRUCT complete;
    status =
        cpaCyBufferListGetMetaSize(*cyInstHandle, numBuffers, &bufferMetaSize);

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pBufferMeta, bufferMetaSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = OS_MALLOC(&pBufferList, bufferListMemSize);
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

        status = OS_MALLOC(&pOpData, sizeof(CpaCySymOpData));
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /** initialization for callback; the "complete" variable is used by the
         * callback function to indicate it has been called*/
        COMPLETION_INIT((&complete));

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
            if (!COMPLETION_WAIT((&complete), TIMEOUT_MS))
            {
                D_DEBUG(DB_TRACE, "timeout or interruption in cpaCySymPerformOp\n");
                status = CPA_STATUS_FAIL;
            }
        } 
        
        if (!packetTypePartial)
            memcpy(csumBuf, pDigestBuffer, csumLen);
    }

    PHYS_CONTIG_FREE(pSrcBuffer);
    OS_FREE(pBufferList);
    PHYS_CONTIG_FREE(pBufferMeta);
    PHYS_CONTIG_FREE(pDigestBuffer);
    OS_FREE(pOpData);
    COMPLETION_DESTROY(&complete);

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

    struct COMPLETION_STRUCT complete;

    status =
        cpaCyBufferListGetMetaSize(*cyInstHandle, 1, &bufferMetaSize);

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pBufferMeta, bufferMetaSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = OS_MALLOC(&pBufferList, bufferListMemSize);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status = PHYS_CONTIG_ALLOC(&pDigestBuffer, csumLen);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        pBufferList->numBuffers = 0;
        status = OS_MALLOC(&pOpData, sizeof(CpaCySymOpData));
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        COMPLETION_INIT((&complete));

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
            if (!COMPLETION_WAIT((&complete), TIMEOUT_MS))
            {
                D_DEBUG(DB_TRACE, "timeout or interruption in cpaCySymPerformOp\n");
                status = CPA_STATUS_FAIL;
            }
        }

        memcpy(csumBuf, pDigestBuffer, csumLen);
    }

    OS_FREE(pBufferList);
    PHYS_CONTIG_FREE(pBufferMeta);
    PHYS_CONTIG_FREE(pDigestBuffer);
    OS_FREE(pOpData);
    
    return status;
}

CpaStatus 
qat_hash_destroy(CpaInstanceHandle *cyInstHandle, 
                                    CpaCySymSessionCtx *sessionCtx)
{
    /* Stop the polling thread */
    stopPolling();

    /* Free session Context */
    PHYS_CONTIG_FREE(*sessionCtx);

    /* Stop instance */
    cpaCyStopInstance(*cyInstHandle);

    return CPA_STATUS_SUCCESS;
}
#endif
