/*
 * Copyright (C) 2024 Cornelis Networks.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "rdma/opx/fi_opx_endpoint.h"
#include "rdma/opx/fi_opx_hfi1_version.h"
#include "rdma/opx/opx_hfi1_pre_cn5000.h"

void opx_jkr_rhe_debug(struct fi_opx_ep * opx_ep,
		       volatile uint64_t *rhe_ptr,
		       volatile uint32_t * rhf_ptr,
		       const uint32_t rhf_msb,
		       const uint32_t rhf_lsb,
		       const uint64_t rhf_seq,
		       const uint64_t hdrq_offset,
		       const uint64_t rhf_rcvd,
		       const union fi_opx_hfi1_packet_hdr *const hdr)
{
	uint32_t rhe_index = hdrq_offset >> FI_OPX_HFI1_HDRQ_INDEX_SHIFT;
	volatile uint64_t *rhe = rhe_ptr + rhe_index; /* 8 byte entries */
#ifdef OPX_VERBOSE_TRIGGER // verbose output
	fprintf(stderr,
#else
	FI_DBG_TRACE(fi_opx_global.prov, FI_LOG_EP_DATA,
#endif
		     "RHF(%#16.16lX) RHE(%p)[%u]=%p RHE %#16.16lX is ERRORED %u, UseEgrBuf %u, EgrIndex %#X/%#X, EgrOffset %#X, %s%s%s %s %#16.16lX  %s%s%s%s%s%s%s%s%s%s%s \n",
		     rhf_rcvd, rhe_ptr, rhe_index, rhe, *rhe,
		     OPX_IS_ERRORED_RHF(rhf_rcvd) != 0UL,
		     OPX_RHF_IS_USE_EGR_BUF(rhf_rcvd),
		     (uint32_t)OPX_RHF_EGR_INDEX(rhf_rcvd),opx_ep->rx->egrq.last_egrbfr_index,
		     (uint32_t) OPX_RHF_EGR_OFFSET(rhf_rcvd),
		     OPX_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd)? "EXPECTED_RCV" : "",
		     OPX_RHF_RCV_TYPE_EAGER_RCV(rhf_rcvd)? "EAGER_RCV" : "",
		     OPX_RHF_RCV_TYPE_OTHER(rhf_rcvd)? "OTHER RCV" : "",
		     ((*rhe) & OPX_JKR_RHE_TAIL        )? "OPX_JKR_RHE_TAIL        " : "", ((*rhe) & OPX_JKR_RHE_TAIL),
		     ((*rhe) & OPX_JKR_RHE_ICRCERR     )? "OPX_JKR_RHE_ICRCERR     " : "",
		     ((*rhe) & OPX_JKR_RHE_TIDBYPASSERR)? "OPX_JKR_RHE_TIDBYPASSERR" : "",
		     ((*rhe) & OPX_JKR_RHE_ECCERR      )? "OPX_JKR_RHE_ECCERR      " : "",
		     ((*rhe) & OPX_JKR_RHE_LENERR      )? "OPX_JKR_RHE_LENERR      " : "",
		     ((*rhe) & OPX_JKR_RHE_TIDERR      )? "OPX_JKR_RHE_TIDERR      " : "",
		     ((*rhe) & OPX_JKR_RHE_RCVTYPEERR  )? "OPX_JKR_RHE_RCVTYPEERR  " : "",
		     ((*rhe) & OPX_JKR_RHE_CRKERR      )? "OPX_JKR_RHE_CRKERR      " : "",
		     ((*rhe) & OPX_JKR_RHE_CRKUNCERR   )? "OPX_JKR_RHE_CRKUNCERR   " : "",
		     ((*rhe) & OPX_JKR_RHE_KHDRLENERR  )? "OPX_JKR_RHE_KHDRLENERR  " : "",
		     ((*rhe) & OPX_JKR_RHE_FLOWGENERR  )? "OPX_JKR_RHE_FLOWGENERR  " : "",
		     ((*rhe) & OPX_JKR_RHE_FLOWSEQERR  )? "OPX_JKR_RHE_FLOWSEQERR  " : "");

	FI_OPX_DEBUG_COUNTERS_INC(opx_ep->debug_counters.rhf.error);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_ICRCERR     , opx_ep->debug_counters.rhf.icrcerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_TIDBYPASSERR,opx_ep->debug_counters.rhf.tidbypasserr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_ECCERR      ,opx_ep->debug_counters.rhf.eccerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_LENERR      ,opx_ep->debug_counters.rhf.lenerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_TIDERR      ,opx_ep->debug_counters.rhf.tiderr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_CRKERR      ,opx_ep->debug_counters.rhf.crkerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_CRKUNCERR   ,opx_ep->debug_counters.rhf.crkuncerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_KHDRLENERR  ,opx_ep->debug_counters.rhf.khdrlenerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_FLOWGENERR  ,opx_ep->debug_counters.rhf.flowgenerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_FLOWSEQERR  ,opx_ep->debug_counters.rhf.flowseqerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((*rhe) & OPX_JKR_RHE_RCVTYPEERR  ,opx_ep->debug_counters.rhf.rcvtypeerr);
	/* Count the packet type that had an error */
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeexp);
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_EAGER_RCV(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeegr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_OTHER(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeoth);

#ifdef OPX_VERBOSE_TRIGGER // verbose output
	fi_opx_hfi1_dump_packet_hdr (hdr, "OPX_IS_ERRORED_RHF", __LINE__);
#endif

	/* trigger on unexpected errors ) ignoring TIDERR */
	if(rhe && !((*rhe) & OPX_JKR_RHE_TIDERR))
		opx_sw_trigger();

	return;
}


int opx_jkr_rhf_error_handler(const uint64_t rhf_rcvd, const union fi_opx_hfi1_packet_hdr *const hdr)
{
	const uint8_t opcode = hdr->stl.bth.opcode;

#ifdef OPX_VERBOSE_TRIGGER // verbose output
	fprintf(stderr,
#else
	FI_WARN(fi_opx_global.prov, FI_LOG_EP_DATA,
#endif
		"%s:%s():%d MISSING PAYLOAD opcode %#X, UseEgrBuf %u, pktlen %#X, type: %s%s%s\n",
		__FILE__, __func__, __LINE__,
		opcode, OPX_RHF_IS_USE_EGR_BUF(rhf_rcvd), ntohs(hdr->stl.lrh.pktlen),
		OPX_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd)? "EXPECTED_RCV" : "",
		OPX_RHF_RCV_TYPE_EAGER_RCV(rhf_rcvd)? "EAGER_RCV" : "",
		OPX_RHF_RCV_TYPE_OTHER(rhf_rcvd)? "OTHER RCV" : "");
#ifdef OPX_VERBOSE_TRIGGER // verbose ouput
	fi_opx_hfi1_dump_packet_hdr (hdr, "MISSING PAYLOAD", __LINE__);
#endif
	opx_sw_trigger();
	return 1;
}

