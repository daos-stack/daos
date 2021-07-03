/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
/* it is NOT hardware limitation, which is used only when the */
/* caller doesn't specify a value */
#define MAX_BUF_SIZE 65536

struct callback_data {
	CpaDcRqResults *dcResults;
	CpaBufferList *pBufferListSrc;
	CpaBufferList *pBufferListDst;
	uint8_t *dst;
	size_t dstLen;
	dc_callback_fn user_cb_fn;
	void *user_cb_data;
};

static int inst_num;

/** Memory */
static inline CpaStatus
mem_alloc_contig(void **ppMemAddr,
		 Cpa32U sizeBytes,
		 Cpa32U alignment)
{
	*ppMemAddr = qaeMemAllocNUMA(sizeBytes, 0, alignment);
	if (*ppMemAddr == NULL)
		return CPA_STATUS_RESOURCE;

	return CPA_STATUS_SUCCESS;
}

static inline void
mem_free_contig(void **ppMemAddr)
{
	if (*ppMemAddr != NULL) {
		qaeMemFreeNUMA(ppMemAddr);
		*ppMemAddr = NULL;
	}
}

static inline CpaPhysicalAddr
virt_to_phys(void *virtAddr)
{
	return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virtAddr);
}

/** Get Compression Instance */
static void
get_dc_instance(CpaInstanceHandle *pDcInstHandle)
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
		inst_num = (inst_num + 1) % numInstances;
		if (status == CPA_STATUS_SUCCESS)
			*pDcInstHandle = dcInstHandles[inst_num];
	}
}

/** Define user callback function for post processing */
static void
user_callback(void *user_cb_data, int produced, int status)
{
	int *cb_data = (int *)user_cb_data;

	if (status == DC_STATUS_OK)
		*cb_data = produced;
	else
		*cb_data = status;
}

/** Define callback function triggered by QAT driver */
static void
dc_callback(void *pCallbackTag, CpaStatus status)
{
	int dc_status = DC_STATUS_OK;
	int produced = 0;
	dc_callback_fn user_cb_fn;
	void *user_cb_data;

	if (pCallbackTag != NULL) {
		struct callback_data *cb_data =
				(struct callback_data *)pCallbackTag;
		user_cb_fn = cb_data->user_cb_fn;
		user_cb_data = cb_data->user_cb_data;
		produced = cb_data->dcResults->produced;
		if (status == CPA_DC_OK &&
		    produced > 0 &&
		    produced <= cb_data->dstLen) {
			/* Copy output from pinned-mem to virtual-mem */
			memcpy(cb_data->dst,
			       cb_data->pBufferListDst->pBuffers->pData,
			       produced);
		} else {
			produced = 0;
			if (status == CPA_DC_OVERFLOW)
				dc_status = DC_STATUS_OVERFLOW;
			else
				dc_status = DC_STATUS_ERR;
		}

		/** Free memory */
		mem_free_contig(
			(void *)&cb_data->pBufferListSrc->pPrivateMetaData);
		mem_free_contig(
			(void *)&cb_data->pBufferListSrc->pPrivateMetaData);
		mem_free_contig(
			(void *)&cb_data->pBufferListSrc->pBuffers->pData);
		mem_free_contig(
			(void *)&cb_data->pBufferListDst->pPrivateMetaData);
		mem_free_contig(
			(void *)&cb_data->pBufferListDst->pBuffers->pData);
		D_FREE(cb_data->pBufferListSrc);
		D_FREE(cb_data->pBufferListDst);
		D_FREE(cb_data->dcResults);
		D_FREE(cb_data);

		/** Now trigger user-defined callback function */
		if (user_cb_fn)
			(user_cb_fn)(user_cb_data, produced, dc_status);
	}
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

int
qat_dc_poll_response(CpaInstanceHandle *dcInstHandle)
{
	return icp_sal_DcPollInstance(*dcInstHandle, 0);
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

	get_dc_instance(dcInstHandle);
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
		status = mem_alloc_contig(
			(void *)&interBufs,
			*numInterBuffLists * sizeof(CpaBufferList *),
			1);

	/** if max buffer size is zero, set default value 64KB */
	if (maxBufferSize == 0)
		maxBufferSize = MAX_BUF_SIZE;

	for (i = 0; i < *numInterBuffLists; i++) {
		if (status == CPA_STATUS_SUCCESS)
			status = mem_alloc_contig(
				(void *)&interBufs[i],
				sizeof(CpaBufferList), 1);

		if (status == CPA_STATUS_SUCCESS)
			status = mem_alloc_contig(
			(void *)(&interBufs[i]->pPrivateMetaData),
			buffMetaSize, 1);

		if (status == CPA_STATUS_SUCCESS)
			status = mem_alloc_contig(
				(void *)&interBufs[i]->pBuffers,
				sizeof(CpaFlatBuffer), 1);

		if (status == CPA_STATUS_SUCCESS) {
			/* Implementation requires an intermediate */
			/* buffer approximately twice the size of */
			/* the output buffer */
			status = mem_alloc_contig(
				(void *)&interBufs[i]->pBuffers->pData,
				2 * maxBufferSize, 1);
			interBufs[i]->numBuffers = 1;
			interBufs[i]->pBuffers->dataLenInBytes =
				2 * maxBufferSize;
		}
	}

	if (status == CPA_STATUS_SUCCESS)
		status = cpaDcSetAddressTranslation(*dcInstHandle,
						    virt_to_phys);

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
		status = mem_alloc_contig((void *)sessionHdl, sess_size, 1);

	/* Initialize the Stateless session */
	if (status == CPA_STATUS_SUCCESS) {
		status = cpaDcInitSession(
			*dcInstHandle,
			*sessionHdl,
			&sd,
			NULL,
			dc_callback);
	}

	*bufferInterArrayPtr = interBufs;

	if (status == CPA_STATUS_SUCCESS)
		return DC_STATUS_OK;

	qat_dc_destroy(dcInstHandle, sessionHdl,
		       interBufs, *numInterBuffLists);
	return DC_STATUS_ERR;
}

int
qat_dc_compress_async(
		CpaInstanceHandle *dcInstHandle,
		CpaDcSessionHandle *sessionHdl,
		uint8_t *src,
		size_t srcLen,
		uint8_t *dst,
		size_t dstLen,
		enum QAT_COMPRESS_DIR dir,
		dc_callback_fn user_cb_fn,
		void *user_cb_data)
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
	CpaDcRqResults *dcResults;
	CpaDcOpData opData = {};
	Cpa32U numBuffers = 1;
	struct callback_data *cb_data;

	D_ALLOC(cb_data, sizeof(struct callback_data));
	D_ALLOC(dcResults, sizeof(CpaDcRqResults));

	cb_data->dcResults = dcResults;
	cb_data->dst = dst;
	cb_data->dstLen = dstLen;
	cb_data->user_cb_fn = user_cb_fn;
	cb_data->user_cb_data = user_cb_data;

	Cpa32U bufferListMemSize =
		sizeof(CpaBufferList) + (numBuffers * sizeof(CpaFlatBuffer));

	opData.compressAndVerify = CPA_TRUE;

	status = cpaDcBufferListGetMetaSize(
			*dcInstHandle,
			numBuffers,
			&bufferMetaSize);

	/* Allocate source buffer */
	if (status == CPA_STATUS_SUCCESS)
		status = mem_alloc_contig(
				(void *)&pBufferMetaSrc, bufferMetaSize, 1);


	if (status == CPA_STATUS_SUCCESS) {
		D_ALLOC(pBufferListSrc, bufferListMemSize);
		status = pBufferListSrc ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
	}
	if (status == CPA_STATUS_SUCCESS)
		status = mem_alloc_contig((void *)&pSrcBuffer, srcLen, 1);

	/* Allocate destination buffer the same size as source buffer */
	if (status == CPA_STATUS_SUCCESS)
		status = mem_alloc_contig(
				(void *)&pBufferMetaDst, bufferMetaSize, 1);

	if (status == CPA_STATUS_SUCCESS) {
		D_ALLOC(pBufferListDst, bufferListMemSize);
		status = pBufferListDst ? CPA_STATUS_SUCCESS : CPA_STATUS_FAIL;
	}
	if (status == CPA_STATUS_SUCCESS)
		status = mem_alloc_contig((void *)&pDstBuffer, dstLen, 1);

	cb_data->pBufferListSrc = pBufferListSrc;
	cb_data->pBufferListDst = pBufferListDst;

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
			/** Keep trying to send the request until success */
			if (dir == DIR_COMPRESS)
				status = cpaDcCompressData2(
					*dcInstHandle,
					*sessionHdl,
					pBufferListSrc,
					pBufferListDst,
					&opData,
					dcResults,
					(void *)cb_data);
			else
				status = cpaDcDecompressData2(
					*dcInstHandle,
					*sessionHdl,
					pBufferListSrc,
					pBufferListDst,
					&opData,
					dcResults,
					(void *)cb_data);
			icp_sal_DcPollInstance(*dcInstHandle, 0);
		} while (status == CPA_STATUS_RETRY);
	}

	if (status == CPA_STATUS_SUCCESS)
		return DC_STATUS_OK;
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
	int user_cb_data = 0;
	CpaStatus status = CPA_STATUS_SUCCESS;

	status = qat_dc_compress_async(dcInstHandle, sessionHdl,
				       src, srcLen, dst, dstLen, dir,
				       user_callback, (void *)&user_cb_data);

	if (status == DC_STATUS_OK) {
		/** wait until the completion of the operation */
		do {
			icp_sal_DcPollInstance(*dcInstHandle, 0);
		} while (user_cb_data == 0);

		if (user_cb_data > 0) {
			*produced = user_cb_data;
			return DC_STATUS_OK;
		}
		return user_cb_data;
	}

	return status;
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
	mem_free_contig((void *)sessionHdl);

	/* Free intermediate buffers */
	if (interBufs != NULL) {
		for (i = 0; i < numInterBuffLists; i++) {
			mem_free_contig((void *)
					&interBufs[i]->pBuffers->pData);
			mem_free_contig((void *)
					&interBufs[i]->pBuffers);
			mem_free_contig((void *)
					&interBufs[i]->pPrivateMetaData);
			mem_free_contig((void *)&interBufs[i]);
		}
		mem_free_contig((void *)&interBufs);
	}

	icp_sal_userStop();

	qaeMemDestroy();

	return 0;
}
#endif
