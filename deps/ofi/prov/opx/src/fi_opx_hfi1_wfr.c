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

void opx_wfr_rhe_debug(struct fi_opx_ep * opx_ep,
		       volatile uint64_t *rhe_ptr,
		       volatile uint32_t * rhf_ptr,
		       const uint32_t rhf_msb,
		       const uint32_t rhf_lsb,
		       const uint64_t rhf_seq,
		       const uint64_t hdrq_offset,
		       const uint64_t rhf_rcvd,
		       const union fi_opx_hfi1_packet_hdr *const hdr)
{
#ifdef OPX_VERBOSE_TRIGGER // verbose output
	fprintf(stderr,
#else
	FI_DBG_TRACE(fi_opx_global.prov, FI_LOG_EP_DATA,
#endif
		     "RHF(%#16.16lX) RHE %#8.8X is ERRORED %u, UseEgrBuf %u, EgrIndex %#X/%#X, EgrOffset %#X, %s%s%s %s%s%s%s%s%s%s%s \n",
		     rhf_rcvd, rhf_msb & 0xBFE00000u,
		     OPX_IS_ERRORED_RHF(rhf_rcvd) != 0UL,
		     OPX_RHF_IS_USE_EGR_BUF(rhf_rcvd),
		     (uint32_t)OPX_RHF_EGR_INDEX(rhf_rcvd),opx_ep->rx->egrq.last_egrbfr_index,
		     (uint32_t) OPX_RHF_EGR_OFFSET(rhf_rcvd),
		     OPX_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd)? "EXPECTED_RCV" : "",
		     OPX_RHF_RCV_TYPE_EAGER_RCV(rhf_rcvd)? "EAGER_RCV" : "",
		     OPX_RHF_RCV_TYPE_OTHER(rhf_rcvd)? "OTHER RCV" : "",
		     rhf_msb & OPX_WFR_RHF_ICRCERR? "OPX_WFR_RHF_ICRCERR" :"",
		     rhf_msb & OPX_WFR_RHF_LENERR? "OPX_WFR_RHF_LENERR" :"",
		     rhf_msb & OPX_WFR_RHF_ECCERR? "OPX_WFR_RHF_ECCERR" :"",
		     rhf_msb & OPX_WFR_RHF_TIDERR? "OPX_WFR_RHF_TIDERR" :"",
		     rhf_msb & OPX_WFR_RHF_DCERR? "OPX_WFR_RHF_DCERR" :"",
		     rhf_msb & OPX_WFR_RHF_DCUNCERR? "OPX_WFR_RHF_DCUNCERR" :"",
		     rhf_msb & OPX_WFR_RHF_KHDRLENERR? "OPX_WFR_RHF_KHDRLENERR" :"",
		     rhf_msb & OPX_WFR_RHF_RCVTYPEERR? "OPX_WFR_RHF_RCVTYPEERR" :"");

	FI_OPX_DEBUG_COUNTERS_INC(opx_ep->debug_counters.rhf.error);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_ICRCERR   ,opx_ep->debug_counters.rhf.icrcerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_LENERR    ,opx_ep->debug_counters.rhf.lenerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_ECCERR    ,opx_ep->debug_counters.rhf.eccerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_TIDERR    ,opx_ep->debug_counters.rhf.tiderr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_DCERR     ,opx_ep->debug_counters.rhf.dcerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_DCUNCERR  ,opx_ep->debug_counters.rhf.dcuncerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_KHDRLENERR,opx_ep->debug_counters.rhf.khdrlenerr);
	FI_OPX_DEBUG_COUNTERS_INC_COND(rhf_msb & OPX_WFR_RHF_RCVTYPEERR,opx_ep->debug_counters.rhf.rcvtypeerr);
	/* Count the packet type that had an error */
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeexp);
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_EAGER_RCV(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeegr);
	FI_OPX_DEBUG_COUNTERS_INC_COND((OPX_RHF_RCV_TYPE_OTHER(rhf_rcvd)),opx_ep->debug_counters.rhf.rcvtypeoth);

#ifdef OPX_VERBOSE_TRIGGER // verbose output
	fi_opx_hfi1_dump_packet_hdr (hdr, "OPX_IS_ERRORED_RHF", __LINE__);
#endif

	return;
}

