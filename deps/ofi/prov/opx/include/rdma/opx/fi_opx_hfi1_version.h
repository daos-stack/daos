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
#ifndef _OPX_HFI1_VERSION_H_
#define _OPX_HFI1_VERSION_H_

#include "rdma/opx/fi_opx_hfi1_wfr.h"
#include "rdma/opx/fi_opx_hfi1_jkr.h"


/*******************************************/
/* These are the same defines both WFR/JKR */
/*******************************************/
// RHF changes
// Common to both JKR/WFR

#define OPX_RHF_RCV_TYPE_EXPECTED_RCV(_rhf) ((_rhf & 0x00007000ul) == 0x00000000ul)
#define OPX_RHF_RCV_TYPE_EAGER_RCV(_rhf)    ((_rhf & 0x00001000ul) == 0x00001000ul)
#define OPX_RHF_RCV_TYPE_OTHER(_rhf)        ((_rhf & 0x00006000ul) != 0x00000000ul)


#define OPX_PBC_CR(cr) ((cr & FI_OPX_HFI1_PBC_CR_MASK) << FI_OPX_HFI1_PBC_CR_SHIFT)
#define OPX_PBC_LEN(len) (len)
#define OPX_PBC_VL(vl) ((vl & FI_OPX_HFI1_PBC_VL_MASK) << FI_OPX_HFI1_PBC_VL_SHIFT)

/* Note: double check JKR sc bits */
#define OPX_PBC_SC(sc) (((sc >> FI_OPX_HFI1_PBC_SC4_SHIFT) & FI_OPX_HFI1_PBC_SC4_MASK) << FI_OPX_HFI1_PBC_DCINFO_SHIFT)

/* PBC most significant bits shift (32 bits) defines */
#define OPX_PBC_MSB_SHIFT                   32


#if (defined(OPX_WFR) && !defined(OPX_JKR))
/***************************************************************/
/* WFR Build specific definitions                              */
/***************************************************************/

    #define OPX_PBC_DLID                OPX_PBC_WFR_DLID
    #define OPX_PBC_SCTXT               OPX_PBC_WFR_SCTXT
    #define OPX_PBC_L2COMPRESSED        OPX_PBC_WFR_L2COMPRESSED
    #define OPX_PBC_PORTIDX             OPX_PBC_WFR_PORTIDX
    #define OPX_PBC_L2TYPE              OPX_PBC_WFR_L2TYPE
    #define OPX_PBC_RUNTIME             OPX_PBC_WFR_RUNTIME
    #define OPX_PBC_LRH_DLID_TO_PBC_DLID   OPX_PBC_WFR_LRH_DLID_TO_PBC_DLID

#elif (defined(OPX_JKR) && !defined(OPX_WFR))
/***************************************************************/
/* JKR Build specific definitions                              */
/***************************************************************/

    #define OPX_PBC_DLID                OPX_PBC_JKR_DLID
    #define OPX_PBC_SCTXT               OPX_PBC_JKR_SCTXT
    #define OPX_PBC_L2COMPRESSED        OPX_PBC_JKR_L2COMPRESSED
    #define OPX_PBC_PORTIDX             OPX_PBC_JKR_PORTIDX
    #define OPX_PBC_L2TYPE              OPX_PBC_JKR_L2TYPE
    #define OPX_PBC_RUNTIME             OPX_PBC_JKR_RUNTIME
    #define OPX_PBC_LRH_DLID_TO_PBC_DLID   OPX_PBC_JKR_LRH_DLID_TO_PBC_DLID

#elif (defined(OPX_JKR) && defined(OPX_WFR))
/***************************************************************/
/* Both JKR and WFR runtime support (not build-time constants) */
/***************************************************************/

    #define OPX_PBC_DLID(dlid)    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
	OPX_PBC_JKR_DLID(dlid) : OPX_PBC_WFR_DLID(dlid))

    #define OPX_PBC_SCTXT(ctx)    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
	OPX_PBC_JKR_SCTXT(ctx) : OPX_PBC_WFR_SCTXT(ctx))

    #define OPX_PBC_L2COMPRESSED(c)  ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?      \
	OPX_PBC_JKR_L2COMPRESSED(c) : OPX_PBC_WFR_L2COMPRESSED(c))

    #define OPX_PBC_PORTIDX(pidx) ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
	OPX_PBC_JKR_PORTIDX(pidx) : OPX_PBC_WFR_PORTIDX(pidx))

    #define OPX_PBC_LRH_DLID_TO_PBC_DLID(dlid)  ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_PBC_JKR_LRH_DLID_TO_PBC_DLID(dlid) : OPX_PBC_WFR_LRH_DLID_TO_PBC_DLID(dlid))

    
/* Mixed WFR/JKR header support must be 9B */
#ifndef NDEBUG

    __OPX_FORCE_INLINE__
    uint32_t opx_pbc_l2type(unsigned type)
    {
    	assert(type == OPX_PBC_JKR_L2TYPE_9B);
    	return ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?
		OPX_PBC_JKR_L2TYPE(type) : OPX_PBC_WFR_L2TYPE(type));
    }
    #define OPX_PBC_L2TYPE(type) opx_pbc_l2type(type)
#else

    #define OPX_PBC_L2TYPE(type) ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?          \
	OPX_PBC_JKR_L2TYPE(OPX_PBC_JKR_L2TYPE_9B) :                          \
	OPX_PBC_WFR_L2TYPE(OPX_PBC_JKR_L2TYPE_9B)) /* OPX_PBC_WFR_UNUSED */
#endif

    /* One runtime check for mutiple fields - DLID, PORT, L2TYPE */
    #define OPX_PBC_RUNTIME(dlid, pidx) ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?   \
	(OPX_PBC_JKR_DLID(dlid) | OPX_PBC_JKR_PORTIDX(pidx)) :               \
	(OPX_PBC_WFR_DLID(dlid) | OPX_PBC_WFR_PORTIDX(pidx)) )


#else /* ERROR */
    #warning Should not happen  Not WFR and Not JKR
    #error "NOT WFR AND NOT JKR"
#endif

#define OPX_BTH_UNUSED 0  // Default unsupported values to 0

#if (defined(OPX_JKR) && !defined(OPX_WFR))
/***************************************************************/
/* JKR Build specific definitions                              */
/***************************************************************/

#define OPX_BTH_CSPEC(_cspec)   OPX_BTH_JKR_CSPEC(_cspec)
#define OPX_BTH_RC2(_rc2)       OPX_BTH_JKR_RC2(_rc2)
#define OPX_BTH_CSPEC_DEFAULT   OPX_BTH_UNUSED // Cspec is not used in 9B header
#define OPX_BTH_RC2_VAL         OPX_BTH_JKR_RC2_VAL

#elif (defined(OPX_WFR) && !defined(OPX_JKR))
/***************************************************************/
/* WKR Build specific definitions                              */
/***************************************************************/

#define OPX_BTH_RC2(_rc2)            OPX_BTH_UNUSED
#define OPX_BTH_CSPEC(_cspec)        OPX_BTH_UNUSED
#define OPX_BTH_CSPEC_DEFAULT        OPX_BTH_UNUSED
#define OPX_BTH_RC2_VAL              OPX_BTH_UNUSED

#elif (defined(OPX_JKR) && defined(OPX_WFR))
/***************************************************************/
/* Both JKR and WFR runtime support (not build-time constants) */
/***************************************************************/

#define OPX_BTH_RC2(_rc2)    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
	OPX_BTH_JKR_RC2(_rc2) : OPX_BTH_UNUSED)
#define OPX_BTH_CSPEC(_cspec)   ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_BTH_JKR_CSPEC(_cspec) : OPX_BTH_UNUSED)
#define OPX_BTH_CSPEC_DEFAULT  OPX_BTH_UNUSED // Cspec is not used in 9B header
#define OPX_BTH_RC2_VAL     ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_BTH_JKR_RC2_VAL     : OPX_BTH_UNUSED)

#endif

#if (defined(OPX_JKR) && !defined(OPX_WFR))
/***************************************************************/
/* JKR Build specific definitions                              */
/***************************************************************/
#define OPX_RHF_SEQ_NOT_MATCH  OPX_JKR_RHF_SEQ_NOT_MATCH
#define OPX_RHF_SEQ_INCREMENT  OPX_JKR_RHF_SEQ_INCREMENT
#define OPX_IS_ERRORED_RHF     OPX_JKR_IS_ERRORED_RHF
#define OPX_RHF_SEQ_MATCH      OPX_JKR_RHF_SEQ_MATCH
#define OPX_RHF_SEQ_INIT_VAL   OPX_JKR_RHF_SEQ_INIT_VAL
#define OPX_RHF_IS_USE_EGR_BUF OPX_JKR_RHF_IS_USE_EGR_BUF
#define OPX_RHF_EGR_INDEX      OPX_JKR_RHF_EGR_INDEX
#define OPX_RHF_EGR_OFFSET     OPX_JKR_RHF_EGR_OFFSET
#define OPX_RHF_HDRQ_OFFSET    OPX_JKR_RHF_HDRQ_OFFSET

#define OPX_RHE_DEBUG          OPX_JKR_RHE_DEBUG
#define OPX_RHF_CHECK_HEADER   OPX_JKR_RHF_CHECK_HEADER

#elif (defined(OPX_WFR) && !defined(OPX_JKR))
/***************************************************************/
/* WKR Build specific definitions                              */
/***************************************************************/
#define OPX_RHF_SEQ_NOT_MATCH  OPX_WFR_RHF_SEQ_NOT_MATCH
#define OPX_RHF_SEQ_INCREMENT  OPX_WFR_RHF_SEQ_INCREMENT
#define OPX_IS_ERRORED_RHF     OPX_WFR_IS_ERRORED_RHF
#define OPX_RHF_SEQ_MATCH      OPX_WFR_RHF_SEQ_MATCH
#define OPX_RHF_SEQ_INIT_VAL   OPX_WFR_RHF_SEQ_INIT_VAL
#define OPX_RHF_IS_USE_EGR_BUF OPX_WFR_RHF_IS_USE_EGR_BUF
#define OPX_RHF_EGR_INDEX      OPX_WFR_RHF_EGR_INDEX
#define OPX_RHF_EGR_OFFSET     OPX_WFR_RHF_EGR_OFFSET
#define OPX_RHF_HDRQ_OFFSET    OPX_WFR_RHF_HDRQ_OFFSET

#define OPX_RHE_DEBUG          OPX_WFR_RHE_DEBUG
#define OPX_RHF_CHECK_HEADER   OPX_WFR_RHF_CHECK_HEADER

#elif (defined(OPX_JKR) && defined(OPX_WFR))
/***************************************************************/
/* Both JKR and WFR runtime support (not build-time constants) */
/* Constant macro magic will be used later for this            */
/***************************************************************/
#define OPX_RHF_SEQ_NOT_MATCH(_seq, _rhf)   ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_SEQ_NOT_MATCH(_seq, _rhf) : OPX_WFR_RHF_SEQ_NOT_MATCH(_seq, _rhf))

#define OPX_RHF_SEQ_INCREMENT(_seq)    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_SEQ_INCREMENT(_seq) : OPX_WFR_RHF_SEQ_INCREMENT(_seq))

#define OPX_IS_ERRORED_RHF(_rhf)       ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_IS_ERRORED_RHF(_rhf) : OPX_WFR_IS_ERRORED_RHF(_rhf))

#define OPX_RHF_SEQ_MATCH(_seq, _rhf)   ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_SEQ_MATCH(_seq, _rhf) : OPX_WFR_RHF_SEQ_MATCH(_seq, _rhf))

#define OPX_RHF_SEQ_INIT_VAL   ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_SEQ_INIT_VAL :  OPX_WFR_RHF_SEQ_INIT_VAL)

#define OPX_RHF_IS_USE_EGR_BUF(_rhf)   ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_IS_USE_EGR_BUF(_rhf) : OPX_WFR_RHF_IS_USE_EGR_BUF(_rhf))

#define OPX_RHF_EGR_INDEX(_rhf)      ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_EGR_INDEX(_rhf) : OPX_WFR_RHF_EGR_INDEX(_rhf))

#define OPX_RHF_EGR_OFFSET(_rhf)     ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_EGR_OFFSET(_rhf) : OPX_WFR_RHF_EGR_OFFSET(_rhf))

#define OPX_RHF_HDRQ_OFFSET(_rhf)    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_HDRQ_OFFSET(_rhf) : OPX_WFR_RHF_HDRQ_OFFSET(_rhf))

#define OPX_RHE_DEBUG(_opx_ep, _rhe_ptr, _rhf_ptr, _rhf_msb, _rhf_lsb, _rhf_seq, _hdrq_offset, _rhf_rcvd, _hdr)    \
    ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?                                                                       \
    OPX_JKR_RHE_DEBUG(_opx_ep, _rhe_ptr, _rhf_ptr, _rhf_msb, _rhf_lsb, _rhf_seq, _hdrq_offset, _rhf_rcvd, _hdr) :  \
    OPX_WFR_RHE_DEBUG(_opx_ep, _rhe_ptr, _rhf_ptr, _rhf_msb, _rhf_lsb, _rhf_seq, _hdrq_offset, _rhf_rcvd, _hdr))

#define OPX_RHF_CHECK_HEADER(_rhf_rcvd, _hdr)     ((OPX_HFI1_TYPE == OPX_HFI1_JKR) ?         \
    OPX_JKR_RHF_CHECK_HEADER(_rhf_rcvd, _hdr) : OPX_WFR_RHF_CHECK_HEADER(_rhf_rcvd, _hdr)

#endif

#endif
