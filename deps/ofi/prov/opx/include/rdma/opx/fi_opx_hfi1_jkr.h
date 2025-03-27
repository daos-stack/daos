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
#ifndef _OPX_HFI1_JKR_H_
#define _OPX_HFI1_JKR_H_

/* This field should be a zero extended copy of the DLID from the packet header.
   This field is used to determine if the packet should be delayed as part of the
   congestion control architecture improvements in JKR.
 */
#define OPX_PBC_JKR_DLID_SHIFT         0
#define OPX_PBC_JKR_DLID_MASK          0xffffff

/* For SDMA operations this field indicates which send context's packet checking
   configuration should be used to validate the packet. For PIO operations this
   field is ignored.
 */
#define OPX_PBC_JKR_SCTXT_SHIFT        24
#define OPX_PBC_JKR_SCTXT_MASK         0xff

/* Unused:
 #define OPX_PBC_JKR_L2TYPE_8B          0b00
 #define OPX_PBC_JKR_L2TYPE_10B         0b01
 */
#define OPX_PBC_JKR_L2TYPE_9B          0b11
#define OPX_PBC_JKR_L2TYPE_16B         0b10
#define OPX_PBC_JKR_L2TYPE_SHIFT       20
#define OPX_PBC_JKR_L2TYPE_MASK        0b11

/* Jackal River has 2+2,
     2 physical ports and 2 loopback ports.
   Opx will have to know 2 new things to use these ports.
   The first is which physical port should this packet ingress
   the fabric on?  The second is, if this is  an SR-iov type of
   send (a new type of intranode), use the loopback port
   instead of that physical port to save some fabric traffic

   0 = port 1
   1 = port 2
   2 = loop 1
   3 = loop 2
*/

#define OPX_JKR_PHYSICAL_PORT_1         0
#define OPX_JKR_PHYSICAL_PORT_2         1
#define OPX_JKR_LOOPBACK_PORT_1         2
#define OPX_JKR_LOOPBACK_PORT_2         3


/* HFI defines both Port and Port Index */
#define OPX_JKR_PHYS_PORT_TO_INDEX(_port)   (_port-1)
#define OPX_JKR_INDEX_TO_PHYS_PORT(_index) (_index+1)

/* Loop back ports are not supported */
#define OPX_JKR_LOOP_PORT_TO_INDEX(_port)   (_port+1)
#define OPX_JKR_INDEX_TO_LOOP_PORT(_index) (_index-1)

#define OPX_PBC_JKR_PORT_SHIFT         16
#define OPX_PBC_JKR_PORT_MASK          0b11

/* This bit indicates that the packet header following the PBC is in compressed format.
   JKR ignores this bit unless the PbcL2Type indicates a 16B packet.When set it indicates
   that the 16B header is being presented in the compressed format and the final 16B
   header should be constructed in the egress path based on CSR values for the send
   context. This applies to both PIO and SDMA packets.
 */
#define OPX_PBC_JKR_L2COMPRESSED_SHIFT 19
#define OPX_PBC_JKR_L2COMPRESSED_MASK  0x1

/* Fields that unused on JKR (zero will be OR'd) */
#define OPX_PBC_JKR_UNUSED            0UL
         
#define OPX_PBC_JKR_DLID(_dlid) (((unsigned long long)(_dlid & OPX_PBC_JKR_DLID_MASK) << OPX_PBC_JKR_DLID_SHIFT) << OPX_PBC_MSB_SHIFT)
#define OPX_PBC_JKR_SCTXT(_ctx) (((unsigned long long)(_ctx & OPX_PBC_JKR_SCTXT_MASK) << OPX_PBC_JKR_SCTXT_SHIFT) << OPX_PBC_MSB_SHIFT)
#define OPX_PBC_JKR_L2COMPRESSED(_c)    OPX_PBC_JKR_UNUSED /* unused until 16B headers are optimized */
#define OPX_PBC_JKR_PORTIDX(_pidx)  (((OPX_JKR_PHYS_PORT_TO_INDEX(_pidx)) & OPX_PBC_JKR_PORT_MASK) << OPX_PBC_JKR_PORT_SHIFT)
#define OPX_PBC_JKR_LRH_DLID_TO_PBC_DLID(_dlid)    OPX_PBC_JKR_DLID(htons(_dlid >> 16))

#ifndef NDEBUG
__OPX_FORCE_INLINE__
uint32_t opx_pbc_jkr_l2type(unsigned _type)
{
	/* 16B not supported yet */
	assert(_type == OPX_PBC_JKR_L2TYPE_9B);
	return (_type & OPX_PBC_JKR_L2TYPE_MASK) << OPX_PBC_JKR_L2TYPE_SHIFT;
}
#define OPX_PBC_JKR_L2TYPE(_type) opx_pbc_jkr_l2type(_type)
#else
#define OPX_PBC_JKR_L2TYPE(_type) ((OPX_PBC_JKR_L2TYPE_9B & OPX_PBC_JKR_L2TYPE_MASK) << OPX_PBC_JKR_L2TYPE_SHIFT) /* 16B not supported yet */
#endif

#define OPX_PBC_JKR_RUNTIME(_dlid, _pidx) OPX_PBC_JKR_UNUSED


/* BTH */

/* JKR HAS 16B RC:
   0 RC_IN_ORDER_0 Routing method 0 for in-order traffic
   1 RC_IN_ORDER_1 Routing method 1 for in-order traffic
   2 RC_IN_ORDER_2 Routing method 2 for in-order traffic
   3 RC_IN_ORDER_3 Routing method 3 for in-order traffic
   4 RC_OUT_OF_ORDER_0 Routing method 0 for out-of-order traffic. Often it is used for adaptive routing
   5 RC_OUT_OF_ORDER_1 Routing method 1 for out-of-order traffic. Often it is used for adaptive routing.
   6 RC_OUT_OF_ORDER_2 Routing method 2 for out-of-order traffic. Often it is used for adaptive routing.
   7 RC_OUT_OF_ORDER_3 Routing method 3 for out-of-order traffic. Often it is used for adaptive routing.
 */
#define OPX_RC_IN_ORDER_0      0
#define OPX_RC_IN_ORDER_1      1
#define OPX_RC_IN_ORDER_2      2
#define OPX_RC_IN_ORDER_3      3
#define OPX_RC_OUT_OF_ORDER_0  4
#define OPX_RC_OUT_OF_ORDER_1  5
#define OPX_RC_OUT_OF_ORDER_2  6
#define OPX_RC_OUT_OF_ORDER_3  7
#define OPX_RC2_MAX            OPX_RC_OUT_OF_ORDER_3

/* For 9B packets, RC[2] is carried in the BTH. The MSB bit, RC[2],
   specifies whether it is for in-order or out-of-order traffic.
   RC[1] and RC[0] do not appear in a 9B packet, but are considered
   to be 0 for during 9B packet routing.
 */
#define OPX_RC2_MASK 0b100
#define OPX_RC1_MASK 0b10
#define OPX_RC0_MASK 0b1

#define OPX_RC2_SHIFT 2
#define OPX_RC1_SHIFT 1
#define OPX_RC0_SHIFT 0

#define OPX_BTH_RC2_IN_ORDER      ((OPX_RC_IN_ORDER_0 & OPX_RC2_MASK) >> OPX_RC2_SHIFT)
#define OPX_BTH_RC2_OUT_OF_ORDER  ((OPX_RC_OUT_OF_ORDER_0 & OPX_RC2_MASK) >> OPX_RC2_SHIFT)
#define OPX_BTH_GET_RC2_VAL(_rc)  ((_rc & OPX_RC2_MASK) >> OPX_RC2_SHIFT)

#define OPX_BTH_RC2_DEFAULT  OPX_BTH_RC2_IN_ORDER

static inline int opx_bth_rc2_val()
{
    int rate_control;
    FI_TRACE(fi_opx_global.prov, FI_LOG_EP_DATA, "Calling rate control\n");
    if (fi_param_get_int(fi_opx_global.prov, "rate_control", &rate_control) == FI_SUCCESS) {
        if (rate_control <= OPX_RC2_MAX) {
            FI_TRACE(fi_opx_global.prov, FI_LOG_EP_DATA, "Rate control received = %d", rate_control);
            return OPX_BTH_GET_RC2_VAL(rate_control);
        }
    }
    return OPX_BTH_RC2_DEFAULT;
}



/* The bit shifts here are for the half word indicating the ECN field */
#define OPX_BTH_JKR_CSPEC_SHIFT        3
#define OPX_BTH_JKR_CSPEC_MASK         0b111

#define OPX_BTH_JKR_RC2_SHIFT          2
#define OPX_BTH_JKR_RC2_MASK           0b1

#define OPX_BTH_JKR_CSPEC(_cspec) ((_cspec & OPX_BTH_JKR_CSPEC_MASK) << OPX_BTH_JKR_CSPEC_SHIFT)
#define OPX_BTH_JKR_RC2(_rc2)     ((_rc2 & OPX_BTH_JKR_RC2_MASK) << OPX_BTH_JKR_RC2_SHIFT)
#define OPX_BTH_JKR_RC2_VAL       opx_bth_rc2_val()

/* RHF */
/* JKR
 *
 * The RHF.RcvSeq field is located in MSB bits [27:24] and values are in
 * the range of (1..13) inclusive. A new packet is available when the
 * expected sequence number in the next header queue element matches
 * the RHF.RcvSeq field.
 *
 * Instead of shifting and masking the RHF bits to retrieve the
 * sequence number in the range of 1..13 (or, 0x1..0xD) use only a bit
 * mask to obtain the RHF sequence in the range of 0x10000000..0xD0000000.
 * In this scheme the expected sequence number is incremented by
 * 0x10000000 instead of 0x1.
 */

#define OPX_JKR_RHF_SEQ_NOT_MATCH(_seq, _rhf)   (_seq != (_rhf & 0x0F00000000000000ul))
#define OPX_JKR_RHF_SEQ_INCREMENT(_seq)         ((_seq < 0x0D00000000000000ul) * _seq + 0x0100000000000000ul)
#define OPX_JKR_IS_ERRORED_RHF(_rhf)            (_rhf & 0x8000000000000000ul)
#define OPX_JKR_RHF_SEQ_MATCH(_seq, _rhf)       (_seq == (_rhf & 0x0F00000000000000ul))
#define OPX_JKR_RHF_SEQ_INIT_VAL                (0x0100000000000000ul)

#define OPX_JKR_RHF_IS_USE_EGR_BUF(_rhf)        ((_rhf & 0x00008000ul) == 0x00008000ul)
#define OPX_JKR_RHF_EGRBFR_INDEX_MASK           (0x3FFF)
#define OPX_JKR_RHF_EGRBFR_INDEX_SHIFT          (16)
#define OPX_JKR_RHF_EGR_INDEX(_rhf)             ((_rhf >> OPX_JKR_RHF_EGRBFR_INDEX_SHIFT) & OPX_JKR_RHF_EGRBFR_INDEX_MASK)
#define OPX_JKR_RHF_EGR_OFFSET(_rhf)            ((_rhf >> 32) & 0x0FFFul)
#define OPX_JKR_RHF_HDRQ_OFFSET(_rhf)           ((_rhf >> (32 + 12)) & 0x01FFul)

#define OPX_JKR_RHE_ICRCERR      (0x8000000000000000ul)
#define OPX_JKR_RHE_TIDBYPASSERR (0x4000000000000000ul)
#define OPX_JKR_RHE_ECCERR       (0x2000000000000000ul)
#define OPX_JKR_RHE_LENERR       (0x1000000000000000ul)
#define OPX_JKR_RHE_TIDERR       (0x0800000000000000ul)
#define OPX_JKR_RHE_RCVTYPEERR   (0x0700000000000000ul)
#define OPX_JKR_RHE_CRKERR       (0x0080000000000000ul)
#define OPX_JKR_RHE_CRKUNCERR    (0x0040000000000000ul)
#define OPX_JKR_RHE_KHDRLENERR   (0x0020000000000000ul)
#define OPX_JKR_RHE_FLOWGENERR   (0x0010000000000000ul)
#define OPX_JKR_RHE_FLOWSEQERR   (0x0008000000000000ul)
#define OPX_JKR_RHE_TAIL         (0x000000000007FFFFul)

struct fi_opx_ep;

void opx_jkr_rhe_debug(struct fi_opx_ep * opx_ep,
		       volatile uint64_t *rhe_ptr,
		       volatile uint32_t * rhf_ptr,
		       const uint32_t rhf_msb,
		       const uint32_t rhf_lsb,
		       const uint64_t rhf_seq,
		       const uint64_t hdrq_offset,
		       const uint64_t rhf_rcvd,
		       const union fi_opx_hfi1_packet_hdr *const hdr);

#define OPX_JKR_RHE_DEBUG(_opx_ep, _rhe_ptr, _rhf_ptr, _rhf_msb, _rhf_lsb, _rhf_seq, _hdrq_offset, _rhf_rcvd, _hdr) \
        opx_jkr_rhe_debug(_opx_ep, _rhe_ptr, _rhf_ptr, _rhf_msb, _rhf_lsb, _rhf_seq, _hdrq_offset, _rhf_rcvd, _hdr)

// Common to both JKR/WFR

#define OPX_JKR_RHF_RCV_TYPE_EXPECTED_RCV(_rhf) ((_rhf & 0x00007000ul) == 0x00000000ul)
#define OPX_JKR_RHF_RCV_TYPE_EAGER_RCV(_rhf)    ((_rhf & 0x00001000ul) == 0x00001000ul)
#define OPX_JKR_RHF_RCV_TYPE_OTHER(_rhf)        ((_rhf & 0x00006000ul) != 0x00000000ul)

/* Common (jkr) handler to WFR/JKR 9B (for now) */
int opx_jkr_rhf_error_handler(const uint64_t rhf_rcvd, const union fi_opx_hfi1_packet_hdr *const hdr);

__OPX_FORCE_INLINE__ int opx_jkr_rhf_check_header(const uint64_t rhf_rcvd, const union fi_opx_hfi1_packet_hdr *const hdr)
{
	/* RHF error */
	if (OFI_UNLIKELY(OPX_JKR_IS_ERRORED_RHF(rhf_rcvd))) return 1; /* error */

	/* Bad packet header */
	if (OFI_UNLIKELY((!OPX_JKR_RHF_IS_USE_EGR_BUF(rhf_rcvd)) &&
	    (ntohs(hdr->stl.lrh.pktlen) > 0x15) &&
	    !(OPX_JKR_RHF_RCV_TYPE_EXPECTED_RCV(rhf_rcvd))))
		return opx_jkr_rhf_error_handler(rhf_rcvd, hdr); /* error */
	else
		return 0; /* no error*/
}
#define OPX_JKR_RHF_CHECK_HEADER(_rhf_rcvd, _hdr) opx_jkr_rhf_check_header(_rhf_rcvd, _hdr)


#endif
