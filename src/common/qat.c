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
#define D_LOGFAC DD_FAC(csum)

#ifdef HAVE_QAT

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include <daos/common.h>
#include <daos/compression.h>
#include <gurt/types.h>
#include <qat.h>

#define MAX_INSTANCES 32

/* Max buffer size is used for intermediate buffer allocation, */
/* it is NOT hardware limitation */
#define MAX_BUF_SIZE 65536

/** Memory */
static inline CpaStatus
Mem_Alloc_Contig(void **ppMemAddr,
		 Cpa32U sizeBytes,
		 Cpa32U alignment)
{
	*ppMemAddr = qaeMemAllocNUMA(sizeBytes, 0, alignment);
	if (*ppMemAddr == NULL)
		return CPA_STATUS_RESOURCE;

	return CPA_STATUS_SUCCESS;
}

static inline void
Mem_Free_Contig(void **ppMemAddr)
{
	if (*ppMemAddr != NULL) {
		qaeMemFreeNUMA(ppMemAddr);
		*ppMemAddr = NULL;
	}
}

static inline CpaStatus
OS_SLEEP(Cpa32U ms)
{
	int ret = 0;
	struct timespec resTime, remTime;

	resTime.tv_sec = ms / 1000;
	resTime.tv_nsec = (ms % 1000) * 1000000;
	do {
		ret = nanosleep(&resTime, &remTime);
		resTime = remTime;
	} while ((ret != 0) && (errno == EINTR));

	if (ret != 0)
		return CPA_STATUS_FAIL;
	else
		return CPA_STATUS_SUCCESS;
}

static inline CpaPhysicalAddr
virtToPhys(void *virtAddr)
{
	return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virtAddr);
}

static void
getDcInstance(CpaInstanceHandle *pDcInstHandle)
{
	CpaInstanceHandle dcInstHandles[MAX_INSTANCES];
	Cpa16U numInstances = 0;
	CpaStatus status = CPA_STATUS_SUCCESS;

	*pDcInstHandle = NULL;
	status = cpaDcGetNumInstances(&numInstances);

	if (numInstances >= MAX_INSTANCES)
		numInstances = MAX_INSTANCES;

	if ((status == CPA_STATUS_SUCCESS) && (numInstances > 0)) {
		status = cpaDcGetInstances(numInstances, dcInstHandles);
		if (status == CPA_STATUS_SUCCESS)
			*pDcInstHandle = dcInstHandles[0];
	}
}

static void
dcCallback(void *pCallbackTag, CpaStatus status)
{
	if (pCallbackTag != NULL)
		*(Cpa32U *)pCallbackTag = 1;
}

/** Common Functions */
int
qat_dc_is_available()
{
	Cpa16U numInstances = 0;
	CpaStatus status = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);

	if (status != CPA_STATUS_SUCCESS)
		return 0;

	cpaDcGetNumInstances(&numInstances);
	icp_sal_userStop();

	return (numInstances > 0);
}

/** Compression Functions */
int
qat_dc_init(CpaInstanceHandle *dcInstHandle,
	    CpaDcSessionHandle *sessionHdl,
	    Cpa16U *numInterBuffLists,
	    CpaBufferList ***bufferInterArrayPtr,
	    Cpa32U maxBufferSize,
	    CpaDcCompLvl compLvl)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	CpaBufferList **interBufs = NULL;
	Cpa16U i = 0;
	Cpa32U buffMetaSize = 0;

	Cpa32U sess_size = 0;
	Cpa32U ctx_size = 0;
	CpaDcSessionSetupData sd = {0};

	*numInterBuffLists = 0;

	status = qaeMemInit();
	if (status != CPA_STATUS_SUCCESS) {
		D_ERROR("QAT: Failed to initialize memory driver\n");
		return DC_STATUS_ERR;
	}

	status = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);
	if (status != CPA_STATUS_SUCCESS) {
		D_ERROR("QAT: Failed to start user process SSL\n");
		qaeMemDestroy();
		return DC_STATUS_ERR;
	}

	getDcInstance(dcInstHandle);
	if (*dcInstHandle == NULL) {
		D_ERROR("QAT: No DC instance\n");
		qaeMemDestroy();
		return DC_STATUS_ERR;
	}


	status = cpaDcBufferListGetMetaSize(*dcInstHandle, 1, &buffMetaSize);

	if (status == CPA_STATUS_SUCCESS)
		status = cpaDcGetNumIntermediateBuffers(
			*dcInstHandle,
			numInterBuffLists);

	if (status == CPA_STATUS_SUCCESS && *numInterBuffLists != 0)
		status = Mem_Alloc_Contig(
			(void *)&interBufs,
			*numInterBuffLists * sizeof(CpaBufferList *),
			1);

	/** if max buffer size is zero, set default value 64KB */
	if (maxBufferSize == 0)
		maxBufferSize = MAX_BUF_SIZE;

	for (i = 0; i < *numInterBuffLists; i++) {
		if (status == CPA_STATUS_SUCCESS)
			status = Mem_Alloc_Contig(
				(void *)&interBufs[i],
				sizeof(CpaBufferList), 1);

		if (status == CPA_STATUS_SUCCESS)
			status = Mem_Alloc_Contig(
			(void *)(&interBufs[i]->pPrivateMetaData),
			buffMetaSize, 1);

		if (status == CPA_STATUS_SUCCESS)
			status = Mem_Alloc_Contig(
				(void *)&interBufs[i]->pBuffers,
				sizeof(CpaFlatBuffer), 1);

		if (status == CPA_STATUS_SUCCESS) {
			/* Implementation requires an intermediate */
			/* buffer approximately twice the size of */
			/* the output buffer */
			status = Mem_Alloc_Contig(
				(void *)&interBufs[i]->pBuffers->pData,
				2 * maxBufferSize, 1);
			interBufs[i]->numBuffers = 1;
			interBufs[i]->pBuffers->dataLenInBytes =
				2 * maxBufferSize;
		}
	}

	if (status == CPA_STATUS_SUCCESS)
		status = cpaDcSetAddressTranslation(*dcInstHandle, virtToPhys);

	if (status == CPA_STATUS_SUCCESS)
		status = cpaDcStartInstance(
			*dcInstHandle, *numInterBuffLists, interBufs);

	if (status == CPA_STATUS_SUCCESS) {
		sd.compLevel = compLvl;
		sd.compType = CPA_DC_DEFLATE;
		sd.huffType = CPA_DC_HT_FULL_DYNAMIC;
		sd.autoSelectBestHuffmanTree = CPA_DC_ASB_STATIC_DYNAMIC;
		sd.sessDirection = CPA_DC_DIR_COMBINED;
		sd.sessState = CPA_DC_STATELESS;
		sd.checksum = CPA_DC_ADLER32;

		status = cpaDcGetSessionSize(*dcInstHandle, &sd,
					     &sess_size, &ctx_size);
	}

	if (status == CPA_STATUS_SUCCESS)
		status = Mem_Alloc_Contig((void *)sessionHdl, sess_size, 1);

	/* Initialize the Stateless session */
	if (status == CPA_STATUS_SUCCESS) {
		status = cpaDcInitSession(
			*dcInstHandle,
			*sessionHdl,
			&sd,
			NULL,
			dcCallback);
	}

	*bufferInterArrayPtr = interBufs;

	if (status == CPA_STATUS_SUCCESS)
		return DC_STATUS_OK;

	qat_dc_destroy(dcInstHandle, sessionHdl,
		       interBufs, *numInterBuffLists);
	return DC_STATUS_ERR;
}

int
qat_dc_compress(CpaInstanceHandle *dcInstHandle,
		CpaDcSessionHandle *sessionHdl,
		uint8_t *src,
		size_t srcLen,
		uint8_t *dst,
		size_t dstLen,
		size_t *produced,
		enum QAT_COMPRESS_DIR dir)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa8U *pBufferMetaSrc = NULL;
	Cpa8U *pBufferMetaDst = NULL;
	Cpa32U bufferMetaSize = 0;
	CpaBufferList *pBufferListSrc = NULL;
	CpaBufferList *pBufferListDst = NULL;
	CpaFlatBuffer *pFlatBuffer = NULL;
	Cpa8U *pSrcBuffer = NULL;
	Cpa8U *pDstBuffer = NULL;
	CpaDcRqResults dcResults;
	CpaDcOpData opData = {};
	Cpa32U numBuffers = 1;
	Cpa32U complete = 0;
	Cpa32U bufferListMemSize =
		sizeof(CpaBufferList) + (numBuffers * sizeof(CpaFlatBuffer));
	int rc = DC_STATUS_ERR;

	opData.compressAndVerify = CPA_TRUE;

	status = cpaDcBufferListGetMetaSize(
			*dcInstHandle,
			numBuffers,
			&bufferMetaSize);

	/* Allocate source buffer */
	if (status == CPA_STATUS_SUCCESS)
		status = Mem_Alloc_Contig(
				(void *)&pBufferMetaSrc, bufferMetaSize, 1);


	if (status == CPA_STATUS_SUCCESS) {
		D_ALLOC(pBufferListSrc, bufferListMemSize);
		status = pBufferListSrc ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
	}
	if (status == CPA_STATUS_SUCCESS)
		status = Mem_Alloc_Contig((void *)&pSrcBuffer, srcLen, 1);

	/* Allocate destination buffer the same size as source buffer */
	if (status == CPA_STATUS_SUCCESS)
		status = Mem_Alloc_Contig(
				(void *)&pBufferMetaDst, bufferMetaSize, 1);

	if (status == CPA_STATUS_SUCCESS) {
		D_ALLOC(pBufferListDst, bufferListMemSize);
		status = pBufferListDst ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
	}
	if (status == CPA_STATUS_SUCCESS)
		status = Mem_Alloc_Contig((void *)&pDstBuffer, dstLen, 1);

	if (status == CPA_STATUS_SUCCESS) {
		/* Copy source into buffer */
		memcpy(pSrcBuffer, src, srcLen);

		/* Build source bufferList */
		pFlatBuffer = (CpaFlatBuffer *)(pBufferListSrc + 1);

		pBufferListSrc->pBuffers = pFlatBuffer;
		pBufferListSrc->numBuffers = 1;
		pBufferListSrc->pPrivateMetaData = pBufferMetaSrc;

		pFlatBuffer->dataLenInBytes = srcLen;
		pFlatBuffer->pData = pSrcBuffer;

		/* Build destination bufferList */
		pFlatBuffer = (CpaFlatBuffer *)(pBufferListDst + 1);

		pBufferListDst->pBuffers = pFlatBuffer;
		pBufferListDst->numBuffers = 1;
		pBufferListDst->pPrivateMetaData = pBufferMetaDst;

		pFlatBuffer->dataLenInBytes = dstLen;
		pFlatBuffer->pData = pDstBuffer;

		do {
			/** keep trying to send the request until success */
			if (dir == DIR_COMPRESS)
				status = cpaDcCompressData2(
					*dcInstHandle,
					*sessionHdl,
					pBufferListSrc,
					pBufferListDst,
					&opData,
					&dcResults,
					(void *)&complete);
			else
				status = cpaDcDecompressData2(
					*dcInstHandle,
					*sessionHdl,
					pBufferListSrc,
					pBufferListDst,
					&opData,
					&dcResults,
					(void *)&complete);
		} while (status == CPA_STATUS_RETRY);

		if (status == CPA_STATUS_SUCCESS) {
			/** wait until the completion of the operation */
			do {
				icp_sal_DcPollInstance(*dcInstHandle, 0);

				/** Sleep and poll */
				OS_SLEEP(10);
			} while (!complete);

			if (dcResults.status == CPA_DC_OK &&
			    dcResults.produced <= dstLen) {
				/** Copy the output to dst buffer */
				memcpy(dst, pDstBuffer, dcResults.produced);
				*produced = dcResults.produced;
				rc = DC_STATUS_OK;
			} else if (dcResults.status == CPA_DC_OVERFLOW)
				rc = DC_STATUS_OVERFLOW;
		}
	}

	Mem_Free_Contig((void *)&pBufferMetaSrc);
	Mem_Free_Contig((void *)&pSrcBuffer);
	Mem_Free_Contig((void *)&pBufferMetaDst);
	Mem_Free_Contig((void *)&pDstBuffer);
	D_FREE(pBufferListSrc);
	D_FREE(pBufferListDst);

	return rc;
}

int
qat_dc_destroy(CpaInstanceHandle *dcInstHandle,
	       CpaDcSessionHandle *sessionHdl,
	       CpaBufferList **interBufs,
	       Cpa16U numInterBuffLists)
{
	int i = 0;

	cpaDcRemoveSession(*dcInstHandle, *sessionHdl);

	cpaDcStopInstance(*dcInstHandle);

	/* Free session Context */
	Mem_Free_Contig((void *)sessionHdl);

	/* Free intermediate buffers */
	if (interBufs != NULL) {
		for (i = 0; i < numInterBuffLists; i++) {
			Mem_Free_Contig((void *)
					&interBufs[i]->pBuffers->pData);
			Mem_Free_Contig((void *)
					&interBufs[i]->pBuffers);
			Mem_Free_Contig((void *)
					&interBufs[i]->pPrivateMetaData);
			Mem_Free_Contig((void *)&interBufs[i]);
		}
		Mem_Free_Contig((void *)&interBufs);
	}

	icp_sal_userStop();

	qaeMemDestroy();

	return 0;
}
#endif
